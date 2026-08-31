#include "pi1mhz_elkulator.h"

#include "pi1mhz_mailbox.h"
#include "pi1mhz_net_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int initialised;
static int enabled;
static pi1mhz_host_clock host_clock;
static pi1mhz_mailbox mailbox;
static pi1mhz_net_backend *backend;
static pi1mhz_bus_profile bus_profile = PI1MHZ_BUS_AP5;
static int noe_enabled = 1;
static FILE *bus_trace;
static unsigned long long host_cycles;
static unsigned bus_trace_events;

static int trace_address(uint16_t address)
{
    return (address >= 0xFCA6u && address <= 0xFCAAu) ||
           (address >= 0xFCFDu && address <= 0xFDFFu) ||
           (address >= 0xFEE0u && address <= 0xFEFFu);
}

/* Elkulator keeps the previous instruction's address in oldpc2; naming the
 * writer is the difference between knowing a JIM byte changed and knowing
 * which routine changed it. rombank says which sideways ROM was paged in,
 * so a host address can be attributed to the right ROM. */
extern uint16_t oldpc2;
extern int rombank;

static void trace_bus(char operation, uint16_t address, int value)
{
    uint32_t page;
    uint32_t jim_address = 0xFFFFFFFFu;

    if (!bus_trace || !trace_address(address))
        return;
    page = mailbox.page & 0xFFFFFFu;
    if (address >= 0xFD00u && address <= 0xFDFFu)
        jim_address = (page << 8) | (address & 0xFFu);
    fprintf(bus_trace, "%llu %c %04X ", host_cycles, operation, address);
    if (value < 0)
        fputs("--", bus_trace);
    else
        fprintf(bus_trace, "%02X", value & 0xFF);
    fprintf(bus_trace, " page=%06X", page);
    if (jim_address != 0xFFFFFFFFu)
        fprintf(bus_trace, " jim=%08X", jim_address);
    fprintf(bus_trace, " pc=%04X rb=%X", oldpc2, rombank & 0xF);
    fputc('\n', bus_trace);
    if ((++bus_trace_events & 0xFFu) == 0u)
        fflush(bus_trace);
}

static int configure_fiq_timing(void)
{
    const char *setting = getenv("PI1MHZ_FIQ_DELAY_ACCESSES");
    const char *service_setting = getenv("PI1MHZ_SERVICE_DELAY_CYCLES");
    char *end = NULL;
    unsigned long delay;
    unsigned long service_delay = 4;

    /* Conservative fault-injection defaults. Exact latency varies with Pi
       model, firmware work and callback type, so callers can sweep both the
       capture and service delays rather than treating these as measurements. */
    if (service_setting && *service_setting) {
        service_delay = strtoul(service_setting, &end, 0);
        if (!end || *end || service_delay > 10000000u) {
            fprintf(stderr, "Pi1MHz mailbox: invalid service delay %s\n",
                    service_setting);
            return -1;
        }
    }
    pi1mhz_mailbox_set_timing(&mailbox, 2, 4, (unsigned)service_delay);
    pi1mhz_mailbox_set_callback_timing(&mailbox, 4, 14, 0);
    if (!setting || !*setting) {
        fprintf(stderr, "Pi1MHz mailbox: conservative timing profile "
                "capture=2 simple=4 page=14 service=%lu cycles\n",
                service_delay);
        return 0;
    }
    delay = strtoul(setting, &end, 0);
    if (!end || *end || delay > 255u) {
        fprintf(stderr, "Pi1MHz mailbox: invalid FIQ delay %s\n", setting);
        return -1;
    }
    if (delay == 0) {
        pi1mhz_mailbox_set_timing(&mailbox, 0, 0, 0);
        pi1mhz_mailbox_set_callback_timing(&mailbox, 0, 0, 0);
    } else {
        pi1mhz_mailbox_set_timing(&mailbox, (unsigned)delay, 4,
                                  (unsigned)service_delay);
        pi1mhz_mailbox_set_callback_timing(&mailbox, 4, 14, 0);
    }
    fprintf(stderr, "Pi1MHz mailbox: compatibility capture delay %lu cycles\n",
            delay);
    return 0;
}

/* PI1MHZ_JIM_MIRROR=OFFSET:HEXBYTES publishes a region the Pi serves at the
 * same offset in every JIM page, so a host trampoline placed there is reachable
 * whatever the page selector holds. Diagnostic wiring for that design. */
static int preload_mirror(void)
{
    const char *setting = getenv("PI1MHZ_JIM_MIRROR");
    const char *colon;
    unsigned long offset;
    uint8_t bytes[160];
    size_t count = 0;
    char *end;

    if (!setting || !*setting)
        return 0;
    offset = strtoul(setting, &end, 16);
    if (*end != ':' || offset > 0xFFu)
        return -1;
    colon = end + 1;
    while (colon[0] && colon[1] && count < sizeof(bytes)) {
        char pair[3] = { colon[0], colon[1], 0 };
        bytes[count++] = (uint8_t)strtoul(pair, &end, 16);
        if (*end)
            return -1;
        colon += 2;
    }
    if (!count)
        return -1;
    pi1mhz_mailbox_set_mirror(&mailbox, (uint8_t)offset, bytes,
                              (uint8_t)count);
    fprintf(stderr, "Pi1MHz mailbox: mirrored %zu bytes at page offset &%02lX\n",
            count, offset);
    return 0;
}

static int preload_jim(void)
{
    const char *path = getenv("PI1MHZ_JIM_IMAGE");
    const char *address_setting = getenv("PI1MHZ_JIM_IMAGE_ADDRESS");
    char *end = NULL;
    unsigned long address = 0;
    FILE *file;
    long length;
    size_t loaded;

    if (!path || !*path)
        return 0;
    if (address_setting && *address_setting) {
        address = strtoul(address_setting, &end, 0);
        if (!end || *end || address >= mailbox.jim_size) {
            fprintf(stderr, "Pi1MHz mailbox: invalid JIM image address %s\n",
                    address_setting);
            return -1;
        }
    }
    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Pi1MHz mailbox: cannot open JIM image %s\n", path);
        return -1;
    }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "Pi1MHz mailbox: cannot size JIM image %s\n", path);
        fclose(file);
        return -1;
    }
    if ((size_t)length > mailbox.jim_size - (size_t)address) {
        fprintf(stderr, "Pi1MHz mailbox: JIM image exceeds available RAM\n");
        fclose(file);
        return -1;
    }
    loaded = fread(mailbox.jim + address, 1, (size_t)length, file);
    if (loaded != (size_t)length || fclose(file)) {
        fprintf(stderr, "Pi1MHz mailbox: failed reading JIM image %s\n", path);
        return -1;
    }
    fprintf(stderr, "Pi1MHz mailbox: loaded %lu bytes at &%06lX from %s\n",
            (unsigned long)loaded, address, path);
    return 0;
}

static void shutdown_device(void)
{
    if (bus_trace) {
        fclose(bus_trace);
        bus_trace = NULL;
    }
    pi1mhz_mailbox_destroy(&mailbox);
    pi1mhz_net_backend_destroy(backend);
    backend = NULL;
}

static void initialise_device(void)
{
    const char *mode;
    const char *trace;
    const char *exit_setting;
    const char *profile_setting;
    const char *noe_setting;
    const char *bus_trace_path;
    if (initialised)
        return;
    initialised = 1;
    mode = getenv("PI1MHZ_MAILBOX");
    if (!mode || (!strcmp(mode, "0") || !strcmp(mode, "off")))
        return;
    profile_setting = getenv("PI1MHZ_AP5_PROFILE");
    noe_setting = getenv("PI1MHZ_NOE");
    if (noe_setting && strcmp(noe_setting, "0") &&
        strcmp(noe_setting, "1")) {
        fprintf(stderr, "Pi1MHz mailbox: invalid PI1MHZ_NOE %s\n", noe_setting);
        return;
    }
    if (noe_setting)
        noe_enabled = strcmp(noe_setting, "0") != 0;
    if (profile_setting && !strcmp(profile_setting, "full"))
        bus_profile = PI1MHZ_BUS_AP5_FULL_FRED;
    else if (profile_setting && !strcmp(profile_setting, "expanded-snoop"))
        bus_profile = PI1MHZ_BUS_AP5_EXPANDED_SNOOP;
    else if (profile_setting && strcmp(profile_setting, "original")) {
        fprintf(stderr, "Pi1MHz mailbox: invalid AP5 profile %s\n",
                profile_setting);
        return;
    } else if (!profile_setting && getenv("PI1MHZ_AP5_FULL_FRED") &&
               strcmp(getenv("PI1MHZ_AP5_FULL_FRED"), "0"))
        bus_profile = PI1MHZ_BUS_AP5_FULL_FRED;
    trace = getenv("PI1MHZ_TRACE");
    bus_trace_path = getenv("PI1MHZ_BUS_TRACE");
    exit_setting = getenv("PI1MHZ_EXIT_ON_CLOSE");
    backend = pi1mhz_net_backend_create(
        mode, trace, exit_setting && strcmp(exit_setting, "0"));
    if (!backend || pi1mhz_mailbox_init(
            &mailbox, pi1mhz_net_backend_dispatch, backend)) {
        fprintf(stderr, "Pi1MHz mailbox: initialisation failed\n");
        pi1mhz_net_backend_destroy(backend);
        backend = NULL;
        return;
    }
    if (configure_fiq_timing() || preload_jim() || preload_mirror()) {
        pi1mhz_mailbox_destroy(&mailbox);
        pi1mhz_net_backend_destroy(backend);
        backend = NULL;
        return;
    }
    if (bus_trace_path && *bus_trace_path) {
        bus_trace = fopen(bus_trace_path, "w");
        if (!bus_trace) {
            fprintf(stderr, "Pi1MHz mailbox: cannot open bus trace %s\n",
                    bus_trace_path);
            shutdown_device();
            return;
        }
        /* The acceptance runner checks this header while Elkulator is still
           running.  Line buffering also preserves the last completed bus
           operation if a fault terminates the emulator. */
        setvbuf(bus_trace, NULL, _IOLBF, 0);
        fputs("# cycle op address value selected-page mapped-jim\n", bus_trace);
    }
    enabled = 1;
    atexit(shutdown_device);
    fprintf(stderr, "Pi1MHz mailbox: %s backend enabled\n", mode);
}

int pi1mhz_elkulator_enabled(void)
{
    initialise_device();
    return enabled;
}

int pi1mhz_elkulator_handles_read(uint16_t address)
{
    if (!pi1mhz_elkulator_enabled())
        return 0;
    return pi1mhz_mailbox_profile_read_driven(
        &mailbox, bus_profile, address, noe_enabled);
}

int pi1mhz_elkulator_handles_write(uint16_t address)
{
    if (!pi1mhz_elkulator_enabled())
        return 0;
    return pi1mhz_mailbox_profile_handles_mode(
        bus_profile, address, 1, noe_enabled);
}

void pi1mhz_elkulator_snoop_read(uint16_t address)
{
    if (pi1mhz_elkulator_enabled() &&
        pi1mhz_mailbox_profile_snoops(bus_profile, address, 0)) {
        uint8_t value = pi1mhz_mailbox_bus_access(
            &mailbox, address, 0xFFu, 0);
        trace_bus('R', address, value);
    } else if (address >= 0xFEE0u && address <= 0xFEFFu) {
        /* The Tube device supplies the value after this snoop hook. */
        trace_bus('R', address, -1);
    }
}

void pi1mhz_elkulator_snoop_write(uint16_t address, uint8_t value)
{
    if (pi1mhz_elkulator_enabled())
        trace_bus('W', address, value);
    if (pi1mhz_elkulator_enabled() &&
        pi1mhz_mailbox_profile_snoops(bus_profile, address, 1))
        (void)pi1mhz_mailbox_bus_access(&mailbox, address, value, 1);
}

uint8_t pi1mhz_elkulator_read(uint16_t address)
{
    uint8_t value = pi1mhz_mailbox_bus_access(&mailbox, address, 0xFFu, 0);
    trace_bus('R', address, value);
    return value;
}

void pi1mhz_elkulator_write(uint16_t address, uint8_t value)
{
    trace_bus('W', address, value);
    (void)pi1mhz_mailbox_bus_access(&mailbox, address, value, 1);
}

void pi1mhz_elkulator_sync_host_clock(int host_cycle_counter)
{
    unsigned elapsed;

    if (!enabled)
        return;
    elapsed = pi1mhz_host_clock_sync(&host_clock, host_cycle_counter);
    host_cycles += elapsed;
    if (elapsed > 0)
        pi1mhz_mailbox_tick_fiq(&mailbox, elapsed);
}

void pi1mhz_elkulator_rebase_host_clock(int host_cycle_counter)
{
    pi1mhz_host_clock_rebase(&host_clock, host_cycle_counter);
}

/* The JIM page currently mapped at &FD00. Recorded by the vector trace, which
 * is how the selector was shown to be whatever the last transfer left it. */
uint32_t pi1mhz_elkulator_selected_page(void)
{
    return mailbox.page & 0xFFFFFFu;
}

#include "pi1mhz_mailbox.h"

#include <stdlib.h>
#include <string.h>

#define PI1MHZ_JIM_SET_SIZE (1u << 24)
#define PI1MHZ_BUS_BASE 0xFC00u

static void enable_vpu_output(pi1mhz_mailbox *mailbox, uint16_t address)
{
    if (address >= PI1MHZ_BUS_BASE && address <= PI1MHZ_PAGE_END)
        mailbox->output_enabled[address - PI1MHZ_BUS_BASE] = 1;
}

static void enable_vpu_page_output(pi1mhz_mailbox *mailbox)
{
    memset(mailbox->output_enabled +
               (PI1MHZ_PAGE_BASE - PI1MHZ_BUS_BASE),
           1, PI1MHZ_PAGE_END - PI1MHZ_PAGE_BASE + 1u);
}

static size_t page_base_address(const pi1mhz_mailbox *mailbox)
{
    size_t set = (size_t)(mailbox->page >> 16) * PI1MHZ_JIM_SET_SIZE;

    /* Pi1MHz clamps an unavailable FCFD set selector to the final installed
       16 MiB JIM set. The callback also publishes that clamped selector. */
    if (set >= mailbox->jim_size)
        set = ((mailbox->jim_size - 1u) / PI1MHZ_JIM_SET_SIZE) *
              PI1MHZ_JIM_SET_SIZE;
    return set + ((size_t)(mailbox->page & 0xFFFFu) << 8);
}

static void publish_service_cursor(pi1mhz_mailbox *mailbox, int drive)
{
    size_t absolute;

    mailbox->address &= 0xFFFFFFu;
    absolute = mailbox->services_base + mailbox->address;
    mailbox->vpu_registers[0] = (uint8_t)mailbox->address;
    mailbox->vpu_registers[1] = (uint8_t)(mailbox->address >> 8);
    mailbox->vpu_registers[2] = (uint8_t)(mailbox->address >> 16);
    mailbox->vpu_registers[3] = mailbox->jim[absolute];
    if (drive) {
        for (uint16_t address = PI1MHZ_REG_ADDR_LO;
             address <= PI1MHZ_REG_DATA; ++address)
            enable_vpu_output(mailbox, address);
    }
}

static void publish_page_window(pi1mhz_mailbox *mailbox)
{
    size_t base = page_base_address(mailbox);

    if (base <= mailbox->jim_size - sizeof(mailbox->page_window))
        memcpy(mailbox->page_window, mailbox->jim + base,
               sizeof(mailbox->page_window));
    else
        memset(mailbox->page_window, 0xFF, sizeof(mailbox->page_window));
}

static void complete_active_callback(pi1mhz_mailbox *mailbox)
{
    uint16_t address = mailbox->active.address;
    uint8_t value = mailbox->active.value;

    mailbox->active.valid = 0;
    mailbox->callback_ticks_remaining = 0;
    if (!mailbox->active.write) {
        if (address == PI1MHZ_REG_DATA) {
            mailbox->address = (mailbox->address + 1u) & 0xFFFFFFu;
            publish_service_cursor(mailbox, 1);
        }
        return;
    }

    switch (address) {
    case PI1MHZ_REG_ADDR_LO:
        mailbox->address = (mailbox->address & 0xFFFF00u) | value;
        publish_service_cursor(mailbox, 1);
        break;
    case PI1MHZ_REG_ADDR_MID:
        mailbox->address = (mailbox->address & 0xFF00FFu) |
                           ((uint32_t)value << 8);
        publish_service_cursor(mailbox, 1);
        break;
    case PI1MHZ_REG_ADDR_HI:
        mailbox->address = (mailbox->address & 0x00FFFFu) |
                           ((uint32_t)value << 16);
        publish_service_cursor(mailbox, 1);
        break;
    case PI1MHZ_REG_DATA:
        mailbox->jim[mailbox->services_base + mailbox->address] = value;
        mailbox->address = (mailbox->address + 1u) & 0xFFFFFFu;
        publish_service_cursor(mailbox, 1);
        break;
    case PI1MHZ_REG_COMMAND:
        enable_vpu_output(mailbox, address);
        mailbox->vpu_registers[4] = value;
        if (value >= 0xF0u) {
            mailbox->selector = value;
            mailbox->result = PI1MHZ_NET_BUSY;
            mailbox->vpu_registers[4] = PI1MHZ_NET_BUSY;
            /* FIQ acceptance publishes BUSY only. The foreground service
               dispatcher runs later from scheduler time, as net_service_poll
               and secure_service_poll do on Pi1MHz. */
            mailbox->service_pending = 1;
            mailbox->service_ticks_remaining = mailbox->service_delay_ticks;
        } else {
            mailbox->result = value;
            mailbox->service_pending = 0;
        }
        break;
    case PI1MHZ_REG_PAGE_HI:
        {
            uint8_t max_set = (uint8_t)((mailbox->jim_size - 1u) /
                                        PI1MHZ_JIM_SET_SIZE);
            if (value > max_set)
                value = max_set;
        }
        mailbox->page = (mailbox->page & 0x00FFFFu) |
                        ((uint32_t)value << 16);
        mailbox->vpu_registers[5] = value;
        publish_page_window(mailbox);
        enable_vpu_page_output(mailbox);
        enable_vpu_output(mailbox, address);
        break;
    case PI1MHZ_REG_PAGE_MID:
        mailbox->page = (mailbox->page & 0xFF00FFu) |
                        ((uint32_t)value << 8);
        publish_page_window(mailbox);
        enable_vpu_page_output(mailbox);
        enable_vpu_output(mailbox, address);
        break;
    case PI1MHZ_REG_PAGE_LO:
        mailbox->page = (mailbox->page & 0xFFFF00u) | value;
        publish_page_window(mailbox);
        enable_vpu_page_output(mailbox);
        enable_vpu_output(mailbox, address);
        break;
    default:
        if (address >= PI1MHZ_PAGE_BASE && address <= PI1MHZ_PAGE_END) {
            size_t jim_address = page_base_address(mailbox) |
                                 (address & 0xFFu);
            if (jim_address < mailbox->jim_size)
                mailbox->jim[jim_address] = value;
            mailbox->page_window[address & 0xFFu] = value;
            enable_vpu_output(mailbox, address);
        }
        break;
    }
}

static void complete_service(pi1mhz_mailbox *mailbox)
{
    uint32_t cp = (uint32_t)mailbox->services_base | 0xFF0000u |
                  ((uint32_t)mailbox->selector << 8);

    mailbox->service_pending = 0;
    mailbox->service_ticks_remaining = 0;
    mailbox->result = mailbox->dispatch(
        mailbox->dispatch_opaque, mailbox->selector, cp,
        mailbox->jim, mailbox->jim_size);
    mailbox->vpu_registers[4] = mailbox->result;
    enable_vpu_output(mailbox, PI1MHZ_REG_COMMAND);
    if (mailbox->capture_delay_ticks == 0 &&
        mailbox->callback_delay_ticks == 0 &&
        mailbox->service_delay_ticks == 0)
        mailbox->legacy_busy_observation = 1;
}

static void capture_posted_event(pi1mhz_mailbox *mailbox)
{
    mailbox->active = mailbox->posted;
    mailbox->active.valid = 1;
    mailbox->posted.valid = 0;
    mailbox->capture_ticks_remaining = 0;
    if (!mailbox->active.write &&
        mailbox->active.address != PI1MHZ_REG_DATA)
        mailbox->callback_ticks_remaining =
            mailbox->read_callback_delay_ticks;
    else if (mailbox->active.write &&
             mailbox->active.address >= PI1MHZ_REG_PAGE_HI &&
             mailbox->active.address <= PI1MHZ_REG_PAGE_LO)
        mailbox->callback_ticks_remaining =
            mailbox->page_callback_delay_ticks;
    else
        mailbox->callback_ticks_remaining = mailbox->callback_delay_ticks;
}

static void drain_zero_time_transitions(pi1mhz_mailbox *mailbox)
{
    int changed;

    do {
        changed = 0;
        if (!mailbox->active.valid && mailbox->posted.valid &&
            mailbox->capture_ticks_remaining == 0) {
            capture_posted_event(mailbox);
            changed = 1;
        }
        if (mailbox->active.valid &&
            mailbox->callback_ticks_remaining == 0) {
            complete_active_callback(mailbox);
            changed = 1;
        }
        if (!mailbox->active.valid && mailbox->service_pending &&
            mailbox->service_ticks_remaining == 0) {
            complete_service(mailbox);
            changed = 1;
        }
    } while (changed);
}

void pi1mhz_mailbox_tick_fiq(pi1mhz_mailbox *mailbox, unsigned ticks)
{
    if (!mailbox)
        return;
    while (ticks--) {
        /* Foreground service work can progress only while FIQ is not inside
           a callback. Work created by this tick starts on the next tick. */
        if (!mailbox->active.valid && mailbox->service_pending &&
            mailbox->service_ticks_remaining > 0 &&
            --mailbox->service_ticks_remaining == 0)
            complete_service(mailbox);

        /* A second doorbell can become pending while FIQ is masked inside the
           active callback. Its latency elapses, but capture waits for return. */
        if (mailbox->posted.valid && mailbox->capture_ticks_remaining > 0)
            --mailbox->capture_ticks_remaining;

        if (mailbox->active.valid) {
            if (mailbox->callback_ticks_remaining > 0 &&
                --mailbox->callback_ticks_remaining == 0)
                complete_active_callback(mailbox);
        }
        drain_zero_time_transitions(mailbox);
    }
}

static void post_fiq_event(pi1mhz_mailbox *mailbox, uint16_t address,
                           uint8_t value, int write)
{
    /* Before FIQ captures the shared VPU word, a newer bus cycle overwrites
       it. Once captured, the active callback is private ARM state and the one
       VPU slot is free to retain the next event. */
    int already_pending = mailbox->posted.valid;

    mailbox->posted.address = address;
    mailbox->posted.value = value;
    mailbox->posted.write = write ? 1u : 0u;
    mailbox->posted.valid = 1u;
    /* Ringing an already-pending doorbell replaces the shared GPIO sample but
       does not restart the interrupt latency already in flight. */
    if (!already_pending)
        mailbox->capture_ticks_remaining = mailbox->capture_delay_ticks;
    drain_zero_time_transitions(mailbox);
}

int pi1mhz_mailbox_init(pi1mhz_mailbox *mailbox,
                        pi1mhz_dispatch_fn dispatch, void *opaque)
{
    if (!mailbox || !dispatch)
        return -1;
    memset(mailbox, 0, sizeof(*mailbox));
    mailbox->jim = (uint8_t *)calloc(PI1MHZ_JIM_SIZE, 1);
    if (!mailbox->jim)
        return -1;
    mailbox->jim_size = PI1MHZ_JIM_SIZE;
    mailbox->services_base = mailbox->jim_size - PI1MHZ_SERVICE_SIZE;
    mailbox->dispatch = dispatch;
    mailbox->dispatch_opaque = opaque;
    publish_service_cursor(mailbox, 0);
    publish_page_window(mailbox);
    /* rampage_emulator_init publishes the initial JIM page. Services init
       publishes only its command and IRQ/result bytes. Other readback bytes
       become driven when their registered callbacks first call MemoryWrite. */
    enable_vpu_page_output(mailbox);
    enable_vpu_output(mailbox, PI1MHZ_REG_COMMAND);
    enable_vpu_output(mailbox, PI1MHZ_REG_IRQ);
    return 0;
}

void pi1mhz_mailbox_destroy(pi1mhz_mailbox *mailbox)
{
    if (!mailbox)
        return;
    free(mailbox->jim);
    memset(mailbox, 0, sizeof(*mailbox));
}

void pi1mhz_mailbox_set_fiq_delay(pi1mhz_mailbox *mailbox,
                                  unsigned delay_ticks)
{
    pi1mhz_mailbox_set_timing(mailbox, delay_ticks, 1, 1);
}

void pi1mhz_mailbox_set_timing(pi1mhz_mailbox *mailbox,
                               unsigned capture_ticks,
                               unsigned callback_ticks,
                               unsigned service_ticks)
{
    if (!mailbox)
        return;
    mailbox->capture_delay_ticks = capture_ticks;
    mailbox->callback_delay_ticks = callback_ticks;
    mailbox->page_callback_delay_ticks = callback_ticks;
    mailbox->read_callback_delay_ticks = 0;
    mailbox->service_delay_ticks = service_ticks;
    mailbox->posted.valid = 0;
    mailbox->active.valid = 0;
    mailbox->service_pending = 0;
    mailbox->legacy_busy_observation = 0;
    mailbox->capture_ticks_remaining = 0;
    mailbox->callback_ticks_remaining = 0;
    mailbox->service_ticks_remaining = 0;
    publish_service_cursor(mailbox, 0);
    publish_page_window(mailbox);
}

void pi1mhz_host_clock_rebase(pi1mhz_host_clock *clock,
                              int host_cycle_counter)
{
    if (!clock)
        return;
    clock->valid = 1;
    clock->last_cycle = host_cycle_counter;
}

unsigned pi1mhz_host_clock_sync(pi1mhz_host_clock *clock,
                                int host_cycle_counter)
{
    int elapsed;

    if (!clock)
        return 0;
    if (!clock->valid) {
        pi1mhz_host_clock_rebase(clock, host_cycle_counter);
        return 0;
    }
    elapsed = host_cycle_counter - clock->last_cycle;
    if (elapsed < 0)
        elapsed += 128;
    clock->last_cycle = host_cycle_counter;
    return elapsed > 0 ? (unsigned)elapsed : 0;
}

int pi1mhz_mailbox_read_driven(const pi1mhz_mailbox *mailbox,
                               uint16_t address)
{
    if (!mailbox || address < PI1MHZ_BUS_BASE || address > PI1MHZ_PAGE_END)
        return 0;
    return mailbox->output_enabled[address - PI1MHZ_BUS_BASE] != 0;
}

void pi1mhz_mailbox_set_callback_timing(pi1mhz_mailbox *mailbox,
                                        unsigned simple_ticks,
                                        unsigned page_copy_ticks,
                                        unsigned unhandled_read_ticks)
{
    if (!mailbox)
        return;
    mailbox->callback_delay_ticks = simple_ticks;
    mailbox->page_callback_delay_ticks = page_copy_ticks;
    mailbox->read_callback_delay_ticks = unhandled_read_ticks;
}

int pi1mhz_mailbox_handles(uint16_t address)
{
    return (address >= PI1MHZ_REG_ADDR_LO &&
            address <= PI1MHZ_REG_COMMAND) ||
           (address >= PI1MHZ_REG_PAGE_HI &&
            address <= PI1MHZ_REG_PAGE_LO) ||
           (address >= PI1MHZ_PAGE_BASE && address <= PI1MHZ_PAGE_END);
}

int pi1mhz_mailbox_ap5_handles(uint16_t address)
{
    /* ACP/Pres decoded only &FCFF from the JIM selector group onto the AP5
       1MHz connector. A direct BBC connection can expose all three selectors,
       which is why this is a separate bus-profile predicate. */
    if (address == PI1MHZ_REG_PAGE_HI || address == PI1MHZ_REG_PAGE_MID)
        return 0;
    return pi1mhz_mailbox_handles(address);
}

int pi1mhz_mailbox_profile_handles(pi1mhz_bus_profile profile,
                                   uint16_t address, int write)
{
    return pi1mhz_mailbox_profile_handles_mode(profile, address, write, 1);
}

int pi1mhz_mailbox_profile_handles_mode(pi1mhz_bus_profile profile,
                                        uint16_t address, int write,
                                        int noe_enabled)
{
    if (profile == PI1MHZ_BUS_DIRECT ||
        profile == PI1MHZ_BUS_AP5_FULL_FRED) {
        /* With external nOE enabled, Pi1MHzvc drives only MemoryWrite-enabled
           locations. Pi1MHznOE=0 uses the unconditional read path. */
        if (!write && !noe_enabled)
            return address >= 0xFC00u && address <= PI1MHZ_PAGE_END;
        return pi1mhz_mailbox_handles(address);
    }
    if (profile != PI1MHZ_BUS_AP5 &&
        profile != PI1MHZ_BUS_AP5_EXPANDED_SNOOP)
        return 0;
    if (address == PI1MHZ_REG_PAGE_HI || address == PI1MHZ_REG_PAGE_MID)
        return 0;
    /* AP5 exposes FCFF as a write-only selector. An unhandled read is left to
       the host emulator's floating-bus implementation. */
    if (address == PI1MHZ_REG_PAGE_LO && !write)
        return 0;
    return pi1mhz_mailbox_handles(address);
}

int pi1mhz_mailbox_profile_read_driven(const pi1mhz_mailbox *mailbox,
                                       pi1mhz_bus_profile profile,
                                       uint16_t address, int noe_enabled)
{
    if (!noe_enabled)
        return pi1mhz_mailbox_profile_handles_mode(
            profile, address, 0, noe_enabled);

    if (profile == PI1MHZ_BUS_AP5 ||
        profile == PI1MHZ_BUS_AP5_EXPANDED_SNOOP) {
        if (address == PI1MHZ_REG_PAGE_HI ||
            address == PI1MHZ_REG_PAGE_MID ||
            address == PI1MHZ_REG_PAGE_LO)
            return 0;
        if (!pi1mhz_mailbox_profile_snoops(profile, address, 0))
            return 0;
    } else if (profile != PI1MHZ_BUS_DIRECT &&
               profile != PI1MHZ_BUS_AP5_FULL_FRED) {
        return 0;
    }
    return pi1mhz_mailbox_read_driven(mailbox, address);
}

int pi1mhz_mailbox_profile_snoops(pi1mhz_bus_profile profile,
                                  uint16_t address, int write)
{
    (void)write;
    if (profile == PI1MHZ_BUS_DIRECT)
        return address >= 0xFC00u && address <= PI1MHZ_PAGE_END;
    if (profile == PI1MHZ_BUS_AP5_FULL_FRED ||
        profile == PI1MHZ_BUS_AP5_EXPANDED_SNOOP)
        return (address >= 0xFC00u && address <= 0xFCFFu) ||
               (address >= PI1MHZ_PAGE_BASE && address <= PI1MHZ_PAGE_END);
    if (profile != PI1MHZ_BUS_AP5)
        return 0;
    return (address >= 0xFC00u && address <= 0xFC0Fu) ||
           (address >= 0xFC80u && address <= 0xFC8Fu) ||
           (address >= 0xFCA0u && address <= 0xFCAFu) ||
           address == PI1MHZ_REG_PAGE_LO ||
           (address >= PI1MHZ_PAGE_BASE && address <= PI1MHZ_PAGE_END);
}

uint8_t pi1mhz_mailbox_bus_access(pi1mhz_mailbox *mailbox,
                                  uint16_t address, uint8_t bus_value,
                                  int write)
{
    uint8_t value;
    if (!mailbox || !mailbox->jim)
        return 0xFFu;
    if (write) {
        post_fiq_event(mailbox, address, bus_value, 1);
        return bus_value;
    }
    if (address >= PI1MHZ_PAGE_BASE && address <= PI1MHZ_PAGE_END) {
        /* A filing-system trampoline cannot live at a fixed JIM page, because
         * the selector tracks the stream cursor and is whatever the last
         * transfer left it. Mirroring a small region into every page makes the
         * trampoline reachable regardless of the selector, which is what lets
         * the host put its filing vectors somewhere no game can overwrite.
         * The mirror is off unless a trampoline has been published. */
        if (mailbox->mirror_length &&
            (address & 0xFFu) >= mailbox->mirror_offset) {
            size_t index = (size_t)(address & 0xFFu) - mailbox->mirror_offset;
            if (index < mailbox->mirror_length) {
                value = mailbox->mirror[index];
                post_fiq_event(mailbox, address, value, 0);
                return value;
            }
        }
        value = mailbox->page_window[address & 0xFFu];
        post_fiq_event(mailbox, address, value, 0);
        return value;
    }
    switch (address) {
    case PI1MHZ_REG_ADDR_LO:
        value = mailbox->vpu_registers[0];
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_ADDR_MID:
        value = mailbox->vpu_registers[1];
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_ADDR_HI:
        value = mailbox->vpu_registers[2];
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_DATA:
        value = mailbox->vpu_registers[3];
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_COMMAND:
        /* Reads observe already-published scheduler state only. */
        value = mailbox->vpu_registers[4];
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_IRQ:
        value = mailbox->vpu_irq;
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_PAGE_HI:
        value = (uint8_t)(mailbox->page >> 16);
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_PAGE_MID:
        value = (uint8_t)(mailbox->page >> 8);
        post_fiq_event(mailbox, address, value, 0);
        return value;
    case PI1MHZ_REG_PAGE_LO:
        value = (uint8_t)mailbox->page;
        post_fiq_event(mailbox, address, value, 0);
        return value;
    default:
        post_fiq_event(mailbox, address, bus_value, 0);
        return bus_value;
    }
}

uint8_t pi1mhz_mailbox_read(pi1mhz_mailbox *mailbox, uint16_t address)
{
    uint8_t value = pi1mhz_mailbox_bus_access(mailbox, address, 0xFFu, 0);

    /* Compatibility wrappers historically exposed BUSY once in zero-latency
       fixtures. The service has already completed; this does not advance the
       scheduler. Cycle-accurate integrations use bus_access directly. */
    if (mailbox && address == PI1MHZ_REG_COMMAND &&
        mailbox->legacy_busy_observation) {
        mailbox->legacy_busy_observation = 0;
        return PI1MHZ_NET_BUSY;
    }
    return value;
}

void pi1mhz_mailbox_write(pi1mhz_mailbox *mailbox, uint16_t address,
                          uint8_t value)
{
    (void)pi1mhz_mailbox_bus_access(mailbox, address, value, 1);
}

void pi1mhz_mailbox_set_mirror(pi1mhz_mailbox *mailbox, uint8_t offset,
                               const uint8_t *data, uint8_t length)
{
    if (!mailbox)
        return;
    if (!data || !length || (unsigned)offset + length > 256u ||
        length > sizeof(mailbox->mirror)) {
        mailbox->mirror_length = 0;
        return;
    }
    memcpy(mailbox->mirror, data, length);
    mailbox->mirror_offset = offset;
    mailbox->mirror_length = length;
}

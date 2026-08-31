/*
 * Acorn SCSI host adapter model for Elkulator.
 *
 * The phase and register behaviour follows the BeebEm/b-em SCSI model by
 * Jon Welch and Y. Tanaka. That implementation is GPL-2.0-or-later and is
 * itself based on the Acorn host adapter at &FC40.
 */

#include "beebscsi_elkulator.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    BUS_FREE,
    SELECTION,
    COMMAND,
    SCSI_READ,
    SCSI_WRITE,
    STATUS,
    MESSAGE
} scsi_phase;

typedef struct {
    scsi_phase phase;
    int selected;
    int message_phase;
    int command_phase;
    int input_phase;
    int busy;
    int request;
    int irq_armed;
    int irq_pending;
    unsigned char command[10];
    unsigned char buffer[256];
    unsigned char geometry[33];
    unsigned status;
    unsigned message;
    unsigned blocks;
    unsigned next_sector;
    unsigned offset;
    unsigned length;
    unsigned last_write;
    unsigned sense_code;
    unsigned sense_sector;
    FILE *image;
    unsigned long image_bytes;
    int writable;
    int initialised;
} beebscsi_state;

static beebscsi_state scsi;
static void (*irq_callback)(void);
static int published_irq;
static int debug_enabled;

static void debug_log(const char *format, ...)
{
    va_list arguments;

    if (!debug_enabled)
        return;
    fputs("BeebSCSI trace: ", stderr);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static void publish_irq(void)
{
    int current = scsi.image && scsi.irq_pending;

    if (current == published_irq)
        return;
    published_irq = current;
    if (irq_callback)
        irq_callback();
}

static void set_request(int active)
{
    active = active != 0;
    scsi.request = active;
    if (scsi.irq_armed && active)
        scsi.irq_pending = 1;
    publish_irq();
}

static void bus_free(void)
{
    scsi.selected = 0;
    scsi.message_phase = 0;
    scsi.command_phase = 0;
    scsi.input_phase = 0;
    scsi.busy = 0;
    set_request(0);
    scsi.phase = BUS_FREE;
}

static int initialise(void)
{
    const char *path;
    const char *read_only;
    const char *geometry_path;
    const char *last_separator;
    char *derived_geometry_path;
    char *extension;
    long size;
    unsigned long cylinders;
    FILE *geometry_file;
    size_t geometry_bytes;
    int geometry_close;
    int geometry_explicit;

    if (scsi.initialised)
        return scsi.image != NULL;
    scsi.initialised = 1;
    debug_enabled = getenv("PI1MHZ_BEEBSCSI_DEBUG") != NULL;
    path = getenv("PI1MHZ_BEEBSCSI_LUN");
    if (!path || !*path)
        return 0;
    read_only = getenv("PI1MHZ_BEEBSCSI_READ_ONLY");
    scsi.writable = !read_only || !*read_only || !strcmp(read_only, "0");
    scsi.image = fopen(path, scsi.writable ? "rb+" : "rb");
    if (!scsi.image && scsi.writable) {
        scsi.writable = 0;
        scsi.image = fopen(path, "rb");
    }
    if (!scsi.image) {
        fprintf(stderr, "BeebSCSI: cannot open LUN 0 %s: %s\n",
                path, strerror(errno));
        return 0;
    }
    if (fseek(scsi.image, 0, SEEK_END) ||
        (size = ftell(scsi.image)) <= 0 || fseek(scsi.image, 0, SEEK_SET)) {
        fprintf(stderr, "BeebSCSI: cannot size LUN 0 %s\n", path);
        fclose(scsi.image);
        scsi.image = NULL;
        return 0;
    }
    scsi.image_bytes = (unsigned long)size;
    memset(scsi.geometry, 0, sizeof(scsi.geometry));
    geometry_path = getenv("PI1MHZ_BEEBSCSI_DSC");
    geometry_explicit = geometry_path && *geometry_path;
    derived_geometry_path = NULL;
    if (!geometry_explicit) {
        derived_geometry_path = malloc(strlen(path) + 5u);
        if (derived_geometry_path) {
            strcpy(derived_geometry_path, path);
            last_separator = strrchr(derived_geometry_path, '/');
            extension = strrchr(derived_geometry_path, '.');
            if (!extension || (last_separator && extension < last_separator))
                extension = derived_geometry_path + strlen(derived_geometry_path);
            strcpy(extension, ".dsc");
            geometry_path = derived_geometry_path;
        }
    }
    geometry_file = geometry_path ? fopen(geometry_path, "rb") : NULL;
    if (geometry_file) {
        geometry_bytes = fread(scsi.geometry, 1, sizeof(scsi.geometry),
                               geometry_file);
        geometry_close = fclose(geometry_file);
        if (geometry_bytes < 22u || geometry_close) {
            fprintf(stderr, "BeebSCSI: cannot read geometry %s\n",
                    geometry_path);
            fclose(scsi.image);
            scsi.image = NULL;
            free(derived_geometry_path);
            return 0;
        }
    } else if (geometry_explicit) {
        fprintf(stderr, "BeebSCSI: cannot read geometry %s\n",
                geometry_path);
        fclose(scsi.image);
        scsi.image = NULL;
        return 0;
    } else {
        cylinders = 1u + ((scsi.image_bytes - 1u) /
                          (256u * 33u * 255u));
        if (cylinders > 65535u)
            cylinders = 65535u;
        scsi.geometry[13] = (unsigned char)(cylinders >> 8);
        scsi.geometry[14] = (unsigned char)cylinders;
        scsi.geometry[15] = 255;
    }
    bus_free();
    fprintf(stderr, "BeebSCSI: LUN 0 mounted at &FC40, %lu bytes, %s\n",
            scsi.image_bytes, scsi.writable ? "read/write" : "read-only");
    if (geometry_file)
        fprintf(stderr, "BeebSCSI: geometry loaded from %s\n", geometry_path);
    free(derived_geometry_path);
    return 1;
}

static unsigned sector_from_command(void)
{
    return ((scsi.command[1] & 0x1fu) << 16) |
           (scsi.command[2] << 8) | scsi.command[3];
}

static int read_sector(unsigned sector)
{
    unsigned long offset = (unsigned long)sector * 256u;
    size_t count;

    if (offset >= scsi.image_bytes || fseek(scsi.image, (long)offset, SEEK_SET))
        return 0;
    memset(scsi.buffer, 0, sizeof(scsi.buffer));
    count = fread(scsi.buffer, 1, sizeof(scsi.buffer), scsi.image);
    return count == sizeof(scsi.buffer) || (!ferror(scsi.image) && count != 0);
}

static int write_sector(unsigned sector)
{
    unsigned long offset = (unsigned long)sector * 256u;

    if (!scsi.writable || offset + 256u > scsi.image_bytes ||
        fseek(scsi.image, (long)offset, SEEK_SET))
        return 0;
    if (fwrite(scsi.buffer, 1, sizeof(scsi.buffer), scsi.image) !=
        sizeof(scsi.buffer))
        return 0;
    return fflush(scsi.image) == 0;
}

static void status_phase(int good)
{
    scsi.phase = STATUS;
    scsi.input_phase = 1;
    scsi.command_phase = 1;
    scsi.message_phase = 0;
    set_request(1);
    scsi.status = good ? 0x00u : 0x02u;
    scsi.message = 0;
}

static void read_phase(unsigned length)
{
    scsi.phase = SCSI_READ;
    scsi.input_phase = 1;
    scsi.command_phase = 0;
    scsi.message_phase = 0;
    set_request(1);
    scsi.offset = 0;
    scsi.length = length;
    scsi.status = 0;
    scsi.message = 0;
}

static void execute_command(void)
{
    unsigned sector = sector_from_command();
    unsigned allocation;

    debug_log("command %02X sector=%u length=%u", scsi.command[0], sector,
              scsi.command[4]);

    switch (scsi.command[0]) {
    case 0x00:                  /* TEST UNIT READY */
        status_phase(scsi.image != NULL);
        break;
    case 0x03:                  /* REQUEST SENSE */
        allocation = scsi.command[4] ? scsi.command[4] : 4u;
        if (allocation > sizeof(scsi.buffer))
            allocation = sizeof(scsi.buffer);
        memset(scsi.buffer, 0, allocation);
        scsi.buffer[0] = (unsigned char)scsi.sense_code;
        scsi.buffer[1] = (unsigned char)(scsi.sense_sector >> 16);
        scsi.buffer[2] = (unsigned char)(scsi.sense_sector >> 8);
        scsi.buffer[3] = (unsigned char)scsi.sense_sector;
        scsi.sense_code = 0;
        scsi.sense_sector = 0;
        scsi.blocks = 1;
        read_phase(allocation);
        break;
    case 0x08:                  /* READ(6) */
        scsi.blocks = scsi.command[4] ? scsi.command[4] : 256u;
        if (!read_sector(sector)) {
            scsi.sense_code = 0x21;
            scsi.sense_sector = sector;
            status_phase(0);
            break;
        }
        scsi.next_sector = sector + 1;
        read_phase(256);
        break;
    case 0x0a:                  /* WRITE(6) */
        scsi.blocks = scsi.command[4] ? scsi.command[4] : 256u;
        scsi.next_sector = sector;
        scsi.offset = 0;
        scsi.length = 256;
        scsi.phase = SCSI_WRITE;
        scsi.input_phase = 0;
        scsi.command_phase = 0;
        scsi.message_phase = 0;
        set_request(1);
        scsi.status = 0;
        scsi.message = 0;
        break;
    case 0x0f:                  /* TRANSLATE */
        scsi.buffer[0] = scsi.command[3];
        scsi.buffer[1] = scsi.command[2];
        scsi.buffer[2] = scsi.command[1] & 0x1f;
        scsi.buffer[3] = 0;
        scsi.blocks = 1;
        read_phase(4);
        break;
    case 0x15:                  /* MODE SELECT */
        scsi.blocks = 1;
        scsi.offset = 0;
        scsi.length = scsi.command[4];
        scsi.phase = SCSI_WRITE;
        scsi.input_phase = 0;
        scsi.command_phase = 0;
        scsi.message_phase = 0;
        set_request(1);
        break;
    case 0x1a:                  /* MODE SENSE */
        allocation = scsi.command[4] ? scsi.command[4] : 22u;
        if (allocation > sizeof(scsi.geometry))
            allocation = sizeof(scsi.geometry);
        memcpy(scsi.buffer, scsi.geometry, allocation);
        scsi.blocks = 1;
        read_phase(allocation);
        break;
    case 0x1b:                  /* START STOP */
        status_phase(1);
        break;
    case 0x2f:                  /* VERIFY(10), used as a range check */
        if ((unsigned long)sector * 256u < scsi.image_bytes)
            status_phase(1);
        else {
            scsi.sense_code = 0x21;
            scsi.sense_sector = sector;
            status_phase(0);
        }
        break;
    default:
        status_phase(0);
        break;
    }
}

static void write_data(unsigned value)
{
    scsi.last_write = value;
    switch (scsi.phase) {
    case BUS_FREE:
        if (scsi.selected) {
            scsi.busy = 1;
            scsi.phase = SELECTION;
            debug_log("bus free -> selection");
        }
        break;
    case SELECTION:
        if (!scsi.selected) {
            scsi.phase = COMMAND;
            scsi.input_phase = 0;
            scsi.command_phase = 1;
            scsi.message_phase = 0;
            scsi.offset = 0;
            scsi.length = 6;
            debug_log("selection -> command");
        }
        break;
    case COMMAND:
        if (scsi.offset < sizeof(scsi.command))
            scsi.command[scsi.offset] = (unsigned char)value;
        if (scsi.offset == 0 && value >= 0x20 && value <= 0x3f)
            scsi.length = 10;
        ++scsi.offset;
        --scsi.length;
        set_request(0);
        if (!scsi.length)
            execute_command();
        break;
    case SCSI_WRITE:
        if (scsi.offset < sizeof(scsi.buffer))
            scsi.buffer[scsi.offset] = (unsigned char)value;
        ++scsi.offset;
        --scsi.length;
        set_request(0);
        if (scsi.length)
            break;
        if (scsi.command[0] == 0x0a) {
            if (!write_sector(scsi.next_sector)) {
                scsi.sense_code = 0x21;
                scsi.sense_sector = scsi.next_sector;
                status_phase(0);
                break;
            }
            ++scsi.next_sector;
        }
        if (!--scsi.blocks) {
            status_phase(1);
            break;
        }
        scsi.offset = 0;
        scsi.length = 256;
        set_request(1);
        break;
    default:
        bus_free();
        break;
    }
}

static unsigned read_data(void)
{
    unsigned value;

    switch (scsi.phase) {
    case STATUS:
        value = scsi.status;
        debug_log("status %02X", value);
        set_request(0);
        scsi.phase = MESSAGE;
        scsi.message_phase = 1;
        set_request(1);
        return value;
    case MESSAGE:
        value = scsi.message;
        debug_log("message %02X -> bus free", value);
        set_request(0);
        bus_free();
        return value;
    case SCSI_READ:
        value = scsi.buffer[scsi.offset++];
        --scsi.length;
        set_request(0);
        if (!scsi.length) {
            if (!--scsi.blocks) {
                status_phase(1);
            } else if (read_sector(scsi.next_sector++)) {
                scsi.offset = 0;
                scsi.length = 256;
                set_request(1);
            } else {
                scsi.sense_code = 0x21;
                scsi.sense_sector = scsi.next_sector - 1;
                status_phase(0);
            }
        }
        return value;
    default:
        if (scsi.phase != BUS_FREE)
            bus_free();
        return scsi.last_write;
    }
}

int beebscsi_elkulator_enabled(void)
{
    return initialise();
}

int beebscsi_elkulator_handles(uint16_t address)
{
    return address >= 0xfc40u && address <= 0xfc44u && initialise();
}

uint8_t beebscsi_elkulator_read(uint16_t address)
{
    unsigned status;
    uint8_t value;

    if (address == 0xfc44u)
        return 0xff;
    if ((address & 3u) == 0) {
        value = (uint8_t)read_data();
        publish_irq();
        return value;
    }
    if ((address & 3u) != 1)
        return 0xff;
    /* Maintained b-em also keeps the visible REQ bit high because Acorn ADFS
       locks during filing-system entry if an idle status read returns it low.
       scsi.request still tracks the transfer phase for the IRQ latch. */
    status = 0x20;
    if (scsi.command_phase)
        status |= 0x80;
    if (scsi.input_phase)
        status |= 0x40;
    if (scsi.request)
        status |= 0x20;
    if (beebscsi_elkulator_host_irq())
        status |= 0x10;
    if (scsi.busy)
        status |= 0x02;
    if (scsi.message_phase)
        status |= 0x01;
    return (uint8_t)status;
}

void beebscsi_elkulator_write(uint16_t address, uint8_t value)
{
    if (address == 0xfc44u) {
        /* BeebSCSI 7 configuration/jukebox register. A single mounted LUN has
           no alternate jukebox to select, so command zero is a bounded no-op. */
        return;
    }
    switch (address & 3u) {
    case 0:
        scsi.selected = 1;
        write_data(value);
        break;
    case 1:                     /* BeebSCSI configuration/jukebox selector. */
        break;
    case 2:
        scsi.selected = 0;
        write_data(value);
        break;
    case 3:
        scsi.selected = 1;
        /* The BeebSCSI CPLD first stage follows D0. An asserted REQ sets the
           second stage, which remains latched until the first stage clears. */
        scsi.irq_armed = (value & 1u) != 0;
        if (!scsi.irq_armed)
            scsi.irq_pending = 0;
        else if (scsi.request)
            scsi.irq_pending = 1;
        break;
    }
    publish_irq();
}

void beebscsi_elkulator_reset(void)
{
    if (!initialise())
        return;
    scsi.sense_code = 0;
    scsi.sense_sector = 0;
    scsi.irq_armed = 0;
    scsi.irq_pending = 0;
    bus_free();
    publish_irq();
}

int beebscsi_elkulator_host_irq(void)
{
    return scsi.image && scsi.irq_pending;
}

void beebscsi_elkulator_set_irq_callback(void (*callback)(void))
{
    irq_callback = callback;
    publish_irq();
}

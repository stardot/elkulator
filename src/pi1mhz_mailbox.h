#ifndef PI1MHZ_MAILBOX_H
#define PI1MHZ_MAILBOX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PI1MHZ_JIM_SIZE       (3u << 24)
#define PI1MHZ_SERVICE_SIZE   (2u << 24)
#define PI1MHZ_REG_ADDR_LO    0xFCA6u
#define PI1MHZ_REG_ADDR_MID   0xFCA7u
#define PI1MHZ_REG_ADDR_HI    0xFCA8u
#define PI1MHZ_REG_DATA       0xFCA9u
#define PI1MHZ_REG_COMMAND    0xFCAAu
#define PI1MHZ_REG_IRQ        0xFCABu
#define PI1MHZ_REG_PAGE_HI    0xFCFDu
#define PI1MHZ_REG_PAGE_MID   0xFCFEu
#define PI1MHZ_REG_PAGE_LO    0xFCFFu
#define PI1MHZ_PAGE_BASE      0xFD00u
#define PI1MHZ_PAGE_END       0xFDFFu

#define PI1MHZ_NET_OK          0x00u
#define PI1MHZ_NET_PENDING     0x01u
#define PI1MHZ_NET_EOF         0x20u
#define PI1MHZ_NET_UNSUPPORTED 0x27u
#define PI1MHZ_NET_BUSY        0x80u

typedef uint8_t (*pi1mhz_dispatch_fn)(void *opaque, uint8_t selector,
                                      uint32_t command_pointer,
                                      uint8_t *jim, size_t jim_size);

typedef enum pi1mhz_bus_profile {
    PI1MHZ_BUS_DIRECT,
    PI1MHZ_BUS_AP5,
    PI1MHZ_BUS_AP5_EXPANDED_SNOOP,
    PI1MHZ_BUS_AP5_FULL_FRED
} pi1mhz_bus_profile;

typedef struct pi1mhz_posted_event {
    uint16_t address;
    uint8_t value;
    uint8_t write;
    uint8_t valid;
} pi1mhz_posted_event;

typedef struct pi1mhz_host_clock {
    int valid;
    int last_cycle;
} pi1mhz_host_clock;

typedef struct pi1mhz_mailbox {
    uint8_t *jim;
    size_t jim_size;
    size_t services_base;
    /* ARM-side state maintained by the deferred Pi1MHz callbacks. */
    uint32_t address;
    uint32_t page;
    /* Bytes the VideoCore bus loop can return without waiting for ARM. */
    uint8_t vpu_registers[6];
    uint8_t vpu_irq;
    uint8_t page_window[256];
    /* A set byte means the VPU bus word has had its output-enable half set by
       a modeled Pi1MHz_MemoryWrite or Pi1MHz_MemoryWritePage operation. */
    uint8_t output_enabled[0x200];
    /* VPU owns one overwriteable posted sample. FIQ captures it into a
       separate active callback before acknowledging the doorbell. */
    pi1mhz_posted_event posted;
    pi1mhz_posted_event active;
    unsigned capture_delay_ticks;
    unsigned callback_delay_ticks;
    unsigned page_callback_delay_ticks;
    unsigned read_callback_delay_ticks;
    unsigned service_delay_ticks;
    unsigned capture_ticks_remaining;
    unsigned callback_ticks_remaining;
    unsigned service_ticks_remaining;
    uint8_t result;
    uint8_t selector;
    int service_pending;
    int legacy_busy_observation;
    pi1mhz_dispatch_fn dispatch;
    void *dispatch_opaque;
    /* Trampoline mirrored into every JIM page; see the read path. */
    uint8_t  mirror[160];
    uint8_t  mirror_offset;
    uint8_t  mirror_length;
} pi1mhz_mailbox;

int pi1mhz_mailbox_init(pi1mhz_mailbox *mailbox,
                        pi1mhz_dispatch_fn dispatch, void *opaque);
void pi1mhz_mailbox_destroy(pi1mhz_mailbox *mailbox);
void pi1mhz_mailbox_set_fiq_delay(pi1mhz_mailbox *mailbox,
                                  unsigned delay_ticks);
void pi1mhz_mailbox_set_timing(pi1mhz_mailbox *mailbox,
                               unsigned capture_ticks,
                               unsigned callback_ticks,
                               unsigned service_ticks);
/* Publish a region the Pi serves at the same offset in every JIM page, so a
 * host trampoline placed there is reachable whatever the page selector holds.
 * Passing a zero or oversized length disables the mirror. */
void pi1mhz_mailbox_set_mirror(pi1mhz_mailbox *mailbox, uint8_t offset,
                               const uint8_t *data, uint8_t length);

void pi1mhz_mailbox_set_callback_timing(pi1mhz_mailbox *mailbox,
                                        unsigned simple_ticks,
                                        unsigned page_copy_ticks,
                                        unsigned unhandled_read_ticks);
/* Advance the ARM FIQ independently of a 1 MHz bus transaction. Emulator
   schedulers should call this for elapsed host CPU time. */
void pi1mhz_mailbox_tick_fiq(pi1mhz_mailbox *mailbox, unsigned ticks);
void pi1mhz_host_clock_rebase(pi1mhz_host_clock *clock,
                              int host_cycle_counter);
unsigned pi1mhz_host_clock_sync(pi1mhz_host_clock *clock,
                                int host_cycle_counter);
int pi1mhz_mailbox_read_driven(const pi1mhz_mailbox *mailbox,
                               uint16_t address);
int pi1mhz_mailbox_handles(uint16_t address);
int pi1mhz_mailbox_ap5_handles(uint16_t address);
int pi1mhz_mailbox_profile_handles(pi1mhz_bus_profile profile,
                                   uint16_t address, int write);
int pi1mhz_mailbox_profile_handles_mode(pi1mhz_bus_profile profile,
                                        uint16_t address, int write,
                                        int noe_enabled);
int pi1mhz_mailbox_profile_read_driven(const pi1mhz_mailbox *mailbox,
                                       pi1mhz_bus_profile profile,
                                       uint16_t address, int noe_enabled);
int pi1mhz_mailbox_profile_snoops(pi1mhz_bus_profile profile,
                                  uint16_t address, int write);
uint8_t pi1mhz_mailbox_bus_access(pi1mhz_mailbox *mailbox,
                                  uint16_t address, uint8_t value, int write);
uint8_t pi1mhz_mailbox_read(pi1mhz_mailbox *mailbox, uint16_t address);
void pi1mhz_mailbox_write(pi1mhz_mailbox *mailbox, uint16_t address,
                          uint8_t value);

#ifdef __cplusplus
}
#endif

#endif

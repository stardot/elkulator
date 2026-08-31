#include "ap5_tube.h"
#include "pi1mhz_mailbox.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elk.h"
#include "vrEmu6502.h"

enum {
    FLOW_SPACE = 0x40,
    FLOW_DATA = 0x80,
    FLOW_BOTH = 0xc0,
    CTRL_Q = 0x01,
    CTRL_I = 0x02,
    CTRL_J = 0x04,
    CTRL_M = 0x08,
    CTRL_V = 0x10,
    CTRL_P = 0x20,
    CTRL_T = 0x40,
    CTRL_S = 0x80,
    R1_FIFO_SIZE = 24,
    PARASITE_ROM_SIZE = 0x800
};

typedef struct {
    uint8_t parasite_to_host_r1[R1_FIFO_SIZE];
    unsigned p2h_r1_head;
    unsigned p2h_r1_tail;
    unsigned p2h_r1_count;
    uint8_t parasite_to_host[4];
    uint8_t host_to_parasite[4];
    uint8_t p2h_r3[2];
    uint8_t h2p_r3[2];
    unsigned p2h_r3_count;
    unsigned h2p_r3_count;
    uint8_t host_status[4];
    uint8_t parasite_status[4];
    uint8_t control;
    uint8_t last_host_write;
    uint8_t last_parasite_write;
} tube_ula;

static tube_ula ula;
static VrEmu6502 *parasite;
static vrEmu6502Interrupt *parasite_irq_pin;
static vrEmu6502Interrupt *parasite_nmi_pin;
static uint8_t parasite_ram[0x10000];
static uint8_t parasite_rom[PARASITE_ROM_SIZE];
static int parasite_rom_mapped;
static int enabled;
static int selected;
static int nmi_asserted;
static int half_cycle;
static pi1mhz_host_clock host_clock;

static void update_signals(void)
{
    int irq = ((ula.control & CTRL_I) && (ula.parasite_status[0] & FLOW_DATA)) ||
              ((ula.control & CTRL_J) && (ula.parasite_status[3] & FLOW_DATA));
    int nmi = (ula.control & CTRL_M) &&
              (ula.parasite_status[2] & FLOW_DATA);

    if (parasite_irq_pin)
        *parasite_irq_pin = irq ? IntRequested : IntCleared;
    if (parasite_nmi_pin && nmi && !nmi_asserted)
        *parasite_nmi_pin = IntRequested;
    nmi_asserted = nmi;
    updateulaints();
}

static void reset_ula_channels(void)
{
    memset(&ula, 0, sizeof(ula));
    ula.host_status[0] = FLOW_SPACE;
    ula.host_status[1] = FLOW_SPACE;
    ula.host_status[2] = FLOW_BOTH;
    ula.host_status[3] = FLOW_SPACE;
    ula.parasite_status[0] = FLOW_SPACE;
    ula.parasite_status[1] = 0x7f;
    ula.parasite_status[2] = 0x3f;
    ula.parasite_status[3] = 0x7f;
    nmi_asserted = 0;
    update_signals();
}

static uint8_t parasite_read(uint16_t address, bool debug_read)
{
    unsigned reg;
    uint8_t value = 0;
    (void)debug_read;

    if ((address & 0xfff8) != 0xfef8) {
        if (parasite_rom_mapped && address >= 0xf000)
            return parasite_rom[address & (PARASITE_ROM_SIZE - 1)];
        return parasite_ram[address];
    }

    reg = address & 7;
    switch (reg) {
    case 0:
        parasite_rom_mapped = 0;
        value = ula.parasite_status[0] | ula.control;
        break;
    case 1:
        value = ula.host_to_parasite[0];
        ula.parasite_status[0] &= (uint8_t)~FLOW_DATA;
        ula.host_status[0] |= FLOW_SPACE;
        break;
    case 2:
        value = ula.parasite_status[1];
        break;
    case 3:
        value = ula.host_to_parasite[1];
        ula.parasite_status[1] &= (uint8_t)~FLOW_DATA;
        ula.host_status[1] |= FLOW_SPACE;
        break;
    case 4:
        value = ula.parasite_status[2];
        break;
    case 5:
        if (ula.h2p_r3_count) {
            value = ula.h2p_r3[0];
            ula.h2p_r3[0] = ula.h2p_r3[1];
            --ula.h2p_r3_count;
            if (!ula.h2p_r3_count) {
                ula.parasite_status[2] &= (uint8_t)~FLOW_DATA;
                ula.host_status[2] |= FLOW_BOTH;
            }
        } else {
            value = ula.last_host_write;
        }
        break;
    case 6:
        value = ula.parasite_status[3];
        break;
    case 7:
        value = ula.host_to_parasite[3];
        ula.parasite_status[3] &= (uint8_t)~FLOW_DATA;
        ula.host_status[3] |= FLOW_SPACE;
        break;
    }
    update_signals();
    return value;
}

static void parasite_write(uint16_t address, uint8_t value)
{
    unsigned reg;
    if ((address & 0xfff8) != 0xfef8) {
        parasite_ram[address] = value;
        return;
    }

    reg = address & 7;
    ula.last_parasite_write = value;
    switch (reg) {
    case 1:
        if (ula.p2h_r1_count < R1_FIFO_SIZE) {
            ula.parasite_to_host_r1[ula.p2h_r1_tail] = value;
            ula.p2h_r1_tail = (ula.p2h_r1_tail + 1) % R1_FIFO_SIZE;
            ++ula.p2h_r1_count;
            ula.host_status[0] |= FLOW_DATA;
            if (ula.p2h_r1_count == R1_FIFO_SIZE)
                ula.parasite_status[0] &= (uint8_t)~FLOW_SPACE;
        }
        break;
    case 3:
        ula.parasite_to_host[1] = value;
        ula.host_status[1] |= FLOW_DATA;
        ula.parasite_status[1] &= (uint8_t)~FLOW_SPACE;
        break;
    case 5:
        if (ula.p2h_r3_count < 2) {
            ula.p2h_r3[ula.p2h_r3_count++] = value;
            if (ula.p2h_r3_count >= ((ula.control & CTRL_V) ? 2u : 1u)) {
                ula.host_status[2] |= FLOW_DATA;
                ula.parasite_status[2] &= (uint8_t)~FLOW_BOTH;
            }
        }
        break;
    case 7:
        ula.parasite_to_host[3] = value;
        ula.host_status[3] |= FLOW_DATA;
        ula.parasite_status[3] &= (uint8_t)~FLOW_SPACE;
        break;
    default:
        break;
    }
    update_signals();
}

int ap5_tube_init(const char *rom_path)
{
    FILE *rom_file;
    uint8_t image[4096];
    size_t size;

    rom_file = fopen(rom_path, "rb");
    if (!rom_file) {
        fprintf(stderr, "Unable to open Tube boot ROM: %s\n", rom_path);
        return 0;
    }
    size = fread(image, 1, sizeof(image), rom_file);
    fclose(rom_file);
    if (size != 2048 && size != 4096) {
        fprintf(stderr, "Tube boot ROM must be 2048 or 4096 bytes: %s\n", rom_path);
        return 0;
    }
    /* Common 4K Acorn dumps place the physical 2K image in the upper half. */
    memcpy(parasite_rom, image + (size - PARASITE_ROM_SIZE),
           PARASITE_ROM_SIZE);

    memset(parasite_ram, 0, sizeof(parasite_ram));
    parasite = vrEmu6502New(CPU_65C02, parasite_read, parasite_write);
    if (!parasite) return 0;
    parasite_irq_pin = vrEmu6502Int(parasite);
    parasite_nmi_pin = vrEmu6502Nmi(parasite);
    enabled = 1;
    selected = 1;
    ap5_tube_prepare_cold_boot();
    ap5_tube_reset();
    fprintf(stderr, "AP5 Tube: external 3MHz 65C02 enabled (%s)\n", rom_path);
    return 1;
}

void ap5_tube_close(void)
{
    if (parasite) vrEmu6502Destroy(parasite);
    parasite = NULL;
    parasite_irq_pin = NULL;
    parasite_nmi_pin = NULL;
    enabled = 0;
    selected = 0;
}

void ap5_tube_reset(void)
{
    if (!enabled) return;
    reset_ula_channels();
    parasite_rom_mapped = 1;
    half_cycle = 0;
    host_clock.valid = 0;
    vrEmu6502Reset(parasite);
    update_signals();
}

void ap5_tube_prepare_cold_boot(void)
{
    if (!enabled) return;
    /* The AP5 Tube-enable switch is on when an external parasite is
       explicitly configured. RH Plus 1 shadows that state in bit 5 of its
       workspace byte and preserves it over BREAK. A cold reset clears that
       workspace, so restore the configured hardware state before MOS boots. */
    ram[0x0d6d] |= 0x20;
}

void ap5_tube_run_host_cycles(int host_cycles)
{
    int ticks;
    if (!enabled || !selected || (ula.control & CTRL_P)) return;
    ticks = host_cycles + (host_cycles / 2);
    half_cycle += host_cycles & 1;
    if (half_cycle >= 2) {
        ++ticks;
        half_cycle -= 2;
    }
    while (ticks-- > 0) {
        update_signals();
        vrEmu6502Tick(parasite);
    }
}

void ap5_tube_sync_host_clock(int host_cycle_counter)
{
    unsigned elapsed;
    if (!enabled) return;
    elapsed = pi1mhz_host_clock_sync(&host_clock, host_cycle_counter);
    if (elapsed > 0) ap5_tube_run_host_cycles(elapsed);
}

void ap5_tube_rebase_host_clock(int host_cycle_counter)
{
    pi1mhz_host_clock_rebase(&host_clock, host_cycle_counter);
}

int ap5_tube_enabled(void)
{
    return enabled;
}

int ap5_tube_handles(uint16_t address)
{
    return enabled && address >= 0xfce0 && address <= 0xfcef;
}

uint8_t ap5_tube_host_read(uint16_t address)
{
    unsigned reg = address & 7;
    uint8_t value = 0;
    if (!selected) return 0xfe;
    switch (reg) {
    case 0:
        value = ula.host_status[0] | ula.control;
        break;
    case 1:
        if (ula.p2h_r1_count) {
            value = ula.parasite_to_host_r1[ula.p2h_r1_head];
            ula.p2h_r1_head = (ula.p2h_r1_head + 1) % R1_FIFO_SIZE;
            --ula.p2h_r1_count;
            if (!ula.p2h_r1_count) ula.host_status[0] &= (uint8_t)~FLOW_DATA;
            ula.parasite_status[0] |= FLOW_SPACE;
        } else {
            value = ula.last_parasite_write;
        }
        break;
    case 2:
        value = ula.host_status[1];
        break;
    case 3:
        value = ula.parasite_to_host[1];
        ula.host_status[1] &= (uint8_t)~FLOW_DATA;
        ula.parasite_status[1] |= FLOW_SPACE;
        break;
    case 4:
        value = ula.host_status[2];
        break;
    case 5:
        if (ula.p2h_r3_count) {
            value = ula.p2h_r3[0];
            ula.p2h_r3[0] = ula.p2h_r3[1];
            --ula.p2h_r3_count;
            if (!ula.p2h_r3_count) {
                ula.host_status[2] &= (uint8_t)~FLOW_DATA;
                ula.parasite_status[2] |= FLOW_BOTH;
            }
        } else {
            value = ula.last_parasite_write;
        }
        break;
    case 6:
        value = ula.host_status[3];
        break;
    case 7:
        value = ula.parasite_to_host[3];
        ula.host_status[3] &= (uint8_t)~FLOW_DATA;
        ula.parasite_status[3] |= FLOW_SPACE;
        break;
    }
    update_signals();
    return value;
}

void ap5_tube_host_write(uint16_t address, uint8_t value)
{
    unsigned reg = address & 7;
    if (reg == 6) {
        /* PiTubeDirect's write-only copro selector extends the Tube ULA at
           host register offset 6. The 3 MHz external 65C02 is copro 1. */
        selected = value == 0 || value == 1;
        if (selected) ap5_tube_reset();
        else reset_ula_channels();
        return;
    }
    if (!selected) return;
    ula.last_host_write = value;
    switch (reg) {
    case 0: {
        uint8_t previous = ula.control;
        if (value & CTRL_S) {
            if ((value & CTRL_T) && !(previous & CTRL_T)) {
                uint8_t keep = ula.control;
                reset_ula_channels();
                ula.control = keep;
            }
            ula.control |= value & 0x3f;
        } else {
            ula.control &= (uint8_t)~(value & 0x3f);
            if (!(value & CTRL_P) && (previous & CTRL_P)) {
                parasite_rom_mapped = 1;
                vrEmu6502Reset(parasite);
            }
        }
        /* Control bits live in the control latch, not in the FIFO flow
           status. Keeping the written mask here makes a cleared Q bit read
           back as set and causes the Electron MOS Tube presence test to
           reject an otherwise live ULA. */
        ula.host_status[0] &= FLOW_BOTH;
        break;
    }
    case 1:
        ula.host_to_parasite[0] = value;
        ula.parasite_status[0] |= FLOW_DATA;
        ula.host_status[0] &= (uint8_t)~FLOW_SPACE;
        break;
    case 3:
        ula.host_to_parasite[1] = value;
        ula.parasite_status[1] |= FLOW_DATA;
        ula.host_status[1] &= (uint8_t)~FLOW_SPACE;
        break;
    case 5:
        if (ula.h2p_r3_count < 2) {
            ula.h2p_r3[ula.h2p_r3_count++] = value;
            if (ula.h2p_r3_count >= ((ula.control & CTRL_V) ? 2u : 1u)) {
                ula.parasite_status[2] |= FLOW_DATA;
                ula.host_status[2] &= (uint8_t)~FLOW_SPACE;
            }
        }
        break;
    case 7:
        ula.host_to_parasite[3] = value;
        ula.parasite_status[3] |= FLOW_DATA;
        ula.host_status[3] &= (uint8_t)~FLOW_SPACE;
        break;
    default:
        break;
    }
    update_signals();
}

int ap5_tube_host_irq(void)
{
    return enabled && (ula.control & CTRL_Q) &&
           (ula.host_status[3] & FLOW_DATA);
}

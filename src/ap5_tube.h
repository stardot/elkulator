#ifndef AP5_TUBE_H
#define AP5_TUBE_H

#include <stdint.h>

int ap5_tube_init(const char *rom_path);
void ap5_tube_close(void);
void ap5_tube_reset(void);
void ap5_tube_prepare_cold_boot(void);
void ap5_tube_run_host_cycles(int host_cycles);
void ap5_tube_sync_host_clock(int host_cycle_counter);
void ap5_tube_rebase_host_clock(int host_cycle_counter);
int ap5_tube_enabled(void);
int ap5_tube_handles(uint16_t address);
uint8_t ap5_tube_host_read(uint16_t address);
void ap5_tube_host_write(uint16_t address, uint8_t value);
int ap5_tube_host_irq(void);

#endif

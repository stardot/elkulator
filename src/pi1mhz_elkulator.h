#ifndef PI1MHZ_ELKULATOR_H
#define PI1MHZ_ELKULATOR_H

#include <stdint.h>

int pi1mhz_elkulator_enabled(void);
int pi1mhz_elkulator_handles_read(uint16_t address);
int pi1mhz_elkulator_handles_write(uint16_t address);
void pi1mhz_elkulator_snoop_read(uint16_t address);
void pi1mhz_elkulator_snoop_write(uint16_t address, uint8_t value);
uint8_t pi1mhz_elkulator_read(uint16_t address);
void pi1mhz_elkulator_write(uint16_t address, uint8_t value);
void pi1mhz_elkulator_sync_host_clock(int host_cycle_counter);
void pi1mhz_elkulator_rebase_host_clock(int host_cycle_counter);
uint32_t pi1mhz_elkulator_selected_page(void);

#endif

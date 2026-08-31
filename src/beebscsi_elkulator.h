#ifndef BEEBSCSI_ELKULATOR_H
#define BEEBSCSI_ELKULATOR_H

#include <stdint.h>

int beebscsi_elkulator_enabled(void);
int beebscsi_elkulator_handles(uint16_t address);
uint8_t beebscsi_elkulator_read(uint16_t address);
void beebscsi_elkulator_write(uint16_t address, uint8_t value);
void beebscsi_elkulator_reset(void);
int beebscsi_elkulator_host_irq(void);
void beebscsi_elkulator_set_irq_callback(void (*callback)(void));

#endif

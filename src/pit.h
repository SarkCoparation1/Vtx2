#ifndef PIT_H
#define PIT_H

#include "idt.h"
#include <stdint.h>

void pit_init(uint64_t frequency);
uint64_t pit_irq_handler_c(uint64_t current_rsp);
extern void irq0_stub(void);
uint64_t pit_get_ticks(void);

#endif
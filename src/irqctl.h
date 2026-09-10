#ifndef IRQCTL_H
#define IRQCTL_H

#include <stdint.h>
#include <stdbool.h>

void irqctl_set_mode(bool using_apic);
void irqctl_eoi(uint8_t legacy_irq);

#endif
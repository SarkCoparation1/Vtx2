#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>

void ioapic_init(uint32_t ioapic_base);
void ioapic_set_entry(uint8_t redir_index, uint8_t vector, uint8_t lapic_id, uint8_t active_low, uint8_t level_triggered, uint8_t masked);

#endif
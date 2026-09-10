#ifndef MADT_H
#define MADT_H

#include <stdint.h>
#include <stdbool.h>

bool madt_init(void);
uint32_t madt_get_lapic_address(void);
uint32_t madt_get_ioapic_address(void);
uint32_t madt_get_ioapic_gsi_base(void);
uint8_t  madt_get_boot_lapic_id(void);
uint32_t madt_isa_irq_to_gsi(uint8_t isa_irq);
uint8_t  madt_isa_irq_polarity_active_low(uint8_t isa_irq);
uint8_t  madt_isa_irq_level_triggered(uint8_t isa_irq);

#endif
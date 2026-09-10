#include "ioapic.h"
#include <stddef.h>

#define IOAPIC_REGSEL_OFFSET 0x00
#define IOAPIC_REGWIN_OFFSET 0x10

#define IOAPIC_REG_REDTBL_BASE 0x10

static volatile uint32_t *g_ioregsel = NULL;
static volatile uint32_t *g_iowin = NULL;

static void ioapic_write(uint32_t reg, uint32_t value) {
    *g_ioregsel = reg;
    *g_iowin = value;
}

void ioapic_init(uint32_t ioapic_base) {
    g_ioregsel = (volatile uint32_t *)(uint64_t)(ioapic_base + IOAPIC_REGSEL_OFFSET);
    g_iowin = (volatile uint32_t *)(uint64_t)(ioapic_base + IOAPIC_REGWIN_OFFSET);
}

void ioapic_set_entry(uint8_t redir_index, uint8_t vector, uint8_t lapic_id, uint8_t active_low, uint8_t level_triggered, uint8_t masked) {
    if (!g_ioregsel) return;

    uint32_t low = vector;
    if (active_low) low |= (1 << 13);
    if (level_triggered) low |= (1 << 15);
    if (masked) low |= (1 << 16);

    uint32_t high = ((uint32_t)lapic_id) << 24;

    uint32_t reg_low  = IOAPIC_REG_REDTBL_BASE + (uint32_t)redir_index * 2;
    uint32_t reg_high = reg_low + 1;

    ioapic_write(reg_high, high);
    ioapic_write(reg_low, low);
}
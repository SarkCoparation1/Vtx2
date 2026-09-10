#include "lapic.h"
#include <stddef.h>

#define LAPIC_REG_ID 0x020
#define LAPIC_REG_EOI 0x0B0
#define LAPIC_REG_SPURIOUS 0x0F0

#define LAPIC_SPURIOUS_ENABLE (1 << 8)
#define LAPIC_SPURIOUS_VECTOR 0xFF /* IDT'de zaten default_isr her vektore atanmis durumda */

static volatile uint32_t *g_lapic = NULL;

static inline uint32_t lapic_read(uint32_t reg) {
    return g_lapic[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    g_lapic[reg / 4] = value;
}

void lapic_init(uint32_t lapic_base) {
    g_lapic = (volatile uint32_t *)(uint64_t)lapic_base;
    lapic_write(LAPIC_REG_SPURIOUS, LAPIC_SPURIOUS_VECTOR | LAPIC_SPURIOUS_ENABLE);
}

void lapic_send_eoi(void) {
    if (!g_lapic) return;
    lapic_write(LAPIC_REG_EOI, 0);
}

uint32_t lapic_get_id(void) {
    if (!g_lapic) return 0;
    return lapic_read(LAPIC_REG_ID) >> 24;
}
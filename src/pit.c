#include "pit.h"
#include "task.h"
#include "idt.h"
#include "irqctl.h"
#include <stdint.h>


#define PIT_COMMAND_PORT 0x43
#define PIT_DATA_PORT 0x40
#define PIT_BASE_FREQ 1193182
#define SCHEDULER_QUANTUM_TICKS 10

static uint64_t g_ticks = 0;

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void pit_init(uint64_t frequency) {
    if (frequency == 0) {
        return;
    }

    uint32_t divisor = PIT_BASE_FREQ / frequency;

    if (divisor == 0) {
        divisor = 1;
    }

    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;
    }

    outb(
        PIT_COMMAND_PORT,
        0x36
    );

    outb(
        PIT_DATA_PORT,
        (uint8_t)(divisor & 0xFF)
    );

    outb(
        PIT_DATA_PORT,
        (uint8_t)((divisor >> 8) & 0xFF)
    );
}

uint64_t pit_irq_handler_c(uint64_t current_rsp) {
    g_ticks++;

    uint64_t next_rsp = current_rsp;

    if ((g_ticks % SCHEDULER_QUANTUM_TICKS) == 0) {
        next_rsp = task_schedule(current_rsp);
    }

    irqctl_eoi(0);

    return next_rsp;
}

uint64_t pit_get_ticks(void)
{
    return g_ticks;
}
#include "irqctl.h"
#include "pic.h"
#include "lapic.h"

static bool g_using_apic = false;

void irqctl_set_mode(bool using_apic) {
    g_using_apic = using_apic;
}

void irqctl_eoi(uint8_t legacy_irq) {
    if (g_using_apic) {
        lapic_send_eoi();
    } else {
        pic_send_eoi(legacy_irq);
    }
}
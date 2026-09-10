#include "idt.h"
#include <stdint.h>

static idt_entry_t idt[256];
static idtr_t idtr;
static uint16_t g_kernel_cs = 0;

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
	idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].attributes = flags;
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].zero = 0;
}

__attribute__((interrupt)) void default_isr(interrupt_frame_t *frame) {
	while (1) {
		__asm__ volatile("hlt");
	}
}

void idt_init(void) {
	idtr.limit = (sizeof(idt_entry_t) * 256) - 1;
	idtr.base = (uint64_t)&idt;
	uint16_t code_selector;
	__asm__ volatile ("mov %%cs, %0" : "=r"(code_selector));
	g_kernel_cs = code_selector;
	
	for (int i = 0; i < 256; i++) {
		idt_set_gate(i, (uint64_t)default_isr, code_selector, 0x8E);
	}
	
	__asm__ volatile("lidt %0" : : "m"(idtr));
}

uint16_t idt_get_kernel_cs(void) {
	return g_kernel_cs;
}
#include "pic.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Md"(port));
	return ret;
}

static inline void io_wait() {
	outb(0x80, 0);
}

void pic_remap(void) {
	uint8_t a1, a2;
	
	a1 = inb(PIC1_DATA);
	a2 = inb(PIC2_DATA);
	
	outb(PIC1_COMMAND, 0x11);
	io_wait();
	outb(PIC2_COMMAND, 0x11);
	io_wait();
	
	/* IRQ'lar CPU exception vektorleriyle cakismamalidir. */
	outb(PIC1_DATA, 0x20);
	io_wait();
	outb(PIC2_DATA, 0x28);
	io_wait();
	
	outb(PIC1_DATA, 0x01);
	io_wait();
	outb(PIC2_DATA, 0x01);
	io_wait();
	
	outb(PIC1_DATA, a1);
	outb(PIC2_DATA, a2);
}

void pic_send_eoi(uint8_t irq) {
	if (irq >= 8) {
		outb(PIC2_COMMAND, 0x20);
	}
	
	outb(PIC1_COMMAND, 0x20);
}

void pic_disable(void) {
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}

void irq_set_mask(uint8_t irq_line) {
	uint16_t port;
	uint8_t value;
	
	if (irq_line < 8) {
		port = PIC1_DATA;
	} else {
		port = PIC2_DATA;
		irq_line -= 8;
	}
	
	value = inb(port) | (1 << irq_line);
	outb(port, value);
}

void irq_clear_mask(uint8_t irq_line) {
	uint16_t port;
	uint8_t value;
	
	if (irq_line < 8) {
		port = PIC1_DATA;
	} else {
		port = PIC2_DATA;
		irq_line -= 8;
	}
	
	value = inb(port) & ~(1 << irq_line);
	outb(port, value);
}

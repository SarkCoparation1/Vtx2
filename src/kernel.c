#include "kernel.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "task.h"
#include "acpi.h"
#include "madt.h"
#include "lapic.h"
#include "ioapic.h"
#include "irqctl.h"
#include "pmm.h"
#include "kheap.h"
#include "gdt.h"
#include "tss.h"
#include "paging.h"
#include "syscall.h"
#include "usyscall.h"
#include "exceptions.h"
#include "console.h"
#include "xhci.h"
#include "mouse.h"
#include "keyboard.h"
#include "ui.h"
#include "desktop.h"
#include "maxbash.h"
#include "power.h"
#include "font.h"
#include "io.h"
#include "vtx2fs_state.h"
#include "editor.h"
#include "backbuffer.h"
#include <stdbool.h>

void put_pixel(BootInfo *boot_info, uint32_t x, uint32_t y, uint32_t color);
void fill_screen(BootInfo *boot_info, uint32_t color);
void draw_rect(BootInfo *boot_info, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(BootInfo *boot_info, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(BootInfo *boot_info, const char *str, uint32_t x, uint32_t y, uint32_t color);
void draw_int(BootInfo *boot_info, int32_t num, uint32_t x, uint32_t y, uint32_t color);
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static void demo_ring3_task(void) {
    usys_write("Merhaba Ring3'ten! (int 0x80 calisti)", 0);
    usys_exit();
    for (;;) {}
}

__attribute__((section(".text.head")))
void kernel_main(BootInfo *boot_info)
{
    outb(0xE9, '1');
    for (uint8_t *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
    outb(0xE9, '2');

    pmm_init(boot_info->memory_map_addr, boot_info->memory_map_size, boot_info->memory_map_descriptor_size);
    outb(0xE9, '3');
    kheap_init();

    paging_init();
    outb(0xE9, '4');

    tss_init();
    gdt_init();
    outb(0xE9, '5');

    idt_init();
    task_init();
    outb(0xE9, '6');

    exceptions_init();

    idt_set_gate(0x20, (uint64_t)irq0_stub, idt_get_kernel_cs(), 0x8E);
    idt_set_gate(0x21, (uint64_t)keyboard_handler, idt_get_kernel_cs(), 0x8E);
    idt_set_gate(0x2C, (uint64_t)mouse_handler, idt_get_kernel_cs(), 0x8E);

    idt_set_gate(0x80, (uint64_t)syscall_stub, idt_get_kernel_cs(), 0xEE);
    syscall_init();

    acpi_init(boot_info->rsdp);

    bool have_apic = madt_init();

    if (have_apic) {
        pic_disable();

        lapic_init(madt_get_lapic_address());
        ioapic_init(madt_get_ioapic_address());
        irqctl_set_mode(true);

        uint8_t boot_lapic_id = madt_get_boot_lapic_id();
        uint32_t gsi_base = madt_get_ioapic_gsi_base();

        uint32_t gsi0 = madt_isa_irq_to_gsi(0);
        ioapic_set_entry((uint8_t)(gsi0 - gsi_base), 0x20, boot_lapic_id, madt_isa_irq_polarity_active_low(0), madt_isa_irq_level_triggered(0), 0);

        uint32_t gsi1 = madt_isa_irq_to_gsi(1);
        ioapic_set_entry((uint8_t)(gsi1 - gsi_base), 0x21, boot_lapic_id, madt_isa_irq_polarity_active_low(1), madt_isa_irq_level_triggered(1), 0);

        uint32_t gsi12 = madt_isa_irq_to_gsi(12);
        ioapic_set_entry((uint8_t)(gsi12 - gsi_base), 0x2C, boot_lapic_id, madt_isa_irq_polarity_active_low(12), madt_isa_irq_level_triggered(12), 0);
    } else {
        irqctl_set_mode(false);
        pic_remap();
        irq_clear_mask(0);
        irq_clear_mask(1);
        irq_clear_mask(2);
        irq_clear_mask(12);
    }

    pit_init(1000);
    mouse_init(boot_info);
    keyboard_init();
    ui_init(boot_info);
    power_init();
    desktop_init();
    outb(0xE9, '7');

    outb(0xE9, 'a');

    bool double_buffered = backbuffer_init(boot_info);
    outb(0xE9, 'c');

    BootInfo *draw_info = double_buffered ? backbuffer_get() : boot_info;
    console_set_target(draw_info);
    outb(0xE9, 'd');

    task_create_user(demo_ring3_task);
    __asm__ volatile ("sti");
    outb(0xE9, 'b');


    desktop_draw(draw_info);
    outb(0xE9, 'e');
    vtx2fs_state_init();
    outb(0xE9, 'f');
    draw_string(draw_info, vtx2fs_state_get_status_message(), 5, 5, vtx2fs_state_is_mounted() ? COLOR_GREEN : COLOR_RED );
    outb(0xE9, 'g');

    bool xhci_ok = xhci_init();
    outb(0xE9, '8');
    if (xhci_ok) {
        draw_string(draw_info, "xHCI: hazir", 5, 20, COLOR_GREEN);
    } else {
        char xhci_msg[96];
        char *xp = xhci_msg;
        const char *prefix = "xHCI basarisiz: ";
        while (*prefix) { *xp++ = *prefix++; }
        const char *reason = xhci_get_last_error();
        while (*reason && (xp - xhci_msg) < 94) { *xp++ = *reason++; }
        *xp = '\0';
        draw_string(draw_info, xhci_msg, 5, 20, COLOR_RED);
    }
    mouse_draw_cursor(draw_info);


    if (double_buffered) {
        backbuffer_present(boot_info);
    }


    while (true) {
        xhci_poll_ports();

        mouse_update_result_t mres = mouse_update(draw_info);

        if (double_buffered) {

            if (mres == MOUSE_UPDATE_FULL_REDRAW) {

                backbuffer_present(
                    boot_info
                );

            } else if (mres == MOUSE_UPDATE_CURSOR_ONLY) {

                int32_t rx;
                int32_t ry;

                uint32_t rw;
                uint32_t rh;


                mouse_get_dirty_rect(
                    &rx,
                    &ry,
                    &rw,
                    &rh
                );


                backbuffer_present_rect(
                    boot_info,
                    rx,
                    ry,
                    rw,
                    rh
                );
            }
        }

        char key;
        while (keyboard_get_char(&key)) {

            bool changed = false;


            changed |= maxbash_app_handle_key(key);
            changed |= editor_app_handle_key(key);


            if (changed) {

                mouse_erase_cursor(
                    draw_info
                );


                desktop_draw(
                    draw_info
                );


                mouse_draw_cursor(
                    draw_info
                );


                if (double_buffered) {

                    backbuffer_present(
                        boot_info
                    );
                }
            }
        }
        __asm__ volatile ("hlt");
    }
}

void put_pixel(BootInfo *boot_info, uint32_t x, uint32_t y, uint32_t color) {
	if(x >= boot_info->width || y >= boot_info->height) {
		return;
	}
	
	uint64_t offset = ((uint64_t)y * boot_info->ppsl + x) * boot_info->bytes_per_pixel;

	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;

	if (boot_info->bytes_per_pixel == 4) {
		uint32_t packed = boot_info->is_bgr ? ((uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16)) : ((uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16));
		*(uint32_t*)(boot_info->framebuffer + offset) = packed;
	} else if (boot_info->bytes_per_pixel == 3) {
		if (boot_info->is_bgr) {
			boot_info->framebuffer[offset + 0] = b;
			boot_info->framebuffer[offset + 1] = g;
			boot_info->framebuffer[offset + 2] = r;
		} else {
			boot_info->framebuffer[offset + 0] = r;
			boot_info->framebuffer[offset + 1] = g;
			boot_info->framebuffer[offset + 2] = b;
		}
	}
}

void fill_screen(BootInfo *boot_info, uint32_t color) {
	uint64_t pixels = (uint64_t)boot_info->ppsl * boot_info->height;

	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;

	if (boot_info->bytes_per_pixel == 4) {
		uint32_t packed = boot_info->is_bgr ? ((uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16)) : ((uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16));
		uint32_t *fb = (uint32_t *)boot_info->framebuffer;
		for (uint64_t i = 0; i < pixels; i++) fb[i] = packed;
	} else {
		uint8_t c0 = boot_info->is_bgr ? b : r;
		uint8_t c2 = boot_info->is_bgr ? r : b;
		for (uint64_t i = 0; i < pixels; i++) {
			uint64_t offset = i * 3;
			boot_info->framebuffer[offset] = c0;
			boot_info->framebuffer[offset + 1] = g;
			boot_info->framebuffer[offset + 2] = c2;
		}
	}
}

void draw_rect(BootInfo *boot_info, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
	for (uint32_t i = 0; i < h; i++) {
		for (uint32_t j = 0; j < w; j++) {
			put_pixel(boot_info, x + j, y + i, color);
		}
	}
}

void draw_char(BootInfo *boot_info, char c, uint32_t x, uint32_t y, uint32_t color) {
	unsigned char uc = (unsigned char)c;
	if (uc >= 128) return;
	
	for (int row = 0; row < 8; row++) {
		uint8_t font_row = font8x8_basic[uc][row];
		for (int col = 0; col < 8; col++) {
			if ((font_row >> (7 - col)) & 1) {
				put_pixel(boot_info, x + col, y + row, color);
			}
		}
	}
}

void draw_string(BootInfo *boot_info, const char str[], uint32_t x, uint32_t y, uint32_t color) {
    uint32_t cur_x = x;
    uint32_t cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        
        if (str[i] == '\n') {
            cur_x = x;
            cur_y += 10;
            continue;
        }

        draw_char(boot_info, str[i], cur_x, cur_y, color);
        
        cur_x += 8;
    }
}

void draw_int(BootInfo *boot_info, int32_t num, uint32_t x, uint32_t y, uint32_t color) {
	char buf[16];
	int i = 0;
	bool is_neg = false;
	
	if (num == 0) {
		draw_string(boot_info, "0", x, y, color);
		return;
	}
	if (num < 0) {
		is_neg = true;
		num = -num;
	}
	while (num > 0) {
		buf[i++] = '0' + (num % 10);
		num /= 10;
	}
	if (is_neg) buf[i++] = '-';
	buf[i] = '\0';
	
	for (int j = 0; j < i / 2; j++) {
		char temp = buf[i];
		buf[j] = buf[i - 1 - j];
		buf[i - 1 - j] = temp;
	}
	draw_string(boot_info, buf, x, y, color);
}
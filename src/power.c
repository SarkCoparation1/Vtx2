#include "power.h"
#include "acpi.h"
#include "font.h"

void power_init(void) {

}

void power_shutdown(BootInfo *boot_info) {
    fill_screen(boot_info, 0x00000000);
    draw_string(boot_info, "VTX2 OS Kapatiliyor...", boot_info->width / 2 - 90, boot_info->height / 2, COLOR_WHITE);

    acpi_shutdown();

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void power_reboot(BootInfo *boot_info) {
    fill_screen(boot_info, 0x00000000);
    draw_string(boot_info, "Sistem Yeniden Baslatiliyor...", boot_info->width / 2 - 120, boot_info->height / 2, COLOR_YELLOW);

    acpi_reboot();

    while (1) {
        __asm__ volatile ("hlt");
    }
}
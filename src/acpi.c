#include "acpi.h"
#include "font.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static RSDPDescriptor20 *g_rsdp = NULL;
static ACPISDTHeader *g_rsdt_xsdt = NULL;
static FADT *g_fadt = NULL;

static uint16_t g_slp_typa = 0;
static uint16_t g_slp_typb = 0;
static bool g_acpi_initialized = false;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static bool acpi_validate_checksum(void *address, size_t length) {
    uint8_t sum = 0;
    uint8_t *ptr = (uint8_t *)address;
    for (size_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    return (sum == 0);
}

ACPISDTHeader *acpi_find_table(const char *signature) {
    if (!g_rsdt_xsdt) return NULL;

    bool is_xsdt = (g_rsdp->first_part.revision >= 2 && g_rsdp->xsdt_address != 0);
    uint32_t entries = (g_rsdt_xsdt->length - sizeof(ACPISDTHeader)) / (is_xsdt ? 8 : 4);

    for (uint32_t i = 0; i < entries; i++) {
        ACPISDTHeader *header = NULL;
        if (is_xsdt) {
            uint64_t *array = (uint64_t *)((uint64_t)g_rsdt_xsdt + sizeof(ACPISDTHeader));
            header = (ACPISDTHeader *)array[i];
        } else {
            uint32_t *array = (uint32_t *)((uint64_t)g_rsdt_xsdt + sizeof(ACPISDTHeader));
            header = (ACPISDTHeader *)(uint64_t)array[i];
        }

        if (header && header->signature[0] == signature[0] &&
            header->signature[1] == signature[1] &&
            header->signature[2] == signature[2] &&
            header->signature[3] == signature[3]) {
            
            if (acpi_validate_checksum(header, header->length)) {
                return header;
            }
        }
    }
    return NULL;
}

static bool acpi_parse_s5(uint8_t *dsdt_ptr, uint32_t length) {
    for (uint32_t i = 0; i < length - 8; i++) {
        if (dsdt_ptr[i] == '_' && dsdt_ptr[i+1] == 'S' && dsdt_ptr[i+2] == '5' && dsdt_ptr[i+3] == '_') {
            if (dsdt_ptr[i-1] == 0x12 || dsdt_ptr[i+4] == 0x12) {
                uint8_t *pkg = (dsdt_ptr[i-1] == 0x12) ? &dsdt_ptr[i-1] : &dsdt_ptr[i+4];
                pkg += 2;
                pkg++;

                if (*pkg == 0x0A) pkg++;
                g_slp_typa = (*pkg) << 10;
                pkg++;

                if (*pkg == 0x0A) pkg++;
                g_slp_typb = (*pkg) << 10;

                return true;
            }
        }
    }
    return false;
}

bool acpi_enable(void) {
    if (!g_fadt) return false;

    if ((inw((uint16_t)g_fadt->pm1a_control_block) & 1) == 1) {
        return true;
    }

    if (g_fadt->smi_command_port && g_fadt->acpi_enable) {
        outb((uint16_t)g_fadt->smi_command_port, g_fadt->acpi_enable);

        for (int i = 0; i < 300; i++) {
            if ((inw((uint16_t)g_fadt->pm1a_control_block) & 1) == 1) {
                return true;
            }

            for (volatile int j = 0; j < 100000; j++);
        }
    }
    return false;
}

void acpi_init(uint64_t rsdp_address) {
    if (!rsdp_address) return;

    g_rsdp = (RSDPDescriptor20 *)rsdp_address;

    if (g_rsdp->first_part.signature[0] != 'R' || g_rsdp->first_part.signature[1] != 'S' ||
        g_rsdp->first_part.signature[2] != 'D' || g_rsdp->first_part.signature[3] != ' ') {
        return;
    }

    if (g_rsdp->first_part.revision >= 2 && g_rsdp->xsdt_address != 0) {
        g_rsdt_xsdt = (ACPISDTHeader *)g_rsdp->xsdt_address;
    } else {
        g_rsdt_xsdt = (ACPISDTHeader *)(uint64_t)g_rsdp->first_part.rsdt_address;
    }

    if (!g_rsdt_xsdt || !acpi_validate_checksum(g_rsdt_xsdt, g_rsdt_xsdt->length)) {
        return;
    }

    g_fadt = (FADT *)acpi_find_table("FACP");
    if (!g_fadt) return;

    ACPISDTHeader *dsdt_header = NULL;
    if (g_fadt->header.revision >= 2 && g_fadt->x_dsdt != 0) {
        dsdt_header = (ACPISDTHeader *)g_fadt->x_dsdt;
    } else {
        dsdt_header = (ACPISDTHeader *)(uint64_t)g_fadt->dsdt;
    }

    if (dsdt_header && acpi_validate_checksum(dsdt_header, dsdt_header->length)) {
        acpi_parse_s5((uint8_t *)dsdt_header, dsdt_header->length);
    } else {
        g_slp_typa = (0x5 << 10);
        g_slp_typb = (0x5 << 10);
    }

    g_acpi_initialized = true;
}

void acpi_shutdown(void) {
    if (g_acpi_initialized) {
        acpi_enable();

        uint16_t slp_en = (1 << 13);

        if (g_fadt->pm1a_control_block) {
            outw((uint16_t)g_fadt->pm1a_control_block, g_slp_typa | slp_en);
        }

        if (g_fadt->pm1b_control_block) {
            outw((uint16_t)g_fadt->pm1b_control_block, g_slp_typb | slp_en);
        }
    }

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void acpi_reboot(void) {
    if (g_acpi_initialized && g_fadt && (g_fadt->flags & (1 << 10))) {
        if (g_fadt->reset_reg.address_space == 1) {
            outb((uint16_t)g_fadt->reset_reg.address, g_fadt->reset_value);
        }
    }

    uint8_t temp = 0x02;
    while (temp & 0x02) {
        temp = inb(0x64);
    }
    outb(0x64, 0xFE);

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idtr = { 0, 0 };

    __asm__ volatile ("lidt %0" : : "m"(null_idtr));
    __asm__ volatile ("int3");

    while (1) {
        __asm__ volatile ("hlt");
    }
}
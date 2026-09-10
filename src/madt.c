#include "madt.h"
#include "acpi.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    ACPISDTHeader header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed)) MADTHeader;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) MADTEntryHeader;

typedef struct {
    MADTEntryHeader header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) MADTLocalApic;

typedef struct {
    MADTEntryHeader header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} __attribute__((packed)) MADTIOApic;

typedef struct {
    MADTEntryHeader header;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed)) MADTInterruptSourceOverride;

#define MADT_TYPE_LOCAL_APIC 0
#define MADT_TYPE_IOAPIC 1
#define MADT_TYPE_ISO 2

typedef struct {
    uint32_t gsi;
    uint8_t active_low;
    uint8_t level_triggered;
} isa_map_entry_t;

static uint32_t g_lapic_address = 0;
static uint32_t g_ioapic_address = 0;
static uint32_t g_ioapic_gsi_base = 0;
static uint8_t g_boot_lapic_id = 0;
static bool g_have_boot_lapic_id = false;
static bool g_have_ioapic = false;

static isa_map_entry_t g_isa_map[16];

bool madt_init(void) {
    for (int i = 0; i < 16; i++) {
        g_isa_map[i].gsi = (uint32_t)i;
        g_isa_map[i].active_low = 0;
        g_isa_map[i].level_triggered = 0;
    }

    ACPISDTHeader *table = acpi_find_table("APIC");
    if (!table) {
        return false;
    }

    MADTHeader *madt = (MADTHeader *)table;
    g_lapic_address = madt->local_apic_address;

    uint8_t *ptr = (uint8_t *)madt + sizeof(MADTHeader);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (ptr + sizeof(MADTEntryHeader) <= end) {
        MADTEntryHeader *eh = (MADTEntryHeader *)ptr;
        if (eh->length == 0) {
            break;
        }

        switch (eh->type) {
            case MADT_TYPE_LOCAL_APIC: {
                MADTLocalApic *la = (MADTLocalApic *)ptr;
                if ((la->flags & 1) && !g_have_boot_lapic_id) {
                    /* İlk "enabled" processor'ı boot CPU olarak alıyoruz.
                     * SMP'ye geçince tüm ID'leri bir listede tutmamız
                     * gerekecek - o zaman burası genişletilecek. */
                    g_boot_lapic_id = la->apic_id;
                    g_have_boot_lapic_id = true;
                }
                break;
            }
            case MADT_TYPE_IOAPIC: {
                MADTIOApic *io = (MADTIOApic *)ptr;
                if (!g_have_ioapic) {
                    g_ioapic_address = io->ioapic_address;
                    g_ioapic_gsi_base = io->gsi_base;
                    g_have_ioapic = true;
                }
                break;
            }
            case MADT_TYPE_ISO: {
                MADTInterruptSourceOverride *iso = (MADTInterruptSourceOverride *)ptr;
                if (iso->irq_source < 16) {
                    uint16_t polarity = iso->flags & 0x3;
                    uint16_t trigger  = (iso->flags >> 2) & 0x3;
                    g_isa_map[iso->irq_source].gsi = iso->gsi;
                    g_isa_map[iso->irq_source].active_low = (polarity == 3);
                    g_isa_map[iso->irq_source].level_triggered = (trigger == 3);
                }
                break;
            }
            default:
                break;
        }

        ptr += eh->length;
    }

    return g_have_ioapic && g_lapic_address != 0;
}

uint32_t madt_get_lapic_address(void)   { return g_lapic_address; }
uint32_t madt_get_ioapic_address(void)  { return g_ioapic_address; }
uint32_t madt_get_ioapic_gsi_base(void) { return g_ioapic_gsi_base; }
uint8_t  madt_get_boot_lapic_id(void)   { return g_boot_lapic_id; }

uint32_t madt_isa_irq_to_gsi(uint8_t isa_irq) {
    if (isa_irq >= 16) return isa_irq;
    return g_isa_map[isa_irq].gsi;
}

uint8_t madt_isa_irq_polarity_active_low(uint8_t isa_irq) {
    if (isa_irq >= 16) return 0;
    return g_isa_map[isa_irq].active_low;
}

uint8_t madt_isa_irq_level_triggered(uint8_t isa_irq) {
    if (isa_irq >= 16) return 0;
    return g_isa_map[isa_irq].level_triggered;
}
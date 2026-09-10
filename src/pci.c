#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_make_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)(device & 0x1F) << 11)
         | ((uint32_t)(function & 0x07) << 8)
         | ((uint32_t)(offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read32(bus, device, function, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    return (uint16_t)((dword >> shift) & 0xFFFF);
}

static uint8_t pci_header_type(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t dword = pci_config_read32(bus, device, function, 0x0C);
    return (uint8_t)((dword >> 16) & 0xFF);
}

bool pci_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t *out) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t device = 0; device < 32; device++) {
            uint8_t max_function = 1; /* varsayilan: tek fonksiyon */
            for (uint32_t function = 0; function < max_function; function++) {
                uint16_t vendor_id = pci_config_read16((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x00);
                if (vendor_id == 0xFFFF) {
                    if (function == 0) break; /* cihaz hic yok, kalan fonksiyonlari deneme */
                    continue;
                }

                if (function == 0) {
                    uint8_t htype = pci_header_type((uint8_t)bus, (uint8_t)device, (uint8_t)function);
                    if (htype & 0x80) max_function = 8; /* cok-fonksiyonlu cihaz */
                }

                uint32_t class_dword = pci_config_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x08);
                uint8_t rev        = (uint8_t)(class_dword & 0xFF);
                uint8_t this_progif = (uint8_t)((class_dword >> 8) & 0xFF);
                uint8_t this_subcls  = (uint8_t)((class_dword >> 16) & 0xFF);
                uint8_t this_class   = (uint8_t)((class_dword >> 24) & 0xFF);

                if (this_class == class_code && this_subcls == subclass && this_progif == prog_if) {
                    uint16_t device_id = pci_config_read16((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x02);
                    out->bus = (uint8_t)bus;
                    out->device = (uint8_t)device;
                    out->function = (uint8_t)function;
                    out->vendor_id = vendor_id;
                    out->device_id = device_id;
                    out->class_code = this_class;
                    out->subclass = this_subcls;
                    out->prog_if = this_progif;
                    out->revision_id = rev;
                    out->header_type = pci_header_type((uint8_t)bus, (uint8_t)device, (uint8_t)function);
                    return true;
                }
            }
        }
    }
    return false;
}

uint64_t pci_get_bar_address(const pci_device_t *dev, uint8_t bar_index) {
    if (bar_index > 5) return 0;

    uint8_t offset = (uint8_t)(0x10 + bar_index * 4);
    uint32_t bar = pci_config_read32(dev->bus, dev->device, dev->function, offset);

    if (bar & 0x1) {
        return 0;
    }

    uint8_t bar_type = (uint8_t)((bar >> 1) & 0x3);

    if (bar_type == 0x2) {
        if (bar_index >= 5) return 0;
        uint32_t bar_high = pci_config_read32(dev->bus, dev->device, dev->function, (uint8_t)(offset + 4));
        uint64_t addr = ((uint64_t)bar_high << 32) | (uint64_t)(bar & 0xFFFFFFF0u);
        return addr;
    }

    return (uint64_t)(bar & 0xFFFFFFF0u);
}

void pci_enable_bus_mastering(const pci_device_t *dev) {
    uint32_t command_status = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
    uint16_t command = (uint16_t)(command_status & 0xFFFF);
    command |= (1 << 1);
    command |= (1 << 2);
    uint32_t new_dword = (command_status & 0xFFFF0000u) | command;
    pci_config_write32(dev->bus, dev->device, dev->function, 0x04, new_dword);
}
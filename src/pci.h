#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  bus, device, function;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if, revision_id;
    uint8_t  header_type;
} pci_device_t;

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

bool pci_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t *out);

uint64_t pci_get_bar_address(const pci_device_t *dev, uint8_t bar_index);

/* Bellek uzayi erisimini (Memory Space) ve Bus Master DMA'yi acar. */
void pci_enable_bus_mastering(const pci_device_t *dev);

#endif /* PCI_H */
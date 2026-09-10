#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include <stdbool.h>

bool xhci_init(void);
bool xhci_is_ready(void);
uint8_t xhci_get_port_count(void);
const char *xhci_get_last_error(void);
uint32_t xhci_read_portsc(uint8_t port_index);
void xhci_write_portsc(uint8_t port_index, uint32_t value);
void xhci_poll_ports(void);
uint8_t xhci_get_port_slot(uint8_t port_index);
bool xhci_get_device_descriptor(uint8_t port_index, const uint8_t **out_descriptor);
const char *xhci_get_port_error(uint8_t port_index);

#endif

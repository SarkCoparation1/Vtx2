#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

void lapic_init(uint32_t lapic_base);
void lapic_send_eoi(void);
uint32_t lapic_get_id(void);

#endif
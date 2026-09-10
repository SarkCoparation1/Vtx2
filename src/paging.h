#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stdbool.h>

void paging_init(void);
bool paging_identity_map_region(uint64_t phys_addr, uint64_t size);

#endif
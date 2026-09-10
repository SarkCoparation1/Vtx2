#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>

#define PMM_PAGE_SIZE 4096

typedef struct {
    uint32_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_mem_desc_t;

#define EFI_CONVENTIONAL_MEMORY 7

void pmm_init(uint64_t memory_map_addr, uint64_t memory_map_size, uint64_t memory_map_descriptor_size);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys_addr);
uint64_t pmm_alloc_contiguous(uint64_t count);
void pmm_free_contiguous(uint64_t phys_addr, uint64_t count);
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);

#endif
#include "paging.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT 0x1ULL
#define PAGE_RW 0x2ULL
#define PAGE_USER 0x4ULL
#define PAGE_PS 0x80ULL

#define ENTRIES_PER_TABLE 512
#define IDENTITY_MAP_GIB 4
#define GIB (1024ULL * 1024 * 1024)
#define MB2 (2ULL * 1024 * 1024)
#define PAGE_SIZE_4K 4096ULL

static uint64_t g_pml4_phys = 0;

static void zero_table(uint64_t *table) {
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        table[i] = 0;
    }
}

static uint64_t *table_child_or_create(uint64_t *table, uint32_t index) {
    if (!(table[index] & PAGE_PRESENT)) {
        uint64_t new_phys = pmm_alloc_page();
        if (!new_phys) return NULL;

        uint64_t *new_table = (uint64_t *)new_phys;
        zero_table(new_table);

        table[index] = new_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    return (uint64_t *)(table[index] & ~0xFFFULL);
}

void paging_init(void) {
    uint64_t pml4_phys = pmm_alloc_page();
    uint64_t pdpt_phys = pmm_alloc_page();

    uint64_t *pml4 = (uint64_t *)pml4_phys;
    uint64_t *pdpt = (uint64_t *)pdpt_phys;

    zero_table(pml4);
    zero_table(pdpt);

    pml4[0] = pdpt_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    for (int g = 0; g < IDENTITY_MAP_GIB; g++) {
        uint64_t pd_phys = pmm_alloc_page();
        uint64_t *pd = (uint64_t *)pd_phys;

        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            uint64_t phys_addr = ((uint64_t)g * GIB) + ((uint64_t)i * MB2);
            pd[i] = phys_addr | PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_PS;
        }

        pdpt[g] = pd_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    g_pml4_phys = pml4_phys;

    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

bool paging_identity_map_region(uint64_t phys_addr, uint64_t size) {
    if (g_pml4_phys == 0 || size == 0) {
        return false;
    }

    uint64_t start = phys_addr & ~(PAGE_SIZE_4K - 1);
    uint64_t end = (phys_addr + size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);

    uint64_t *pml4 = (uint64_t *)g_pml4_phys;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE_4K) {
        if (addr < (uint64_t)IDENTITY_MAP_GIB * GIB) {
            continue;
        }

        uint32_t pml4_idx = (uint32_t)((addr >> 39) & 0x1FFu);
        uint32_t pdpt_idx = (uint32_t)((addr >> 30) & 0x1FFu);
        uint32_t pd_idx   = (uint32_t)((addr >> 21) & 0x1FFu);
        uint32_t pt_idx   = (uint32_t)((addr >> 12) & 0x1FFu);

        uint64_t *pdpt = table_child_or_create(pml4, pml4_idx);
        if (!pdpt) return false;

        uint64_t *pd = table_child_or_create(pdpt, pdpt_idx);
        if (!pd) return false;

        if (pd[pd_idx] & PAGE_PS) {
            continue; /* bu 2MB blok zaten (baska bir cagriyla) huge page olarak mapli */
        }

        uint64_t *pt = table_child_or_create(pd, pd_idx);
        if (!pt) return false;

        if (!(pt[pt_idx] & PAGE_PRESENT)) {
            pt[pt_idx] = addr | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        }
    }

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");

    return true;
}
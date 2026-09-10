#include "pmm.h"

#define PMM_MAX_TRACKED_PAGES (4ULL * 1024 * 1024 * 1024 / PMM_PAGE_SIZE)
#define PMM_BITMAP_BYTES (PMM_MAX_TRACKED_PAGES / 8)

static uint8_t g_bitmap[PMM_BITMAP_BYTES];
static uint64_t g_total_pages = 0;
static uint64_t g_free_pages = 0;
static uint64_t g_alloc_cursor = 0;

static void pmm_memset(void *dst, uint8_t val, uint64_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (uint64_t i = 0; i < n; i++) {
        p[i] = val;
    }
}

static inline bool bit_test(uint64_t page_idx) {
    return (g_bitmap[page_idx / 8] & (1u << (page_idx % 8))) != 0;
}

static inline void bit_set(uint64_t page_idx) {
    g_bitmap[page_idx / 8] |= (uint8_t)(1u << (page_idx % 8));
}

static inline void bit_clear(uint64_t page_idx) {
    g_bitmap[page_idx / 8] &= (uint8_t)~(1u << (page_idx % 8));
}

void pmm_init(uint64_t memory_map_addr, uint64_t memory_map_size, uint64_t memory_map_descriptor_size) {
    pmm_memset(g_bitmap, 0xFF, PMM_BITMAP_BYTES);
    g_total_pages = 0;
    g_free_pages = 0;

    if (memory_map_descriptor_size == 0) {
        return; /* boot.c bir sekilde memory map veremedi - PMM tamamen bos/rezerve kalir */
    }

    uint64_t entry_count = memory_map_size / memory_map_descriptor_size;
    uint8_t *cursor = (uint8_t *)memory_map_addr;

    for (uint64_t i = 0; i < entry_count; i++) {
        efi_mem_desc_t *desc = (efi_mem_desc_t *)cursor;

        if (desc->type == EFI_CONVENTIONAL_MEMORY) {
            uint64_t start_page = desc->physical_start / PMM_PAGE_SIZE;
            uint64_t page_count = desc->number_of_pages;

            for (uint64_t p = 0; p < page_count; p++) {
                uint64_t page_idx = start_page + p;
                if (page_idx == 0) continue;
                if (page_idx >= PMM_MAX_TRACKED_PAGES) break;

                bit_clear(page_idx);
                g_free_pages++;
            }

            uint64_t end_page = start_page + page_count;
            if (end_page > g_total_pages && end_page <= PMM_MAX_TRACKED_PAGES) {
                g_total_pages = end_page;
            }
        }

        cursor += memory_map_descriptor_size;
    }
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t i = 0; i < PMM_MAX_TRACKED_PAGES; i++) {
        uint64_t idx = (g_alloc_cursor + i) % PMM_MAX_TRACKED_PAGES;
        if (!bit_test(idx)) {
            bit_set(idx);
            g_free_pages--;
            g_alloc_cursor = idx + 1;
            return idx * PMM_PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_page(uint64_t phys_addr) {
    uint64_t idx = phys_addr / PMM_PAGE_SIZE;
    if (idx == 0 || idx >= PMM_MAX_TRACKED_PAGES) return;
    if (!bit_test(idx)) return;

    bit_clear(idx);
    g_free_pages++;
}

uint64_t pmm_alloc_contiguous(uint64_t count) {
    if (count == 0) return 0;

    uint64_t run_start = 0;
    uint64_t run_len = 0;

    for (uint64_t idx = 0; idx < PMM_MAX_TRACKED_PAGES; idx++) {
        if (!bit_test(idx)) {
            if (run_len == 0) run_start = idx;
            run_len++;

            if (run_len == count) {
                for (uint64_t p = 0; p < count; p++) {
                    bit_set(run_start + p);
                }
                g_free_pages -= count;
                return run_start * PMM_PAGE_SIZE;
            }
        } else {
            run_len = 0;
        }
    }

    return 0;
}

void pmm_free_contiguous(uint64_t phys_addr, uint64_t count) {
    uint64_t start_idx = phys_addr / PMM_PAGE_SIZE;
    if (start_idx == 0 || start_idx + count > PMM_MAX_TRACKED_PAGES) return;

    for (uint64_t p = 0; p < count; p++) {
        uint64_t idx = start_idx + p;
        if (bit_test(idx)) {
            bit_clear(idx);
            g_free_pages++;
        }
    }
}

uint64_t pmm_get_total_pages(void) { return g_total_pages; }
uint64_t pmm_get_free_pages(void)  { return g_free_pages; }
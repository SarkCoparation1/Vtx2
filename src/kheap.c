#include "kheap.h"
#include "pmm.h"
#include <stdbool.h>
#include <stddef.h>

#define KHEAP_ARENA_PAGES 256
#define KHEAP_MAGIC 0x4B484541u

typedef struct block_header {
    uint32_t magic;
    bool free;
    uint64_t size;
    struct block_header *prev;
    struct block_header *next;
} block_header_t;

typedef struct arena {
    uint64_t phys_base;
    uint64_t page_count;
    block_header_t *first_block;
    struct arena *next_arena;
} arena_t;

static arena_t *g_arena_list = NULL;
static uint64_t g_used_bytes = 0;
static uint64_t g_free_bytes = 0;

#define HEADER_SIZE sizeof(block_header_t)
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((uint64_t)(a) - 1))

static bool kheap_add_arena(uint64_t pages) {
    uint64_t phys = pmm_alloc_contiguous(pages);
    if (!phys) return false;

    arena_t *arena = (arena_t *)phys; /* arena metadata'sini arena'nin en basina yerlestiriyoruz */
    uint64_t arena_meta_size = ALIGN_UP(sizeof(arena_t), 16);

    arena->phys_base = phys;
    arena->page_count = pages;
    arena->next_arena = g_arena_list;

    block_header_t *first = (block_header_t *)((uint8_t *)phys + arena_meta_size);
    first->magic = KHEAP_MAGIC;
    first->free = true;
    first->size = (pages * PMM_PAGE_SIZE) - arena_meta_size - HEADER_SIZE;
    first->prev = NULL;
    first->next = NULL;

    arena->first_block = first;
    g_arena_list = arena;

    g_free_bytes += first->size;

    return true;
}

void kheap_init(void) {
    g_arena_list = NULL;
    g_used_bytes = 0;
    g_free_bytes = 0;
    kheap_add_arena(KHEAP_ARENA_PAGES);
}

static void split_block(block_header_t *b, uint64_t needed) {
    if (b->size < needed) return;

    uint64_t remaining = b->size - needed;
    if (remaining <= HEADER_SIZE + 16) {
        return;
    }

    uint8_t *new_block_addr = (uint8_t *)b + HEADER_SIZE + needed;
    block_header_t *new_block = (block_header_t *)new_block_addr;

    new_block->magic = KHEAP_MAGIC;
    new_block->free = true;
    new_block->size = remaining - HEADER_SIZE;
    new_block->prev = b;
    new_block->next = b->next;

    if (b->next) {
        b->next->prev = new_block;
    }
    b->next = new_block;
    b->size = needed;
}

static void try_merge_with_next(block_header_t *b) {
    if (b->next && b->next->free) {
        block_header_t *n = b->next;
        b->size += HEADER_SIZE + n->size;
        b->next = n->next;
        if (n->next) {
            n->next->prev = b;
        }
    }
}

static void *take_block(block_header_t *b, uint64_t needed) {
    split_block(b, needed);
    b->free = false;

    g_free_bytes -= b->size;
    g_used_bytes += b->size;

    return (void *)((uint8_t *)b + HEADER_SIZE);
}

void *kmalloc(uint64_t size) {
    if (size == 0) return NULL;

    uint64_t needed = ALIGN_UP(size, 16);

    for (arena_t *arena = g_arena_list; arena; arena = arena->next_arena) {
        for (block_header_t *b = arena->first_block; b; b = b->next) {
            if (b->free && b->size >= needed) {
                return take_block(b, needed);
            }
        }
    }

    uint64_t needed_pages = (needed + HEADER_SIZE + sizeof(arena_t) + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (needed_pages < KHEAP_ARENA_PAGES) needed_pages = KHEAP_ARENA_PAGES;

    if (!kheap_add_arena(needed_pages)) {
        return NULL; /* fiziksel bellek tukendi */
    }

    block_header_t *b = g_arena_list->first_block;
    if (b->size < needed) return NULL; /* olmamasi gereken durum, savunma amacli */

    return take_block(b, needed);
}

void kfree(void *ptr) {
    if (!ptr) return;

    block_header_t *b = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    if (b->magic != KHEAP_MAGIC) return;
    if (b->free) return;

    b->free = true;
    g_used_bytes -= b->size;
    g_free_bytes += b->size;

    try_merge_with_next(b);
    if (b->prev && b->prev->free) {
        try_merge_with_next(b->prev);
    }
}

uint64_t kheap_get_used_bytes(void) { return g_used_bytes; }
uint64_t kheap_get_free_bytes(void) { return g_free_bytes; }
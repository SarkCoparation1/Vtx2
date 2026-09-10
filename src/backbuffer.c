#include "backbuffer.h"
#include <stddef.h>

#define BACKBUFFER_CAPACITY_BYTES (1920ull * 1200ull * 4ull)

static uint8_t g_backbuffer[BACKBUFFER_CAPACITY_BYTES] __attribute__((aligned(16)));
static BootInfo g_back_boot_info;
static bool g_ready = false;

static inline void fast_copy(void *dst, const void *src, uint64_t n) {
    __asm__ volatile (
        "rep movsb"
        : "+D"(dst), "+S"(src), "+c"(n)
        :
        : "memory"
    );
}

static inline void fast_zero(void *dst, uint64_t n) {
    __asm__ volatile (
        "rep stosb"
        : "+D"(dst), "+c"(n)
        : "a"(0)
        : "memory"
    );
}

bool backbuffer_init(BootInfo *real_boot_info) {
    g_ready = false;
    if (real_boot_info == NULL) return false;

    uint64_t needed = (uint64_t)real_boot_info->ppsl * real_boot_info->height * real_boot_info->bytes_per_pixel;
    if (needed == 0 || needed > BACKBUFFER_CAPACITY_BYTES) return false;

    g_back_boot_info = *real_boot_info;
    g_back_boot_info.framebuffer = g_backbuffer;

    fast_zero(g_backbuffer, needed);

    g_ready = true;
    return true;
}

BootInfo *backbuffer_get(void) {
    return g_ready ? &g_back_boot_info : NULL;
}

void backbuffer_present(BootInfo *real_boot_info) {
    if (!g_ready || real_boot_info == NULL) return;

    uint64_t bytes = (uint64_t)real_boot_info->ppsl * real_boot_info->height * real_boot_info->bytes_per_pixel;
    if (bytes > BACKBUFFER_CAPACITY_BYTES) return;

    fast_copy(real_boot_info->framebuffer, g_backbuffer, bytes);
}

void backbuffer_present_rect(BootInfo *real_boot_info, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!g_ready || real_boot_info == NULL) return;

    /* Ekran sinirlarina gore kirp */
    if (x < 0) { if ((uint32_t)(-x) >= w) return; w += (uint32_t)x; x = 0; }
    if (y < 0) { if ((uint32_t)(-y) >= h) return; h += (uint32_t)y; y = 0; }
    if (x >= (int32_t)real_boot_info->width || y >= (int32_t)real_boot_info->height) return;
    if (x + (int32_t)w > (int32_t)real_boot_info->width) w = real_boot_info->width - (uint32_t)x;
    if (y + (int32_t)h > (int32_t)real_boot_info->height) h = real_boot_info->height - (uint32_t)y;
    if (w == 0 || h == 0) return;

    uint32_t bpp = real_boot_info->bytes_per_pixel;
    uint32_t row_bytes = w * bpp;

    for (uint32_t row = 0; row < h; row++) {
        uint64_t offset = ((uint64_t)((uint32_t)y + row) * real_boot_info->ppsl + (uint64_t)(uint32_t)x) * bpp;
        fast_copy(real_boot_info->framebuffer + offset, g_backbuffer + offset, row_bytes);
    }
}
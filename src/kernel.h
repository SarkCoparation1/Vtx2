#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint8_t *framebuffer;
	uint32_t width;
	uint32_t height;
	uint32_t ppsl;
	uint8_t bpp;
	uint8_t bytes_per_pixel;
	uint8_t is_bgr;
	uint64_t rsdp;
	uint64_t memory_map_addr;
	uint64_t memory_map_size;
	uint64_t memory_map_descriptor_size;
} BootInfo;

#define COLOR_NAVY 0x000B132B
#define COLOR_GREEN 0x0000E67E
#define COLOR_RED 0x00FF3366
#define COLOR_WHITE 0x00FFFFFF
#define COLOR_YELLOW 0x00FFFF00

void put_pixel(BootInfo *boot_info, uint32_t x, uint32_t y, uint32_t color);
void fill_screen(BootInfo *boot_info, uint32_t color);
void draw_rect(BootInfo *boot_info, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(BootInfo *boot_info, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(BootInfo *boot_info, const char *str, uint32_t x, uint32_t y, uint32_t color);
void draw_int(BootInfo *boot_info, int32_t num, uint32_t x, uint32_t y, uint32_t color);

void kernel_main(BootInfo *boot_info);

#endif
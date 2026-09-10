#include "gdt.h"
#include "tss.h"

#define GDT_TOTAL_BYTES 56

static uint8_t g_gdt[GDT_TOTAL_BYTES];

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

static gdtr_t g_gdtr;

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran_hi_nibble) {
    uint8_t *e = &g_gdt[index * 8];
    e[0] = (uint8_t)(limit & 0xFF);
    e[1] = (uint8_t)((limit >> 8) & 0xFF);
    e[2] = (uint8_t)(base & 0xFF);
    e[3] = (uint8_t)((base >> 8) & 0xFF);
    e[4] = (uint8_t)((base >> 16) & 0xFF);
    e[5] = access;
    e[6] = (uint8_t)(((limit >> 16) & 0x0F) | (gran_hi_nibble & 0xF0));
    e[7] = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_set_tss_entry(int byte_offset, uint64_t base, uint32_t limit) {
    uint8_t *e = &g_gdt[byte_offset];
    e[0] = (uint8_t)(limit & 0xFF);
    e[1] = (uint8_t)((limit >> 8) & 0xFF);
    e[2] = (uint8_t)(base & 0xFF);
    e[3] = (uint8_t)((base >> 8) & 0xFF);
    e[4] = (uint8_t)((base >> 16) & 0xFF);
    e[5] = 0x89;
    e[6] = (uint8_t)((limit >> 16) & 0x0F);
    e[7] = (uint8_t)((base >> 24) & 0xFF);

    e[8] = (uint8_t)((base >> 32) & 0xFF);
    e[9] = (uint8_t)((base >> 40) & 0xFF);
    e[10] = (uint8_t)((base >> 48) & 0xFF);
    e[11] = (uint8_t)((base >> 56) & 0xFF);
    e[12] = 0;
    e[13] = 0;
    e[14] = 0;
    e[15] = 0;
}

void gdt_init(void) {
    gdt_set_entry(0, 0, 0, 0x00, 0x00);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xA0);
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);

    gdt_set_tss_entry(GDT_TSS_SELECTOR, tss_get_base(), tss_get_limit());

    g_gdtr.limit = GDT_TOTAL_BYTES - 1;
    g_gdtr.base = (uint64_t)&g_gdt;

    __asm__ volatile ("lgdt %0" : : "m"(g_gdtr));

    __asm__ volatile (
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushq %1\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        : "n"(GDT_KERNEL_DATA_SELECTOR), "n"(GDT_KERNEL_CODE_SELECTOR)
        : "rax"
    );

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_TSS_SELECTOR));
}
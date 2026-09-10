#include "exceptions.h"
#include "idt.h"
#include "task.h"
#include "console.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} __attribute__((packed)) exc_frame_t;

typedef void (*isr_fn)(void);

extern void isr_exc0(void);  extern void isr_exc1(void);  extern void isr_exc2(void);  extern void isr_exc3(void);
extern void isr_exc4(void);  extern void isr_exc5(void);  extern void isr_exc6(void);  extern void isr_exc7(void);
extern void isr_exc8(void);  extern void isr_exc9(void);  extern void isr_exc10(void); extern void isr_exc11(void);
extern void isr_exc12(void); extern void isr_exc13(void); extern void isr_exc14(void); extern void isr_exc15(void);
extern void isr_exc16(void); extern void isr_exc17(void); extern void isr_exc18(void); extern void isr_exc19(void);
extern void isr_exc20(void); extern void isr_exc21(void); extern void isr_exc22(void); extern void isr_exc23(void);
extern void isr_exc24(void); extern void isr_exc25(void); extern void isr_exc26(void); extern void isr_exc27(void);
extern void isr_exc28(void); extern void isr_exc29(void); extern void isr_exc30(void); extern void isr_exc31(void);

static isr_fn exception_stubs[32] = {
    isr_exc0,  isr_exc1,  isr_exc2,  isr_exc3,  isr_exc4,  isr_exc5,  isr_exc6,  isr_exc7,
    isr_exc8,  isr_exc9,  isr_exc10, isr_exc11, isr_exc12, isr_exc13, isr_exc14, isr_exc15,
    isr_exc16, isr_exc17, isr_exc18, isr_exc19, isr_exc20, isr_exc21, isr_exc22, isr_exc23,
    isr_exc24, isr_exc25, isr_exc26, isr_exc27, isr_exc28, isr_exc29, isr_exc30, isr_exc31,
};

void exceptions_init(void) {
    uint16_t cs = idt_get_kernel_cs();
    for (int i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, (uint64_t)exception_stubs[i], cs, 0x8E);
    }
}

static const char *exception_name(uint64_t vector) {
    switch (vector) {
        case 0:  return "Bolme Hatasi (#DE)";
        case 1:  return "Debug (#DB)";
        case 2:  return "NMI";
        case 3:  return "Breakpoint (#BP)";
        case 4:  return "Tasma (#OF)";
        case 5:  return "BOUND Araligi Asildi (#BR)";
        case 6:  return "Gecersiz Opcode (#UD)";
        case 7:  return "Cihaz/FPU Yok (#NM)";
        case 8:  return "Double Fault (#DF)";
        case 10: return "Gecersiz TSS (#TS)";
        case 11: return "Segment Yok (#NP)";
        case 12: return "Stack Hatasi (#SS)";
        case 13: return "Genel Koruma Hatasi (#GP)";
        case 14: return "Sayfa Hatasi (#PF)";
        case 16: return "x87 FPU Hatasi (#MF)";
        case 17: return "Hizalama Kontrolu (#AC)";
        case 18: return "Makine Kontrolu (#MC)";
        case 19: return "SIMD FP Hatasi (#XM)";
        default: return "Bilinmeyen Istisna";
    }
}

static char *str_append(char *dst, const char *src) {
    while (*src) { *dst++ = *src++; }
    *dst = '\0';
    return dst;
}

static char *hex_append(char *dst, uint64_t v) {
    static const char digits[] = "0123456789ABCDEF";
    *dst++ = '0';
    *dst++ = 'x';
    for (int i = 15; i >= 0; i--) {
        *dst++ = digits[(v >> (i * 4)) & 0xF];
    }
    *dst = '\0';
    return dst;
}

static uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static uint32_t g_exc_log_line = 0;
#define EXC_LOG_BASE_Y 300
#define EXC_LOG_LINE_HEIGHT 16
#define EXC_LOG_MAX_LINES 8

uint64_t exception_dispatch_c(uint64_t saved_regs_rsp) {
    exc_frame_t *f = (exc_frame_t *)saved_regs_rsp;
    /* Visible even before the backbuffer has been presented. */
    __asm__ volatile ("outb %b0, $0xe9" : : "a"('!'));
    __asm__ volatile ("outb %b0, $0xe9" : : "a"((char)('A' + (f->vector & 0x1F))));
    bool from_user = (f->cs & 0x3) == 3;

    char line[160];
    char *p = line;
    p = str_append(p, from_user ? "[RING3 ISTISNA] " : "[KERNEL - FATAL] ");
    p = str_append(p, exception_name(f->vector));
    p = str_append(p, "  RIP=");
    p = hex_append(p, f->rip);
    if (f->vector == 14) {
        p = str_append(p, "  CR2=");
        hex_append(p, read_cr2());
    }

    uint32_t y = EXC_LOG_BASE_Y + (g_exc_log_line % EXC_LOG_MAX_LINES) * EXC_LOG_LINE_HEIGHT;
    g_exc_log_line++;
    console_print(line, 20, y, from_user ? 0xFFCC00 : 0xFF3030);

    if (from_user) {
        task_exit_current();
        return task_schedule(saved_regs_rsp);
    }

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

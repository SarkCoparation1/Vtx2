#include "syscall.h"
#include "console.h"
#include "task.h"
#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
} __attribute__((packed)) saved_regs_t;

void syscall_dispatch_c(uint64_t saved_regs_rsp) {
    saved_regs_t *regs = (saved_regs_t *)saved_regs_rsp;

    uint64_t num = regs->rax;
    uint64_t arg1 = regs->rdi;
    uint64_t arg2 = regs->rsi;
    uint64_t ret = (uint64_t)-1;

    switch (num) {
        case SYS_WRITE: {
            const char *msg = (const char *)arg1;
            uint32_t y = (uint32_t)(100 + (arg2 % 5) * 20);
            console_print(msg, 50, y, 0xFFFFFF);
            ret = 0;
            break;
        }
        case SYS_EXIT:
            task_exit_current();
            ret = 0;
            break;
        default:
            ret = (uint64_t)-1;
            break;
    }

    regs->rax = ret;
}

void syscall_init(void) {

}
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_EXIT 0
#define SYS_WRITE 1

void syscall_dispatch_c(uint64_t saved_regs_rsp);
extern void syscall_stub(void);
void syscall_init(void);

#endif
#ifndef USYSCALL_H
#define USYSCALL_H

#include "syscall.h"
#include <stdint.h>

static inline uint64_t usys_write(const char *msg, uint64_t slot) {
    uint64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((uint64_t)SYS_WRITE), "D"(msg), "S"(slot)
        : "memory"
    );
    return ret;
}

static inline void usys_exit(void) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"((uint64_t)SYS_EXIT)
        : "memory"
    );
}

#endif
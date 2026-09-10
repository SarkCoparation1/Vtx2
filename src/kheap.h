#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>

void kheap_init(void);
void *kmalloc(uint64_t size);
void kfree(void *ptr);
uint64_t kheap_get_used_bytes(void);
uint64_t kheap_get_free_bytes(void);

#endif
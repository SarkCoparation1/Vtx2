#include "tss.h"

static tss_t g_tss;

void tss_init(void) {
    uint8_t *p = (uint8_t *)&g_tss;
    for (uint64_t i = 0; i < sizeof(tss_t); i++) {
        p[i] = 0;
    }

    g_tss.iomap_base = sizeof(tss_t);
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}

uint64_t tss_get_base(void) {
    return (uint64_t)&g_tss;
}

uint32_t tss_get_limit(void) {
    return (uint32_t)sizeof(tss_t) - 1;
}
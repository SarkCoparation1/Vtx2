#ifndef POWER_H
#define POWER_H

#include "kernel.h"

void power_init(void);
void power_shutdown(BootInfo *boot_info);
void power_reboot(BootInfo *boot_info);

#endif
#ifndef ARCH_INIT_H
#define ARCH_INIT_H
#include <kernel/boot.h>
void early_init(void);
void arch_init(struct boot_info *boot);
#endif

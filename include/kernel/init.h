#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H
#include <kernel/boot.h>
[[noreturn]] void kernel_init(const struct boot_info *boot);
#endif

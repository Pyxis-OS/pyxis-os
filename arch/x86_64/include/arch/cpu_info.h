#ifndef ARCH_CPU_INFO_H
#define ARCH_CPU_INFO_H

#define ARCH_CPU_BRAND_BYTES 48

/* Sample this CPU's guest-visible CPUID brand. Empty if absent. The caller
 * supplies room for ARCH_CPU_BRAND_BYTES plus the terminating NUL. */
void arch_cpu_brand(char brand[ARCH_CPU_BRAND_BYTES + 1]);

#endif

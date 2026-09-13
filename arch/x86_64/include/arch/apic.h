#ifndef ARCH_APIC_H
#define ARCH_APIC_H

#include <stdint.h>

#define APIC_TIMER_VECTOR 32
#define APIC_SPURIOUS_VECTOR 255

/* Single boot CPU, xAPIC mode. Paging maps this device before APIC setup. */
uint64_t apic_physical_address(void);
void apic_init(void);
void apic_timer_start(void);
void apic_end_interrupt(void);

#endif

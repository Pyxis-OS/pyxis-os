#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H
#include <kernel/boot.h>

/* BSP bootstrap after VM and heap initialization, before AP startup. Copies
 * the RSDP address and firmware memory map and reserves the firmware mapping
 * window. Failure leaves ACPI unavailable; it never stops boot. */
void acpi_prepare(const struct boot_info *boot);

/* BSP, IF=0, after task_init(). Creates the single ACPI worker, which owns
 * uACPI, AML execution, the SCI and deferred ACPI work. */
void acpi_start(void);

/* SCI interrupt entry on the BSP, IF=0. Masks the input and wakes the worker;
 * no uACPI code runs in interrupt entry. */
void acpi_interrupt(void);

#endif

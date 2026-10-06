#ifndef KERNEL_ACPI_HOST_H
#define KERNEL_ACPI_HOST_H
#include <abi/syscall.h>
#include <kernel/acpi.h>
#include <kernel/boot.h>
#include <kernel/mm/types.h>
#include <stddef.h>
#include <stdint.h>

/* Every uACPI entry point and host callback runs on the ACPI worker. Asserts
 * that the caller is that worker; there is no other thread to wait for. */
void acpi_require_worker(void);

/* ACPI worker, IF=1, holding no uACPI spinlock. Sleeps until DEADLINE or an
 * SCI, then runs uACPI's SCI handler if one is pending. That handler may signal
 * the event a caller waits for, so callers loop and recheck their condition.
 * UINT64_MAX waits for an SCI only. Deferred work never runs here. */
void acpi_worker_block(uint64_t deadline);

/* Firmware mapping window. Prepare follows acpi_prepare's contract. Mappings
 * are never removed and their addresses are never reused: unmapping only
 * checks the address, and a request for already covered pages returns the
 * existing mapping. ACPI and firmware memory is cached; reserved or unlisted
 * physical space is uncached device memory, including registers the kernel
 * also uses; RAM, kernel, loader, framebuffer and bad memory are refused, as
 * is a request mixing classes. Map and unmap run on the worker. */
bool acpi_map_prepare(const struct boot_info *boot);
void *acpi_map(phys_addr_t physical, size_t bytes);
void acpi_unmap(void *address, size_t bytes);
size_t acpi_map_pages(void);

/* ACPI worker, IF=1, top level. Holds user tasks, flushes the native pools,
 * then enters S5 or resets. Returns only on failure, with user tasks released
 * and pools unsealed, so the system keeps running. */
enum call_status acpi_power_run(enum acpi_power_action action);

/* Requested bytes and blocks currently allocated by uACPI. Worker only. */
struct acpi_heap_use {
  size_t bytes, blocks;
};
struct acpi_heap_use acpi_heap_use(void);

#endif

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

/* BSP, IF=0. Wakes the worker for a queued table request. */
void acpi_worker_notify(void);

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
 * confirms NVMe shutdown, then enters S5 or resets. FORCE bypasses both storage
 * steps and is only for an explicit local overlay choice after a failed clean
 * operation. Returns only on failure, with user tasks released and any sealed
 * pools unsealed. NVMe controllers stay retired until reboot. */
enum call_status acpi_power_run(enum acpi_power_action action, bool force);

/* Register the bounded legacy SystemCMOS handler before namespace initialization. */
void acpi_cmos_start(void);

/* Embedded controller. Load_ecdt runs after the namespace is loaded and
 * before it is initialized, so _INI and _STA can reach an ECDT controller;
 * start runs after initialization, finds the PNP0C09 device when there was no
 * usable ECDT, then enables the controller's GPE. Worker only. */
void acpi_ec_load_ecdt(void);
void acpi_ec_start(void);

/* One-shot, worker-owned HID-I2C endpoint discovery after namespace and EC
 * initialization. Publishes owned firmware values, without accessing I2C. */
void acpi_hid_i2c_start(void);

/* One-shot AX200 PCI-companion RF-policy capture, after PCI root discovery.
 * Unavailable publishes failure if bootstrap or namespace startup fails. */
void acpi_wifi_start(void);
void acpi_wifi_unavailable(void);
/* One-shot RTL8922AE capture, gated by wifi.rtl8922ae opt-in. Same worker
 * and publication contract; Intel capture owns the unavailable hook. */
void acpi_rtl_wifi_start(void);
void acpi_rtl_wifi_unavailable(void);

/* Power buttons. Start installs firmware-declared Notify sources and the
 * fixed event where present. Press runs on this worker, either SCI service or
 * deferred Notify delivery; startup and in-operation presses are dropped. */
void acpi_buttons_start(void);
void acpi_button_press(void);

/* Batteries and the AC adapter. Start finds the devices after acpi_ec_start.
 * Poll reads them when NOW has reached the next poll, and returns that next
 * poll time, UINT64_MAX without any device. Worker only, top level. */
void acpi_battery_start(void);
uint64_t acpi_battery_poll(uint64_t now);

/* UCSI client for a PNP0CA0 device. Start binds the device after
 * acpi_ec_start; poll advances the client when NOW reaches its due time or a
 * Notify arrived, and returns the next due time, UINT64_MAX when nothing is
 * due. Worker only, top level: completion arrives through deferred EC queries,
 * so it is never awaited inside Notify. */
void acpi_ucsi_start(void);
uint64_t acpi_ucsi_poll(uint64_t now);

/* Requested bytes and blocks currently allocated by uACPI. Worker only. */
struct acpi_heap_use {
  size_t bytes, blocks;
};
struct acpi_heap_use acpi_heap_use(void);

#endif

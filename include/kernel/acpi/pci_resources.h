/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_ACPI_PCI_RESOURCES_H
#define KERNEL_ACPI_PCI_RESOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Internal capture policy; exhausting any bound refuses the whole snapshot. */
#define ACPI_PCI_ROOT_CAPACITY 16
#define ACPI_PCI_WINDOW_CAPACITY 128
#define ACPI_PCI_RESERVATION_CAPACITY 512
#define ACPI_PCI_RESOURCE_PATH_CAPACITY 128

enum acpi_pci_resources_state {
  ACPI_PCI_RESOURCES_PENDING,
  ACPI_PCI_RESOURCES_READY,
  ACPI_PCI_RESOURCES_REFUSED,
};

/* Inclusive bus-address endpoints. Capture supports only identity translation;
 * these are not PMM allocations, pointers or permission to map physical memory. */
struct acpi_pci_resource_range {
  uint64_t first, last;
};

struct acpi_pci_root {
  char path[ACPI_PCI_RESOURCE_PATH_CAPACITY];
  uint16_t segment;
  uint8_t first_bus, last_bus;
};

struct acpi_pci_window {
  struct acpi_pci_resource_range range;
  size_t root;
  bool prefetchable, writable;
};

struct acpi_pci_reservation {
  struct acpi_pci_resource_range range;
  char path[ACPI_PCI_RESOURCE_PATH_CAPACITY];
};

/* Owned once, immutable after READY. Reservations are physical memory; with
 * identity translation they also exclude those addresses in every PCI root.
 * Disabled devices' valid _CRS memory remains reserved conservatively. */
struct acpi_pci_resources {
  size_t root_count, window_count, reservation_count;
  struct acpi_pci_root roots[ACPI_PCI_ROOT_CAPACITY];
  struct acpi_pci_window windows[ACPI_PCI_WINDOW_CAPACITY];
  struct acpi_pci_reservation reservations[ACPI_PCI_RESERVATION_CAPACITY];
};

/* ACPI worker only, once after namespace and EC initialization. Failure only
 * refuses PCI allocation; it does not stop ACPI, discovery or input startup. */
void acpi_pci_resources_start(void);

/* BSP, IF=0. Sets RESOURCES only when READY, to boot-lifetime immutable owned
 * storage. No namespace node, resource buffer or uACPI string escapes capture. */
enum acpi_pci_resources_state acpi_pci_resources_snapshot(
    const struct acpi_pci_resources **resources);
#endif

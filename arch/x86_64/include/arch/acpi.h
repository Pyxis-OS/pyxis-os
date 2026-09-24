#ifndef ARCH_ACPI_H
#define ARCH_ACPI_H

#include <kernel/boot.h>

struct keyboard_irq_route {
  uint64_t io_apic_physical;
  uint32_t gsi_base;
  uint32_t gsi;
  bool active_low;
  bool level_triggered;
};

/* BSP bootstrap only, before replacing Limine's root. Copies routing data;
 * no ACPI pointer or direct-map alias survives this call. */
bool acpi_keyboard_route(const struct boot_info *boot,
                         struct keyboard_irq_route *route);

/* Same bootstrap lifetime as keyboard routing. Requires a memory-mapped HPET. */
uint64_t acpi_hpet_address(const struct boot_info *boot);

struct pci_ecam;
/* Copies the supported MCFG aperture while firmware tables remain mapped.
 * Missing/unsupported MCFG disables discovery with a diagnostic. */
bool acpi_pci_ecam(const struct boot_info *boot, struct pci_ecam *ecam);

#endif

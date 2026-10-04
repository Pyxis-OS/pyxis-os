#ifndef ARCH_ACPI_H
#define ARCH_ACPI_H

#include <kernel/boot.h>

struct isa_irq_route {
  uint64_t io_apic_physical;
  uint32_t gsi_base;
  uint32_t gsi;
  bool active_low;
  bool level_triggered;
};

/* BSP bootstrap only, before replacing Limine's root. Copies routing data;
 * no ACPI pointer or direct-map alias survives this call. False means no 8042
 * or keyboard (ISA IRQ 1) route. The mouse (ISA IRQ 12) route is zeroed when
 * absent or invalid; that never affects the keyboard result. */
bool acpi_ps2_routes(const struct boot_info *boot, struct isa_irq_route *keyboard,
                     struct isa_irq_route *mouse);

/* Same bootstrap lifetime as PS/2 routing. Requires a memory-mapped HPET. */
uint64_t acpi_hpet_address(const struct boot_info *boot);

struct pci_ecam;
/* Copies the supported MCFG aperture while firmware tables remain mapped.
 * Missing/unsupported MCFG disables discovery with a diagnostic. */
bool acpi_pci_ecam(const struct boot_info *boot, struct pci_ecam *ecam);

#endif

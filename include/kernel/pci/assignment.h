/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_PCI_ASSIGNMENT_H
#define KERNEL_PCI_ASSIGNMENT_H
#include <kernel/pci.h>

/* Stable native-owner storage. Reserved before AP startup, filled once after
 * firmware resource capture. Never reused or unmapped, including on failure. */
struct pci_assignment_window {
  const struct pci_claim *claim;
  uint64_t generation;
  uintptr_t base;
  size_t bytes;
  bool consumed;
};

void pci_assignment_prepare(const struct boot_info *boot);
enum mm_result pci_assignment_window_prepare(struct pci_claim *claim, size_t bytes,
    struct pci_assignment_window *window);

/* BSP/IF=0, same live claim, native driver-proven hardware fence. IO/MEM/BME
 * and all interrupt delivery must be off. Assigns only an unassigned memory
 * BAR; never moves another assignment. Capture/route/size/occupancy ambiguity
 * refuses with one diagnostic. Success publishes claim->bars[bar] and ADDRESS,
 * leaves decode/BME off, and pins the claim/physical range until reboot.
 * No output is published on failure. Fresh mapping slots remain consumed. */
bool pci_assign_memory_bar(struct pci_claim *claim, unsigned bar,
    struct pci_assignment_window *window, uintptr_t *address);
#endif

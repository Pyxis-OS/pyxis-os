#ifndef ARCH_BOCHS_DISPLAY_H
#define ARCH_BOCHS_DISPLAY_H

#include <kernel/boot.h>
#include <kernel/pci.h>

enum bochs_display_result {
  BOCHS_DISPLAY_READY,
  BOCHS_DISPLAY_REFUSED,
  BOCHS_DISPLAY_RESTORE_FAILED,
};

bool arch_bochs_display_matches(const struct pci_device *device);

/* BSP/IF=0 before AP startup, after the display owner stops CPU pixel writes.
 * Refusal performs no DISPI mode writes, except an attempted mode whose exact
 * firmware register state has been restored and verified. PCI ownership and
 * permanent mappings remain until reboot. Reasons are static strings. */
enum bochs_display_result arch_bochs_display_prepare(const struct boot_info *boot,
    struct pci_device *device, uint32_t width, uint32_t height,
    uintptr_t *address, const char **reason);

#endif

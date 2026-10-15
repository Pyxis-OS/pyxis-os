#ifndef KERNEL_PCI_PLACEMENT_H
#define KERNEL_PCI_PLACEMENT_H

#include <kernel/pci.h>

enum pci_bar_placement_result {
  PCI_BAR_PLACEMENT_OK,
  PCI_BAR_PLACEMENT_INVALID,
  PCI_BAR_PLACEMENT_NOT_QUIESCENT,
  PCI_BAR_PLACEMENT_UNSUPPORTED,
  PCI_BAR_PLACEMENT_ASSIGNED,
  PCI_BAR_PLACEMENT_NO_SIZE,
  PCI_BAR_PLACEMENT_INVALID_MASK,
  PCI_BAR_PLACEMENT_CHANGED,
  PCI_BAR_PLACEMENT_READBACK_FAILED,
  PCI_BAR_PLACEMENT_UNCERTAIN,
};

struct pci_bar_placement {
  const struct pci_claim *claim;
  const struct pci_device *device;
  uint64_t generation, bytes;
  struct pci_address address;
  uint16_t vendor_id, device_id;
  uint8_t revision, header_type;
  unsigned bar;
  uint32_t original_low, original_high;
  bool memory64, prefetchable;
};

/* BSP/IF=0, reserved or completed endpoint claim, no driver DMA started.
 * The driver must have proven quiescence, including across any power transition.
 * IO/MEM/BME must be off, INTx disabled and MSI/MSI-X disabled; checked live.
 * Probe writes only this zero-base standard memory BAR and its paired upper
 * half, restores both and checks readback before publishing metadata. Output is
 * cleared on failure. NO_SIZE includes an all-zero mask that cannot prove an
 * implemented BAR. Size is also the required power-of-two bus alignment.
 * UNCERTAIN retains the claim; restoration is unsafe if identity changes or
 * live decode cannot be disabled. No claim or BAR-size publication occurs here. */
enum pci_bar_placement_result pci_probe_unassigned_memory_bar(struct pci_claim *claim,
    unsigned bar, struct pci_bar_placement *placement);

/* Same fence and ownership contract. Caller must retain an allocation-ledger
 * reservation proving the whole bus-address extent free and routable, bound to
 * this exact claim generation/device/BAR, before calling. Metadata is the
 * unchanged successful probe snapshot; size and address width/alignment are
 * checked again. This programs and verifies only the selected BAR, leaving
 * decoding and DMA off. It does not publish claim->bars or release anything.
 * READBACK_FAILED means the original zero-base values were restored and checked.
 * UNCERTAIN means BAR restoration or the fence could not be confirmed: retain
 * claim and extent until reboot. If decode unexpectedly becomes enabled, first
 * attempt to disable IO/MEM/BME; never rewrite a BAR while that remains unproven.
 * The helper cannot promise a known-off fence when hardware rejects the write. */
enum pci_bar_placement_result pci_program_unassigned_memory_bar(struct pci_claim *claim,
    const struct pci_bar_placement *placement, uint64_t bus_base);

const char *pci_bar_placement_result_name(enum pci_bar_placement_result result);

#endif

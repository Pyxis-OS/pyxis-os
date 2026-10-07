#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <kernel/boot.h>
#include <kernel/mm/types.h>

#define PCI_BUS_COUNT 256
#define PCI_DEVICE_COUNT 32
#define PCI_FUNCTION_COUNT 8
#define PCI_CONFIG_BYTES 4096

struct pci_address {
  uint8_t bus, device, function;
};

struct pci_claim;
struct pci_device {
  struct pci_address address;
  uint16_t vendor_id, device_id;
  uint8_t base_class, subclass, interface, revision;
  uint8_t header_type;
  struct pci_claim *owner;
  struct pci_device *next;
};

/* BSP boot inventory, retained for driver lookup. Records live for the boot. */
void pci_discover(void);
struct pci_device *pci_find_device(uint16_t vendor, uint16_t device);

enum pci_inventory_state { PCI_INVENTORY_UNAVAILABLE, PCI_INVENTORY_INCOMPLETE,
                           PCI_INVENTORY_COMPLETE };
/* Immutable once discovery returns, before user tasks run; any CPU may read.
 * Count and indices cover retained records in a stable, unspecified order. */
enum pci_inventory_state pci_inventory_state(void);
size_t pci_device_count(void);
const struct pci_device *pci_device_at(size_t index);
enum pci_selection { PCI_SELECTION_ABSENT, PCI_SELECTION_UNIQUE,
                     PCI_SELECTION_AMBIGUOUS, PCI_SELECTION_INCOMPLETE };
/* Clears output unless exactly one match exists in a complete inventory. */
enum pci_selection pci_select_device(uint16_t vendor, uint16_t device,
    struct pci_device **selected);
enum pci_selection pci_select_class(uint8_t base_class, uint8_t subclass,
    uint8_t interface, struct pci_device **selected);

/* Six BAR registers and at most 48 aligned capability headers are hardware
 * layout limits, not limits on the number of devices the kernel can own. */
#define PCI_BAR_COUNT 6
#define PCI_CAP_COUNT 48
#define PCI_BOOTSTRAP_BAR_BYTES 4096

struct pci_bar {
  phys_addr_t physical;
  uint64_t bytes;
};

struct pci_mapping {
  uintptr_t address, base;
  size_t bytes;
  struct pci_mapping *next;
};

struct pci_region {
  unsigned bar;
  uint32_t offset, length;
  struct pci_mapping mapping;
};

struct pci_claim {
  struct pci_device *device;
  uint16_t saved_command;
  uint8_t capabilities[PCI_CAP_COUNT];
  unsigned capability_count;
  struct pci_bar bars[PCI_BAR_COUNT];
  struct pci_mapping *mappings;
  bool reserved, dma_started;
};

struct pci_probe_state {
  uint16_t command, pmcsr;
  unsigned power_capability;
  bool command_changed, power_changed;
};

/* BSP/IF=0 before AP startup, stable zeroed claim/mapping records. Reserve
 * an endpoint with MSI/MSI-X disabled. Software/configuration ownership leaves
 * firmware command state unchanged.
 * Complete only after driver-confirmed ownership handoff and halt: disable DMA
 * and INTx, checking readback. A failed completion retains an ordinary claim;
 * release only if quiescence is confirmed, otherwise retain it until reboot.
 * Cancel an uncompleted reservation without command writes. No driver DMA or
 * interrupt delivery may have been published, and firmware BME is preserved. */
bool pci_reserve_device(struct pci_device *device, struct pci_claim *claim);
/* Same reservation contract for a retained inventory index. */
bool pci_reserve_device_at(size_t index, struct pci_claim *claim);
bool pci_complete_claim(struct pci_claim *claim);
void pci_cancel_reservation(struct pci_claim *claim);
/* Reserved claim before mappings, zeroed caller-owned snapshot. If D0/memory
 * decode is already usable, leave configuration untouched, including BME.
 * Otherwise require BME off before temporarily waking/enabling memory decode;
 * reject reset-causing D3hot wake, preserve PME enable and leave PME status.
 * Begin failure may
 * follow a write: restore before cancellation on unsupported identity/failure.
 * Restore requires the still-reserved claim and no other hardware changes;
 * false retains ownership until reboot. */
bool pci_begin_mmio_probe(struct pci_claim *claim, struct pci_probe_state *state);
bool pci_restore_mmio_probe(struct pci_claim *claim, struct pci_probe_state *state);
/* Assigned, page-aligned memory BAR low-half prefix, before completion/sizing.
 * Requires memory decoding enabled; maps PCI_BOOTSTRAP_BAR_BYTES.
 * Caller checks every access/body against this provisional extent and validates
 * the extent against the sized BAR after reset. This does not publish BAR size. */
enum mm_result pci_map_bootstrap_bar(struct pci_claim *claim, unsigned bar,
    const struct boot_info *boot, struct pci_mapping *mapping);

/* BSP/IF=0 before AP startup. Caller keeps claim/mapping records at stable
 * addresses, initially zeroed. Endpoint functions with MSI/MSI-X disabled only.
 * Claim disables bus mastering and INTx; the driver must confirm quiescence
 * and disable address decoding before sizing BARs. DMA/IRQ controllers must
 * reset/halt; a QEMU display instead retires its CPU writers (no DMA/IRQ).
 * Release is boot failure unwinding, not hot-unplug: unmap resources, restore original decoding with
 * DMA/INTx kept disabled, and withdraw configuration write access. A reset is
 * not reversible. The driver must not start DMA before this unwind path. */
bool pci_claim_device(struct pci_device *device, struct pci_claim *claim);
void pci_release_device(struct pci_claim *claim);
bool pci_size_bars(struct pci_claim *claim);
bool pci_capability_fits(const struct pci_claim *claim, unsigned offset, size_t bytes);
enum mm_result pci_map_bar(struct pci_claim *claim, unsigned bar, uint64_t offset,
                           size_t bytes, const struct boot_info *boot,
                           struct pci_mapping *mapping);
/* Sized display memory BAR, BSP/IF=0 before AP startup, completed claim/no DMA.
 * Reuses a boot framebuffer only when it starts at the BAR base, retaining WC.
 * Returns a permanent fixed aperture, outside claim->mappings; release never
 * withdraws the boot/panic framebuffer. Other BARs still use pci_map_bar. */
enum mm_result pci_map_display_bar(struct pci_claim *claim, unsigned bar,
    const struct boot_info *boot, uintptr_t *address);
/* Established owner, BSP/IF=0, including activation after AP startup. Does not
 * change mappings; claim/release and BAR preparation remain boot-only. Command
 * writes are 16-bit and require a completed claim. Any later BME-enable write
 * permanently excludes boot release, even after BME is disabled again. */
void pci_write8(struct pci_claim *claim, unsigned offset, uint8_t value);
void pci_write16(struct pci_claim *claim, unsigned offset, uint16_t value);
void pci_write32(struct pci_claim *claim, unsigned offset, uint32_t value);

#endif

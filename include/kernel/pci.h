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

/* Six BAR registers and at most 48 aligned capability headers are hardware
 * layout limits, not limits on the number of devices the kernel can own. */
#define PCI_BAR_COUNT 6
#define PCI_CAP_COUNT 48

struct pci_bar {
  phys_addr_t physical;
  uint64_t bytes;
};

struct pci_mapping {
  uintptr_t address, base;
  size_t bytes;
  struct pci_mapping *next;
};

struct pci_claim {
  struct pci_device *device;
  uint16_t saved_command;
  uint8_t capabilities[PCI_CAP_COUNT];
  unsigned capability_count;
  struct pci_bar bars[PCI_BAR_COUNT];
  struct pci_mapping *mappings;
};

/* BSP/IF=0 before AP startup. Caller keeps claim/mapping records at stable
 * addresses, initially zeroed. Endpoint functions with MSI/MSI-X disabled only.
 * Claim disables bus mastering and INTx; the driver must confirm device reset
 * and disable address decoding before sizing BARs. Release is boot failure
 * unwinding, not hot-unplug: unmap resources, restore original decoding with
 * DMA/INTx kept disabled, and withdraw configuration write access. A reset is
 * not reversible. The driver must not start DMA before this unwind path. */
bool pci_claim_device(struct pci_device *device, struct pci_claim *claim);
void pci_release_device(struct pci_claim *claim);
bool pci_size_bars(struct pci_claim *claim);
bool pci_capability_fits(const struct pci_claim *claim, unsigned offset, size_t bytes);
enum mm_result pci_map_bar(struct pci_claim *claim, unsigned bar, uint64_t offset,
                           size_t bytes, const struct boot_info *boot,
                           struct pci_mapping *mapping);
/* Established owner, BSP/IF=0, including activation after AP startup. Does not
 * change mappings; claim/release and BAR preparation remain boot-only. */
void pci_write8(struct pci_claim *claim, unsigned offset, uint8_t value);
void pci_write16(struct pci_claim *claim, unsigned offset, uint16_t value);
void pci_write32(struct pci_claim *claim, unsigned offset, uint32_t value);

#endif

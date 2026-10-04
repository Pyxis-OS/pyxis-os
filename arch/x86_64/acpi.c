#include <arch/acpi.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define RSDP_V1_BYTES 20
#define FADT_BOOT_ARCH_OFFSET 109
#define FADT_BOOT_ARCH_REVISION 3
#define FADT_HAS_8042 (1u << 1)
#define MADT_IO_APIC 1
#define MADT_IRQ_OVERRIDE 2
#define ISA_BUS 0
#define KEYBOARD_ISA_IRQ 1
#define MOUSE_ISA_IRQ 12
#define INTI_POLARITY_MASK 3
#define INTI_TRIGGER_SHIFT 2
#define INTI_TRIGGER_MASK 3
#define INTI_CONFORMS 0
#define INTI_HIGH_OR_EDGE 1
#define INTI_LOW_OR_LEVEL 3

struct acpi_rsdp {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt;
  uint32_t length;
  uint64_t xsdt;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} __attribute__((packed));

struct acpi_header {
  char signature[4];
  uint32_t length;
  uint8_t revision, checksum;
  char oem_id[6], oem_table_id[8];
  uint32_t oem_revision, creator_id, creator_revision;
} __attribute__((packed));

struct acpi_madt {
  struct acpi_header header;
  uint32_t local_apic_address, flags;
} __attribute__((packed));

struct madt_entry {
  uint8_t type, length;
} __attribute__((packed));

struct madt_io_apic {
  struct madt_entry header;
  uint8_t id, reserved;
  uint32_t address, gsi_base;
} __attribute__((packed));

struct madt_irq_override {
  struct madt_entry header;
  uint8_t bus, source;
  uint32_t gsi;
  uint16_t flags;
} __attribute__((packed));

static const void *firmware_pointer(const struct boot_info *boot,
                                    uint64_t physical, size_t bytes)
{
  if (!physical || physical > UINT64_MAX - bytes ||
      physical + bytes > UINTPTR_MAX - boot->bootstrap_direct_offset) {
    panic("ACPI address extent overflows");
  }

  uint64_t end = physical + bytes;
  uint64_t covered = physical;
  for (size_t i = 0; i < boot->region_count && covered < end; ++i) {
    const struct boot_region *region = &boot->regions[i];
    uint64_t region_end = region->base + region->length;
    if (region_end <= covered) {
      continue;
    }
    if (region->base > covered ||
        (region->type != BOOT_ACPI && region->type != BOOT_FIRMWARE)) {
      break;
    }
    covered = region_end;
  }
  if (covered < end) {
    panic("ACPI table outside mapped firmware memory");
  }
  return (const void *)(boot->bootstrap_direct_offset + physical);
}

static bool checksum_valid(const void *data, size_t bytes)
{
  const uint8_t *cursor = data;
  uint8_t sum = 0;
  for (size_t i = 0; i < bytes; ++i) {
    sum += cursor[i];
  }
  return sum == 0;
}

static const struct acpi_header *read_table(const struct boot_info *boot,
                                           uint64_t physical)
{
  const struct acpi_header *header = firmware_pointer(boot, physical, sizeof(*header));
  if (header->length < sizeof(*header)) {
    panic("truncated ACPI table header");
  }
  firmware_pointer(boot, physical, header->length);
  if (!checksum_valid(header, header->length)) {
    panic("invalid ACPI table checksum");
  }
  return header;
}

static const struct acpi_header *root_table(const struct boot_info *boot,
                                           size_t *entry_bytes)
{
  const struct acpi_rsdp *rsdp = firmware_pointer(boot, boot->acpi_rsdp, RSDP_V1_BYTES);
  if (memcmp(rsdp->signature, "RSD PTR ", 8) || !checksum_valid(rsdp, RSDP_V1_BYTES)) {
    panic("invalid ACPI RSDP");
  }

  uint64_t physical = rsdp->rsdt;
  *entry_bytes = sizeof(uint32_t);
  if (rsdp->revision >= 2) {
    firmware_pointer(boot, boot->acpi_rsdp, sizeof(*rsdp));
    if (rsdp->length < sizeof(*rsdp)) {
      panic("truncated extended ACPI RSDP");
    }
    firmware_pointer(boot, boot->acpi_rsdp, rsdp->length);
    if (!checksum_valid(rsdp, rsdp->length)) {
      panic("invalid extended ACPI RSDP checksum");
    }
    if (rsdp->xsdt) {
      physical = rsdp->xsdt;
      *entry_bytes = sizeof(uint64_t);
    }
  }

  const struct acpi_header *root = read_table(boot, physical);
  const char *signature = *entry_bytes == sizeof(uint64_t) ? "XSDT" : "RSDT";
  if (memcmp(root->signature, signature, 4) ||
      (root->length - sizeof(*root)) % *entry_bytes) {
    panic("invalid ACPI root table");
  }
  return root;
}

static const struct madt_entry *madt_entry_at(const struct acpi_madt *madt,
                                             size_t offset)
{
  const struct madt_entry *entry = (const void *)((const uint8_t *)madt + offset);
  if (madt->header.length - offset < sizeof(*entry) ||
      entry->length < sizeof(*entry) || entry->length > madt->header.length - offset) {
    panic("invalid MADT entry extent");
  }
  return entry;
}

enum isa_route_result {
  ISA_ROUTE_FOUND,
  ISA_ROUTE_ABSENT,
  ISA_ROUTE_INVALID,
};

static enum isa_route_result isa_route(const struct acpi_madt *madt, unsigned isa_irq,
                                       struct isa_irq_route *route)
{
  *route = (struct isa_irq_route){.gsi = isa_irq};
  bool overridden = false;
  for (size_t offset = sizeof(*madt); offset < madt->header.length;) {
    const struct madt_entry *entry = madt_entry_at(madt, offset);
    if (entry->type == MADT_IRQ_OVERRIDE) {
      if (entry->length < sizeof(struct madt_irq_override)) {
        panic("truncated MADT interrupt override");
      }
      const struct madt_irq_override *override = (const void *)entry;
      if (override->bus == ISA_BUS && override->source == isa_irq) {
        unsigned polarity = override->flags & INTI_POLARITY_MASK;
        unsigned trigger = (override->flags >> INTI_TRIGGER_SHIFT) & INTI_TRIGGER_MASK;
        if (overridden ||
            (polarity != INTI_CONFORMS && polarity != INTI_HIGH_OR_EDGE &&
             polarity != INTI_LOW_OR_LEVEL) ||
            (trigger != INTI_CONFORMS && trigger != INTI_HIGH_OR_EDGE &&
             trigger != INTI_LOW_OR_LEVEL)) {
          *route = (struct isa_irq_route){0};
          return ISA_ROUTE_INVALID;
        }
        /* ISA defaults are active-high and edge-triggered. */
        route->gsi = override->gsi;
        route->active_low = polarity == INTI_LOW_OR_LEVEL;
        route->level_triggered = trigger == INTI_LOW_OR_LEVEL;
        overridden = true;
      }
    }
    offset += entry->length;
  }

  /* Find the controller whose GSI range can contain the input. Its actual
   * upper bound comes from its version register after the MMIO page is mapped. */
  for (size_t offset = sizeof(*madt); offset < madt->header.length;) {
    const struct madt_entry *entry = madt_entry_at(madt, offset);
    if (entry->type == MADT_IO_APIC) {
      if (entry->length < sizeof(struct madt_io_apic)) {
        panic("truncated MADT I/O APIC entry");
      }
      const struct madt_io_apic *controller = (const void *)entry;
      if (controller->gsi_base <= route->gsi &&
          (!route->io_apic_physical || controller->gsi_base > route->gsi_base)) {
        route->io_apic_physical = controller->address;
        route->gsi_base = controller->gsi_base;
      }
    }
    offset += entry->length;
  }
  return route->io_apic_physical ? ISA_ROUTE_FOUND : ISA_ROUTE_ABSENT;
}

bool acpi_ps2_routes(const struct boot_info *boot, struct isa_irq_route *keyboard,
                     struct isa_irq_route *mouse)
{
  *keyboard = *mouse = (struct isa_irq_route){0};
  if (!boot->acpi_rsdp) {
    return false;
  }

  size_t entry_bytes;
  const struct acpi_header *root = root_table(boot, &entry_bytes);
  const struct acpi_madt *madt = NULL;
  for (size_t offset = sizeof(*root); offset < root->length; offset += entry_bytes) {
    uint64_t physical = 0;
    memcpy(&physical, (const uint8_t *)root + offset, entry_bytes);
    const struct acpi_header *table = read_table(boot, physical);
    if (!memcmp(table->signature, "APIC", 4)) {
      if (madt || table->length < sizeof(*madt)) {
        panic("invalid or duplicate MADT");
      }
      madt = (const void *)table;
    } else if (!memcmp(table->signature, "FACP", 4) &&
               table->revision >= FADT_BOOT_ARCH_REVISION) {
      uint16_t boot_arch;
      if (table->length < FADT_BOOT_ARCH_OFFSET + sizeof(boot_arch)) {
        panic("truncated FADT boot architecture flags");
      }
      memcpy(&boot_arch, (const uint8_t *)table + FADT_BOOT_ARCH_OFFSET, sizeof(boot_arch));
      if (!(boot_arch & FADT_HAS_8042)) {
        return false;
      }
    }
  }
  if (!madt) {
    return false;
  }

  switch (isa_route(madt, KEYBOARD_ISA_IRQ, keyboard)) {
  case ISA_ROUTE_INVALID:
    panic("invalid MADT keyboard interrupt override");
  case ISA_ROUTE_ABSENT:
    return false;
  case ISA_ROUTE_FOUND:
    break;
  }
  /* The mouse is optional: a bad IRQ 12 description leaves only it unusable. */
  if (isa_route(madt, MOUSE_ISA_IRQ, mouse) == ISA_ROUTE_INVALID) {
    klog("mouse: invalid MADT IRQ 12 override; mouse unavailable\n");
  }
  return true;
}

uint64_t acpi_hpet_address(const struct boot_info *boot)
{
  if (!boot->acpi_rsdp) {
    panic("HPET requires ACPI tables");
  }

  /* HPET table: header, block ID, then a Generic Address Structure. Only the
   * system-memory register block is supported; no firmware alias is retained. */
  struct hpet_table {
    struct acpi_header header;
    uint32_t block_id;
    uint8_t address_space, bit_width, bit_offset, access_size;
    uint64_t address;
    uint8_t number;
    uint16_t minimum_tick;
    uint8_t page_protection;
  } __attribute__((packed));
  enum { ACPI_SYSTEM_MEMORY = 0 };

  size_t entry_bytes;
  const struct acpi_header *root = root_table(boot, &entry_bytes);
  for (size_t offset = sizeof(*root); offset < root->length; offset += entry_bytes) {
    uint64_t physical = 0;
    memcpy(&physical, (const uint8_t *)root + offset, entry_bytes);
    const struct acpi_header *table = read_table(boot, physical);
    if (memcmp(table->signature, "HPET", 4)) {
      continue;
    }
    if (table->length < sizeof(struct hpet_table)) {
      panic("truncated HPET table");
    }
    const struct hpet_table *hpet = (const void *)table;
    if (hpet->address_space != ACPI_SYSTEM_MEMORY || hpet->bit_offset || !hpet->address) {
      panic("unsupported HPET register address");
    }
    return hpet->address;
  }
  panic("no ACPI HPET table");
}

bool acpi_pci_ecam(const struct boot_info *boot, struct pci_ecam *ecam)
{
  struct mcfg_allocation {
    uint64_t base;
    uint16_t segment;
    uint8_t first_bus, last_bus;
    uint32_t reserved;
  } __attribute__((packed));
  struct mcfg_table {
    struct acpi_header header;
    uint64_t reserved;
    struct mcfg_allocation allocations[];
  } __attribute__((packed));

  *ecam = (struct pci_ecam){0};
  if (!boot->acpi_rsdp) {
    klog("PCI: no ACPI tables; discovery disabled\n");
    return false;
  }

  size_t entry_bytes;
  const struct acpi_header *root = root_table(boot, &entry_bytes);
  const struct acpi_header *mcfg = NULL;
  for (size_t offset = sizeof(*root); offset < root->length; offset += entry_bytes) {
    uint64_t physical = 0;
    memcpy(&physical, (const uint8_t *)root + offset, entry_bytes);
    const struct acpi_header *table = read_table(boot, physical);
    if (!memcmp(table->signature, "MCFG", 4)) {
      if (mcfg) {
        klog("PCI: duplicate MCFG tables; discovery disabled\n");
        return false;
      }
      mcfg = table;
    }
  }

  if (!mcfg) {
    klog("PCI: no MCFG table; discovery disabled\n");
    return false;
  }
  if (mcfg->length != sizeof(struct mcfg_table) + sizeof(struct mcfg_allocation)) {
    klog("PCI: MCFG must contain exactly one aperture; discovery disabled\n");
    return false;
  }

  const struct mcfg_table *table = (const void *)mcfg;
  const struct mcfg_allocation *allocation = &table->allocations[0];
  if (allocation->segment || allocation->first_bus || !allocation->base ||
      allocation->base % PCI_ECAM_BUS_BYTES) {
    klog("PCI: unsupported MCFG base/segment/bus range; discovery disabled\n");
    return false;
  }

  /* MCFG's base is relative to bus zero, even for ranges starting elsewhere.
   * This first platform slice explicitly supports only a range starting at 0. */
  ecam->physical = allocation->base;
  ecam->bus_count = (unsigned)allocation->last_bus + 1;
  return true;
}

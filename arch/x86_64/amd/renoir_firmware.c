#include <arch/acpi.h>
#include <arch/amd/renoir_firmware.h>
#include <arch/amd/renoir_inventory.h>
#include <kernel/log.h>

#define RENOIR_VENDOR_ID 0x1002u
#define RENOIR_DEVICE_ID 0x1636u
#define VFCT_MAX_IMAGES 4
#define VFCT_HEADER_BYTES 76u
#define VFCT_VBIOS_OFFSET 52u
#define VFCT_LIBRARY_OFFSET 56u
#define VFCT_IMAGE_HEADER_BYTES 28u
#define VFCT_IMAGE_VENDOR_OFFSET 12u
#define VFCT_IMAGE_DEVICE_OFFSET 14u
#define VFCT_IMAGE_REVISION_OFFSET 20u
#define VFCT_IMAGE_LENGTH_OFFSET 24u
#define ATOM_ROM_POINTER_OFFSET 0x48u
#define ATOM_ROM_MIN_BYTES 36u
#define ATOM_ROM_DATA_OFFSET 32u
#define ATOM_COMMON_HEADER_BYTES 4u
#define ATOM_MASTER_FIRMWARE_INDEX 4u
#define ATOM_MASTER_VRAM_INDEX 11u
#define ATOM_MASTER_MIN_BYTES 28u
#define ATOM_VRAM_ADDRESS_MASK 0x3fffffffu
#define ATOM_VRAM_FLAGS_SHIFT 30u
#define ATOM_VRAM_NO_RESERVATION 1u
#define ATOM_VRAM_SRIOV_RESERVATION 2u
#define ATOM_FIRMWARE_V3_1_BYTES 72u
#define ATOM_FIRMWARE_V3_4_BYTES 108u
#define ATOM_FIRMWARE_V3_5_BYTES 172u
#define ATOM_FIRMWARE_RESERVED_OFFSET 84u
#define ATOM_FIRMWARE_PROTECTED_OFFSET 20u

/* Numeric layouts: Linux v6.19.10 drivers/gpu/drm/amd/include/atomfirmware.h
 * (ROM/master tables, firmware_info, vram_usagebyfirmware, VFCT), atombios.h
 * (VRAM flags), and amdgpu/amdgpu_bios.c (VFCT image matching). These bytes
 * are firmware metadata, not an allocator handoff or a GPU address mapping. */
enum metadata_state {
  METADATA_UNKNOWN,
  METADATA_OK,
  METADATA_MISSING,
  METADATA_MALFORMED,
  METADATA_UNSUPPORTED,
};

struct table_metadata {
  enum metadata_state state;
  uint16_t bytes;
  uint8_t format, content;
};

struct vram_metadata {
  struct table_metadata table;
  uint32_t firmware_start_kb, firmware_size_kb;
  uint32_t driver_start_kb, driver_size_kb;
  uint8_t firmware_flags, driver_flags;
  bool driver_start_known, driver_flags_known;
};

struct firmware_metadata {
  struct table_metadata table;
  uint32_t revision, capability;
  uint64_t mc_base;
  uint32_t reserved_kb, protected_kb;
  bool reserved_known, protected_known;
};

struct image_metadata {
  struct pci_address address;
  uint32_t bytes, revision;
  struct table_metadata rom, master;
  struct vram_metadata vram;
  struct firmware_metadata firmware;
};

static struct {
  bool captured, image_limit;
  enum metadata_state state;
  unsigned scanned, count;
  struct image_metadata images[VFCT_MAX_IMAGES];
} inventory;

static uint16_t read_le16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t read_le32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
    (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool fits(size_t total, size_t offset, size_t bytes)
{
  return offset <= total && bytes <= total - offset;
}

static const uint8_t *table_at(const uint8_t *image, size_t bytes,
                             uint16_t offset, struct table_metadata *table)
{
  table->state = METADATA_MISSING;
  if (!offset) {
    return NULL;
  }
  table->state = METADATA_MALFORMED;
  if (!fits(bytes, offset, ATOM_COMMON_HEADER_BYTES)) {
    return NULL;
  }
  const uint8_t *header = image + offset;
  table->bytes = read_le16(header);
  table->format = header[2];
  table->content = header[3];
  if (table->bytes < ATOM_COMMON_HEADER_BYTES || !fits(bytes, offset, table->bytes)) {
    return NULL;
  }
  table->state = METADATA_OK;
  return header;
}

static void parse_vram(const uint8_t *table, struct vram_metadata *vram)
{
  struct table_metadata *metadata = &vram->table;
  if (metadata->format != 2 || (metadata->content != 1 && metadata->content != 2)) {
    metadata->state = METADATA_UNSUPPORTED;
    return;
  }
  size_t needed = metadata->content == 1 ? 12 : 48;
  if (metadata->bytes < needed) {
    metadata->state = METADATA_MALFORMED;
    return;
  }
  uint32_t firmware_address = read_le32(table + 4);
  vram->firmware_start_kb = firmware_address & ATOM_VRAM_ADDRESS_MASK;
  vram->firmware_flags = firmware_address >> ATOM_VRAM_FLAGS_SHIFT;
  vram->firmware_size_kb = read_le16(table + 8);
  if (vram->firmware_flags > ATOM_VRAM_SRIOV_RESERVATION ||
      (uint64_t)vram->firmware_start_kb + vram->firmware_size_kb >
        (uint64_t)ATOM_VRAM_ADDRESS_MASK + 1) {
    metadata->state = METADATA_MALFORMED;
    return;
  }
  if (metadata->content == 1) {
    vram->driver_size_kb = read_le16(table + 10);
    if (!vram->firmware_flags && vram->driver_size_kb <= vram->firmware_start_kb) {
      vram->driver_start_kb = vram->firmware_start_kb - vram->driver_size_kb;
      vram->driver_start_known = true;
    }
    return;
  }
  uint32_t driver_address = read_le32(table + 12);
  vram->driver_start_kb = driver_address & ATOM_VRAM_ADDRESS_MASK;
  vram->driver_flags = driver_address >> ATOM_VRAM_FLAGS_SHIFT;
  vram->driver_size_kb = read_le32(table + 16);
  if (vram->driver_flags > ATOM_VRAM_SRIOV_RESERVATION ||
      (uint64_t)vram->driver_start_kb + vram->driver_size_kb >
        (uint64_t)ATOM_VRAM_ADDRESS_MASK + 1) {
    metadata->state = METADATA_MALFORMED;
    return;
  }
  vram->driver_start_known = true;
  vram->driver_flags_known = true;
}

static void parse_firmware(const uint8_t *table, struct firmware_metadata *firmware)
{
  struct table_metadata *metadata = &firmware->table;
  if (metadata->format != 3 || metadata->content > 5) {
    metadata->state = METADATA_UNSUPPORTED;
    return;
  }
  size_t needed = ATOM_FIRMWARE_V3_1_BYTES;
  if (metadata->content == 4) {
    needed = ATOM_FIRMWARE_V3_4_BYTES;
  } else if (metadata->content == 5) {
    needed = ATOM_FIRMWARE_V3_5_BYTES;
  }
  if (metadata->bytes < needed) {
    metadata->state = METADATA_MALFORMED;
    return;
  }
  firmware->revision = read_le32(table + 4);
  firmware->capability = read_le32(table + 16);
  firmware->mc_base = (uint64_t)read_le32(table + 40) << 32 | read_le32(table + 44);
  if (metadata->content >= 4) {
    firmware->reserved_kb = read_le32(table + ATOM_FIRMWARE_RESERVED_OFFSET);
    firmware->reserved_known = true;
  }
  if (metadata->content == 5) {
    firmware->protected_kb = read_le32(table + ATOM_FIRMWARE_PROTECTED_OFFSET);
    firmware->protected_known = true;
  }
}

static void parse_atom(const uint8_t *image, struct image_metadata *metadata)
{
  metadata->rom.state = METADATA_MALFORMED;
  if (!fits(metadata->bytes, 0, ATOM_ROM_POINTER_OFFSET + 2) ||
      image[0] != 0x55 || image[1] != 0xaa) {
    return;
  }
  uint16_t rom_offset = read_le16(image + ATOM_ROM_POINTER_OFFSET);
  const uint8_t *rom = table_at(image, metadata->bytes, rom_offset, &metadata->rom);
  if (!rom) {
    return;
  }
  if (metadata->rom.bytes < ATOM_ROM_MIN_BYTES ||
      rom[4] != 'A' || rom[5] != 'T' || rom[6] != 'O' || rom[7] != 'M') {
    metadata->rom.state = METADATA_MALFORMED;
    return;
  }
  if (!((metadata->rom.format == 1 && metadata->rom.content == 1) ||
        (metadata->rom.format == 2 &&
         (metadata->rom.content == 1 || metadata->rom.content == 2)))) {
    metadata->rom.state = METADATA_UNSUPPORTED;
    return;
  }
  if (metadata->rom.format == 2 && metadata->rom.bytes < 40) {
    metadata->rom.state = METADATA_MALFORMED;
    return;
  }
  uint16_t master_offset = read_le16(rom + ATOM_ROM_DATA_OFFSET);
  const uint8_t *master = table_at(image, metadata->bytes, master_offset, &metadata->master);
  if (!master) {
    return;
  }
  if (metadata->master.format != 2 || metadata->master.content != 1) {
    metadata->master.state = METADATA_UNSUPPORTED;
    return;
  }
  if (metadata->master.bytes < ATOM_MASTER_MIN_BYTES) {
    metadata->master.state = METADATA_MALFORMED;
    return;
  }
  uint16_t firmware_offset = read_le16(master + ATOM_COMMON_HEADER_BYTES +
    2 * ATOM_MASTER_FIRMWARE_INDEX);
  uint16_t vram_offset = read_le16(master + ATOM_COMMON_HEADER_BYTES +
    2 * ATOM_MASTER_VRAM_INDEX);
  const uint8_t *firmware = table_at(image, metadata->bytes, firmware_offset,
    &metadata->firmware.table);
  if (firmware) {
    parse_firmware(firmware, &metadata->firmware);
  }
  const uint8_t *vram = table_at(image, metadata->bytes, vram_offset, &metadata->vram.table);
  if (vram) {
    parse_vram(vram, &metadata->vram);
  }
}

void renoir_inventory_boot(const struct boot_info *boot)
{
  if (inventory.captured) {
    return;
  }
  inventory.captured = true;
  inventory.state = METADATA_UNKNOWN;
  size_t bytes = 0;
  const uint8_t *vfct = acpi_boot_table(boot, "VFCT", &bytes);
  if (!vfct) {
    return;
  }
  inventory.state = METADATA_MALFORMED;
  if (bytes < VFCT_HEADER_BYTES) {
    return;
  }
  size_t offset = read_le32(vfct + VFCT_VBIOS_OFFSET);
  size_t end = bytes;
  size_t library_offset = read_le32(vfct + VFCT_LIBRARY_OFFSET);
  if (!offset) {
    inventory.state = METADATA_MISSING;
    return;
  }
  if (offset < VFCT_HEADER_BYTES || offset >= bytes) {
    return;
  }
  if (library_offset) {
    if (library_offset <= offset || library_offset > bytes) {
      return;
    }
    end = library_offset;
  }
  while (offset < end) {
    if (inventory.scanned == VFCT_MAX_IMAGES) {
      inventory.image_limit = true;
      inventory.state = METADATA_UNSUPPORTED;
      return;
    }
    if (!fits(end, offset, VFCT_IMAGE_HEADER_BYTES)) {
      return;
    }
    const uint8_t *header = vfct + offset;
    uint32_t image_bytes = read_le32(header + VFCT_IMAGE_LENGTH_OFFSET);
    offset += VFCT_IMAGE_HEADER_BYTES;
    if (!fits(end, offset, image_bytes)) {
      return;
    }
    ++inventory.scanned;
    if (image_bytes && read_le16(header + VFCT_IMAGE_VENDOR_OFFSET) == RENOIR_VENDOR_ID &&
        read_le16(header + VFCT_IMAGE_DEVICE_OFFSET) == RENOIR_DEVICE_ID) {
      uint32_t bus = read_le32(header);
      uint32_t device = read_le32(header + 4);
      uint32_t function = read_le32(header + 8);
      if (bus >= PCI_BUS_COUNT || device >= PCI_DEVICE_COUNT || function >= PCI_FUNCTION_COUNT) {
        return;
      }
      struct image_metadata *metadata = &inventory.images[inventory.count++];
      metadata->address = (struct pci_address){bus, device, function};
      metadata->bytes = image_bytes;
      metadata->revision = read_le32(header + VFCT_IMAGE_REVISION_OFFSET);
      parse_atom(vfct + offset, metadata);
    }
    offset += image_bytes;
  }
  inventory.state = METADATA_OK;
}

static const char *state_name(enum metadata_state state)
{
  switch (state) {
    case METADATA_OK: return "known";
    case METADATA_MISSING: return "missing";
    case METADATA_MALFORMED: return "malformed";
    case METADATA_UNSUPPORTED: return "unsupported";
    default: return "unknown";
  }
}

static void log_table(const char *name, struct table_metadata table)
{
  klog("renoir-inventory: %s metadata=%s state=%s format=%u content=%u bytes=%u\n", name,
    table.state == METADATA_OK ? "known" : "unknown", state_name(table.state),
    (unsigned)table.format, (unsigned)table.content,
    (unsigned)table.bytes);
}

void renoir_inventory_firmware(struct pci_address address)
{
  if (!inventory.captured || inventory.state != METADATA_OK) {
    klog("renoir-inventory: VFCT metadata unknown state=%s image_limit=%u\n",
      state_name(inventory.state), (unsigned)inventory.image_limit);
    return;
  }
  const struct image_metadata *image = NULL;
  unsigned matches = 0;
  for (unsigned i = 0; i < inventory.count; ++i) {
    const struct image_metadata *candidate = &inventory.images[i];
    if (candidate->address.bus == address.bus && candidate->address.device == address.device &&
        candidate->address.function == address.function) {
      image = candidate;
      ++matches;
    }
  }
  if (matches != 1) {
    klog("renoir-inventory: VFCT metadata unknown BDF=%x:%x.%u matches=%u\n",
      (unsigned)address.bus, (unsigned)address.device, (unsigned)address.function, matches);
    return;
  }
  klog("renoir-inventory: VFCT BDF=%x:%x.%u images=%u image_bytes=%u revision=%x\n",
    (unsigned)address.bus, (unsigned)address.device, (unsigned)address.function,
    inventory.scanned, image->bytes, image->revision);
  log_table("ATOM ROM", image->rom);
  log_table("ATOM master", image->master);
  log_table("firmware_info", image->firmware.table);
  if (image->firmware.table.state == METADATA_OK) {
    const struct firmware_metadata *firmware = &image->firmware;
    klog("renoir-inventory: firmware revision=%x capability=%x mc_base=%lx\n",
      firmware->revision, firmware->capability, firmware->mc_base);
    if (firmware->reserved_known) {
      klog("renoir-inventory: firmware extra_reserved_kb=%u\n", firmware->reserved_kb);
    } else {
      klog("renoir-inventory: firmware extra_reserved_kb=unknown\n");
    }
    if (firmware->protected_known) {
      klog("renoir-inventory: firmware protected_region_kb=%u\n", firmware->protected_kb);
    }
  }
  log_table("vram_usagebyfirmware", image->vram.table);
  if (image->vram.table.state == METADATA_OK) {
    const struct vram_metadata *vram = &image->vram;
    klog("renoir-inventory: firmware_region start_kb=%u size_kb=%u flags=%u\n",
      vram->firmware_start_kb, vram->firmware_size_kb, (unsigned)vram->firmware_flags);
    klog("renoir-inventory: driver_region size_kb=%u start_known=%u start_kb=%u "
         "flags_known=%u flags=%u\n", vram->driver_size_kb,
      (unsigned)vram->driver_start_known, vram->driver_start_kb,
      (unsigned)vram->driver_flags_known, (unsigned)vram->driver_flags);
    if (vram->firmware_flags == ATOM_VRAM_NO_RESERVATION ||
        (vram->driver_flags_known && vram->driver_flags == ATOM_VRAM_NO_RESERVATION)) {
      klog("renoir-inventory: ATOM no-reservation flag is metadata, not free-memory proof\n");
    }
  }
  klog("renoir-inventory: host ATOM scratch is not VRAM; allocator ownership unknown\n");
}

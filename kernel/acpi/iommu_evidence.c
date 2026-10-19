#include <arch/cpu.h>
#include <arch/paging.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include "host.h"

/* Temporary, read-only evidence. AMD 48882, sections 3.2.2, 3.4 and 5.2:
 * IVHD supplies the MMIO base; every supported aperture covers at least 16 KiB.
 * No device table, event buffer or DMA pointer is followed. */
#define IVRS_HEADER_BYTES 48u
#define IVRS_BYTES_LIMIT 65536u
#define IVHD_LIMIT 8u
#define IOMMU_MIN_APERTURE UINT64_C(0x4000)
#define IOMMU_DEVICE_TABLE 0x00u
#define IOMMU_EVENT_LOG 0x10u
#define IOMMU_CONTROL 0x18u
#define IOMMU_EXCLUSION_BASE 0x20u
#define IOMMU_EXCLUSION_LIMIT 0x28u
#define IOMMU_EVIDENCE_BYTES 0x30u
#define IOMMU_ENABLE UINT64_C(1)
#define PAGE_WRITE_THROUGH (UINT64_C(1) << 3)
#define PAGE_CACHE_DISABLE (UINT64_C(1) << 4)
#define PAGE_PAT_4K (UINT64_C(1) << 7)

struct iommu_evidence_unit {
  uint64_t base;
  uint16_t device, segment;
  uint8_t type;
};

static uint16_t load16(const uint8_t *bytes)
{
  uint16_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

static uint64_t load64(const uint8_t *bytes)
{
  uint64_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

static void read_unit(const struct iommu_evidence_unit *unit)
{
  volatile uint64_t *registers = acpi_map(unit->base, IOMMU_EVIDENCE_BYTES);
  uint64_t leaf = 0;
  uint64_t flags = cpu_save_interrupts();
  bool mapped = registers && arch_kernel_page_evidence((uintptr_t)registers, &leaf);
  cpu_restore_interrupts(flags);
  /* Native PAT slot 3 is UC. Refuse a cached firmware-table mapping. */
  uint64_t cache_bits = PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE;
  if (!mapped || (leaf & (cache_bits | PAGE_PAT_4K)) != cache_bits) {
    kinfo("wifi: IOMMU evidence base=%lx mapping unknown/refused leaf=%lx\n",
          unit->base, leaf);
  } else {
    uint64_t control = registers[IOMMU_CONTROL / sizeof(uint64_t)];
    kinfo("wifi: IOMMU evidence IVHD=%x segment=%x device=%x base=%lx control=%lx enabled=%s\n",
          (unsigned)unit->type, (unsigned)unit->segment, (unsigned)unit->device,
          unit->base, control, control == UINT64_MAX ? "unknown" :
          (control & IOMMU_ENABLE ? "yes" : "no"));
    kinfo("wifi: IOMMU evidence device-table=%lx event-log=%lx raw-20=%lx raw-28=%lx\n",
          registers[IOMMU_DEVICE_TABLE / sizeof(uint64_t)],
          registers[IOMMU_EVENT_LOG / sizeof(uint64_t)],
          registers[IOMMU_EXCLUSION_BASE / sizeof(uint64_t)],
          registers[IOMMU_EXCLUSION_LIMIT / sizeof(uint64_t)]);
  }
  if (registers) {
    acpi_unmap((void *)registers, IOMMU_EVIDENCE_BYTES);
  }
}

void acpi_iommu_evidence(void)
{
  acpi_require_worker();
  uacpi_table table;
  uacpi_status status = uacpi_table_find_by_signature("IVRS", &table);
  if (status != UACPI_STATUS_OK) {
    kinfo("wifi: IOMMU evidence IVRS %s status=%u\n",
          status == UACPI_STATUS_NOT_FOUND ? "absent" : "unknown", (unsigned)status);
    return;
  }
  const uint8_t *bytes = table.ptr;
  uint32_t length = table.hdr->length;
  struct iommu_evidence_unit units[IVHD_LIMIT];
  unsigned count = 0;
  if (length < IVRS_HEADER_BYTES || length > IVRS_BYTES_LIMIT ||
      (table.hdr->revision != 1 && table.hdr->revision != 2) ||
      bytes[0] != 'I' || bytes[1] != 'V' || bytes[2] != 'R' || bytes[3] != 'S' ||
      load64(bytes + 40) != 0) {
    goto invalid;
  }
  uint8_t checksum = 0;
  for (uint32_t i = 0; i < length; ++i) {
    checksum += bytes[i];
  }
  if (checksum) {
    goto invalid;
  }
  /* Validate the whole bounded walk before touching any register. */
  for (uint32_t offset = IVRS_HEADER_BYTES; offset < length;) {
    if (length - offset < 4) {
      goto invalid;
    }
    const uint8_t *block = bytes + offset;
    uint16_t size = load16(block + 2);
    if (size < 4 || size > length - offset) {
      goto invalid;
    }
    uint8_t type = block[0];
    if (type == 0x10 || type == 0x11 || type == 0x40) {
      unsigned header = type == 0x10 ? 24 : 40;
      if (size < header || (type == 0x40 && table.hdr->revision != 2)) {
        goto invalid;
      }
      uint64_t base = load64(block + 8);
      if (!base || (base & (IOMMU_MIN_APERTURE - 1)) ||
          base > UINT64_MAX - IOMMU_MIN_APERTURE) {
        goto invalid;
      }
      unsigned i = 0;
      while (i < count && units[i].base != base) {
        ++i;
      }
      if (i == count) {
        if (count == IVHD_LIMIT) {
          goto invalid;
        }
        units[count++] = (struct iommu_evidence_unit){
          .base = base, .device = load16(block + 4),
          .segment = load16(block + 16), .type = type,
        };
      }
    } else if (type != 0x20 && type != 0x21 && type != 0x22) {
      goto invalid;
    }
    offset += size;
  }
  kinfo("wifi: IOMMU evidence IVRS revision=%u bytes=%u units=%u; device policy unknown\n",
        (unsigned)table.hdr->revision, length, count);
  uacpi_table_unref(&table);
  for (unsigned i = 0; i < count; ++i) {
    read_unit(&units[i]);
  }
  return;
invalid:
  kinfo("wifi: IOMMU evidence IVRS unknown: invalid/unsupported table or bound exceeded\n");
  uacpi_table_unref(&table);
}

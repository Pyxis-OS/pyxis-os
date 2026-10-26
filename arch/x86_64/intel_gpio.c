/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/cpu.h>
#include <arch/intel_gpio.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/acpi/gpio.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

#define GPIO_MAPPING_PAGES 3
#define GPIO_REVID 0x000
#define GPIO_REVID_SHIFT 16
#define GPIO_DEBOUNCE_REVID 0x94
#define GPIO_PADBAR 0x00c
#define GPIO_PADCFG_MODE_MASK (UINT32_C(0xf) << 10)
#define GPIO_PADCFG_RX_DISABLE (UINT32_C(1) << 9)
#define GPIO_PADCFG_TX_DISABLE (UINT32_C(1) << 8)
#define GPIO_PADCFG_RX_STATE (UINT32_C(1) << 1)
#define GPIO_PADOWN_MASK UINT32_C(0xf)
#define GPIO_PADOWN_BITS 4
#define GPIO_PADOWN_PINS_PER_WORD 8
#define GPIO_MAX_GROUP_PINS 32

struct gpio_input {
  struct acpi_gpio_input firmware;
  uintptr_t padcfg, padown;
  uint32_t padown_shift;
};

/* Boot prepares storage; one BSP attachment fills fresh virtual addresses.
 * The returned input and its mapping never change after publication. */
static struct boot_region regions[BOOT_MAX_REGIONS];
static size_t region_count;
static uint64_t framebuffer_physical, framebuffer_bytes;
static struct vm_kernel_window window;
static size_t page_count;
static phys_addr_t mapped_pages[GPIO_MAPPING_PAGES];
static struct gpio_input input;
static bool attempted, mapping_failed;

static bool overlaps(uint64_t first, uint64_t bytes, uint64_t other, uint64_t length)
{
  return length && (length > UINT64_MAX - other ||
      (first < other + length && other < first + bytes));
}

void intel_gpio_input_prepare(const struct boot_info *boot)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!window.base && !attempted);
  if (boot->region_count > BOOT_MAX_REGIONS) {
    kinfo("gpio-intel: preparation refused: boot memory-map capacity\n");
    return;
  }
  memcpy(regions, boot->regions, boot->region_count * sizeof(*regions));
  region_count = boot->region_count;
  framebuffer_physical = boot->framebuffer.physical;
  framebuffer_bytes = boot->framebuffer.size;
  if (vm_kernel_window_prepare(GPIO_MAPPING_PAGES * PAGE_SIZE, PAGE_SIZE,
      &window) != MM_OK) {
    kinfo("gpio-intel: preparation refused: native window reservation\n");
  }
}

static bool page_available(phys_addr_t page)
{
  if (page > UINT64_MAX - PAGE_SIZE || !arch_pci_mmio_available(page, PAGE_SIZE) ||
      overlaps(page, PAGE_SIZE, framebuffer_physical, framebuffer_bytes)) {
    return false;
  }
  for (size_t i = 0; i < region_count; ++i) {
    if (regions[i].type != BOOT_RESERVED &&
        overlaps(page, PAGE_SIZE, regions[i].base, regions[i].length)) {
      return false;
    }
  }
  return true;
}

/* Both a firmware interpreter alias and this native alias use UC MMIO. RAM,
 * firmware-cached memory, ECAM/APIC/HPET and WC scanout aliases are excluded.
 * Failed slots are consumed and never retried, including partial mappings. */
static uintptr_t map_register(const struct acpi_gpio_input *snapshot, uint64_t offset)
{
  if (mapping_failed || (offset & 3) || offset > snapshot->bytes ||
      sizeof(uint32_t) > snapshot->bytes - offset) {
    return 0;
  }
  phys_addr_t physical = snapshot->physical + offset;
  phys_addr_t page = physical & ~(PAGE_SIZE - 1);
  for (size_t i = 0; i < page_count; ++i) {
    if (mapped_pages[i] == page) {
      return window.base + i * PAGE_SIZE + physical - page;
    }
  }
  if (page_count == GPIO_MAPPING_PAGES || !page_available(page)) {
    return 0;
  }
  uintptr_t address;
  mapped_pages[page_count++] = page;
  if (vm_kernel_window_map(&window, page, PAGE_SIZE, true, &address) != MM_OK) {
    mapping_failed = true;
    return 0;
  }
  return address + physical - page;
}

static uint32_t read32(uintptr_t address)
{
  return *(volatile uint32_t *)address;
}

bool gpio_input_level(const struct gpio_input *reader, bool *high)
{
  KASSERT(arch_cpu_index() == 0);
  if (!reader || reader != &input || !reader->padcfg || !reader->padown || !high) {
    return false;
  }
  uint32_t ownership = read32(reader->padown);
  uint32_t configuration = read32(reader->padcfg);
  if (((ownership >> reader->padown_shift) & GPIO_PADOWN_MASK) ||
      configuration == UINT32_MAX ||
      (configuration & (GPIO_PADCFG_MODE_MASK | GPIO_PADCFG_RX_DISABLE)) ||
      !(configuration & GPIO_PADCFG_TX_DISABLE)) {
    return false;
  }
  *high = (configuration & GPIO_PADCFG_RX_STATE) != 0;
  return true;
}

const struct gpio_input *intel_gpio_input_attach(const struct acpi_gpio_input *snapshot)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!window.base || attempted) {
    return NULL;
  }
  attempted = true;
  const char *failure = "firmware resource/layout bounds";
  if (!snapshot || !snapshot->physical || !snapshot->bytes ||
      snapshot->bytes > UINT64_MAX - snapshot->physical || (snapshot->physical & 3) ||
      !snapshot->group_count || snapshot->group_count > ACPI_GPIO_GROUP_CAPACITY ||
      (snapshot->stride != 8 && snapshot->stride != 16) ||
      !snapshot->ownership_base || (snapshot->ownership_base & 3)) {
    goto refused;
  }
  uintptr_t revision_register = map_register(snapshot, GPIO_REVID);
  uintptr_t padbar_register = map_register(snapshot, GPIO_PADBAR);
  if (!revision_register || !padbar_register) {
    failure = "community identity mapping";
    goto refused;
  }
  uint32_t revision = read32(revision_register);
  uint32_t padbar = read32(padbar_register);
  uint32_t stride = (revision >> GPIO_REVID_SHIFT) >= GPIO_DEBOUNCE_REVID ? 16 : 8;
  if (revision == UINT32_MAX || padbar == UINT32_MAX || (padbar & 3) ||
      stride != snapshot->stride || padbar > snapshot->bytes) {
    failure = "firmware pad layout disagrees with REVID/PADBAR";
    goto refused;
  }
  uint32_t pin_count = 0, ownership_word = 0, padown_shift = 0;
  uint64_t padcfg_offset = 0, padown_offset = 0;
  bool found = false;
  for (uint32_t i = 0; i < snapshot->group_count; ++i) {
    const struct acpi_gpio_group *group = &snapshot->groups[i];
    if (!group->count || group->count > GPIO_MAX_GROUP_PINS ||
        (uint64_t)group->offset != (uint64_t)padbar + (uint64_t)pin_count * stride) {
      failure = "firmware groups disagree with the Intel pad layout";
      goto refused;
    }
    if (snapshot->pin >= pin_count && snapshot->pin - pin_count < group->count) {
      uint32_t local_pin = snapshot->pin - pin_count;
      padcfg_offset = (uint64_t)group->offset + (uint64_t)local_pin * stride;
      padown_offset = (uint64_t)snapshot->ownership_base +
          (ownership_word + local_pin / GPIO_PADOWN_PINS_PER_WORD) * sizeof(uint32_t);
      padown_shift = (local_pin % GPIO_PADOWN_PINS_PER_WORD) * GPIO_PADOWN_BITS;
      found = true;
    }
    pin_count += group->count;
    ownership_word += (group->count + GPIO_PADOWN_PINS_PER_WORD - 1) / GPIO_PADOWN_PINS_PER_WORD;
  }
  if (!found || (uint64_t)pin_count * stride > snapshot->bytes - padbar) {
    failure = "pin or pad array exceeds the firmware memory resource";
    goto refused;
  }
  uintptr_t padown = map_register(snapshot, padown_offset);
  uintptr_t padcfg = map_register(snapshot, padcfg_offset);
  if (!padown || !padcfg) {
    failure = "pad register mapping";
    goto refused;
  }
  input = (struct gpio_input){.firmware = *snapshot, .padcfg = padcfg,
      .padown = padown, .padown_shift = padown_shift};
  bool high;
  if (!gpio_input_level(&input, &high)) {
    kinfo("gpio-intel: %s pin=%u padcfg=0x%x ownership=0x%x shift=%u\n",
        snapshot->controller, snapshot->pin, read32(padcfg), read32(padown),
        padown_shift);
    input.padcfg = 0;
    input.padown = 0;
    failure = "pad is not a host-owned enabled GPIO input";
    goto refused;
  }
  kinfo("gpio-intel: %s uid=%llu pin=%u group=%s native input ready; "
      "resource=0x%llx+0x%llx revision=0x%x padcfg=0x%llx padown=0x%llx shift=%u "
      "level=%s; read-only\n",
      snapshot->controller, (unsigned long long)snapshot->uid, snapshot->pin, snapshot->group,
      (unsigned long long)snapshot->physical, (unsigned long long)snapshot->bytes,
      revision, (unsigned long long)padcfg_offset, (unsigned long long)padown_offset,
      padown_shift, high ? "high" : "low");
  return &input;

refused:
  kinfo("gpio-intel: input attachment refused: %s\n", failure);
  return NULL;
}

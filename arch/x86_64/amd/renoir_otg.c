#include <arch/amd/renoir_otg.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>

#define RENOIR_VENDOR_ID 0x1002
#define RENOIR_DEVICE_ID 0x1636
#define RENOIR_DISPLAY_CLASS 0x03
#define RENOIR_REGISTER_BAR 5
#define RENOIR_WINDOW_OFFSET 0x13000u
#define RENOIR_WINDOW_BYTES (2 * PAGE_SIZE)
#define RENOIR_OTG_COUNT 4
#define RENOIR_OTG_STRIDE 0x200u

/* AMD's Linux v6.19 dcn_2_1_0_offset.h/sh_mask.h and renoir_ip_offset.h:
 * DCN segment 2 is 0x34c0 DWORDs; these are BAR-relative byte offsets.
 * dcn21_resource.c creates four OTGs. Never probe generated OTG4/5 entries. */
#define OTG_H_TOTAL 0x13fa8u
#define OTG_H_BLANK_START_END 0x13facu
#define OTG_H_TIMING_CNTL 0x13fb8u
#define OTG_V_TOTAL 0x13fbcu
#define OTG_V_TOTAL_MIN 0x13fc0u
#define OTG_V_TOTAL_MAX 0x13fc4u
#define OTG_V_TOTAL_CONTROL 0x13fccu
#define OTG_V_BLANK_START_END 0x13fd8u
#define OTG_CONTROL 0x14004u
#define OTG_INTERLACE_CONTROL 0x14010u
#define OTG_STATUS 0x14024u
#define OTG_STATUS_POSITION 0x14028u
#define OTG_STATUS_FRAME_COUNT 0x14030u

#define OTG_TIMING_MASK 0x7fffu
#define OTG_HIGH_FIELD_SHIFT 16
#define OTG_BLANK_MASK (OTG_TIMING_MASK | (OTG_TIMING_MASK << OTG_HIGH_FIELD_SHIFT))
#define OTG_MASTER_ENABLE (1u << 0)
#define OTG_INTERLACE_ENABLE (1u << 0)
#define OTG_H_DIV_BY2 (1u << 0)
#define OTG_H_DIV_UPDATE_MODE (1u << 8)
#define OTG_V_TOTAL_MIN_SELECT (1u << 0)
#define OTG_V_TOTAL_MAX_SELECT (1u << 1)
#define OTG_V_TOTAL_MID_REPLACE_MAX (1u << 2)
#define OTG_V_TOTAL_MID_REPLACE_MIN (1u << 3)
#define OTG_V_TOTAL_MIN_MASK_ENABLE (1u << 7)
#define OTG_V_TOTAL_MODE_MASK (OTG_V_TOTAL_MIN_SELECT | OTG_V_TOTAL_MAX_SELECT | \
  OTG_V_TOTAL_MID_REPLACE_MAX | OTG_V_TOTAL_MID_REPLACE_MIN | OTG_V_TOTAL_MIN_MASK_ENABLE)
#define OTG_V_BLANK (1u << 0)
#define OTG_FRAME_MASK 0xffffffu

static struct {
  struct pci_device *device;
  struct renoir_otg_info info;
  uintptr_t window;
  unsigned power_capability;
  bool attempted, ready;
} observer;
static const char *reason = "not prepared";

const char *renoir_otg_reason(void)
{
  return reason;
}

static bool refuse(const char *text)
{
  reason = text;
  return false;
}

static bool overlap(phys_addr_t first, size_t bytes, phys_addr_t other, uint64_t length)
{
  if (!length) {
    return false;
  }
  if (length > UINT64_MAX - other) {
    return true;
  }
  return first < other + length && other < first + bytes;
}

static bool select_device(void)
{
  if (pci_select_device(RENOIR_VENDOR_ID, RENOIR_DEVICE_ID, &observer.device) !=
      PCI_SELECTION_UNIQUE) {
    return refuse("Renoir device absent, ambiguous or inventory incomplete");
  }
  unsigned display_count = 0;
  for (const struct pci_device *device = pci_device_at(0); device; device = device->next) {
    display_count += device->base_class == RENOIR_DISPLAY_CLASS;
  }
  if (display_count != 1 || observer.device->base_class != RENOIR_DISPLAY_CLASS ||
      observer.device->header_type != PCI_HEADER_ENDPOINT || observer.device->owner) {
    return refuse("display endpoint is ambiguous, unsupported or already owned");
  }
  return true;
}

static bool find_power_capability(void)
{
  struct pci_address address = observer.device->address;
  if (!(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return refuse("D0 cannot be verified without a power capability");
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  unsigned count = 0;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES] ||
        ++count > PCI_CAP_COUNT) {
      return refuse("invalid PCI capability chain");
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    if (pci_read8(address, offset + PCI_CAP_ID) == PCI_CAP_POWER) {
      if (observer.power_capability || offset > PCI_CONVENTIONAL_BYTES - PCI_POWER_BYTES) {
        return refuse("invalid PCI power capability");
      }
      observer.power_capability = offset;
    }
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  if (!observer.power_capability) {
    return refuse("D0 cannot be verified without a power capability");
  }
  for (unsigned body_offset = observer.power_capability + PCI_REGISTER_BYTES;
      body_offset < observer.power_capability + PCI_POWER_BYTES; body_offset += PCI_REGISTER_BYTES) {
    if (seen[body_offset / PCI_REGISTER_BYTES]) {
      return refuse("overlapping PCI power capability");
    }
  }
  return true;
}

static bool device_usable(void)
{
  struct pci_address address = observer.device->address;
  if (observer.device->owner ||
      pci_read16(address, PCI_VENDOR_ID) != RENOIR_VENDOR_ID ||
      pci_read16(address, PCI_DEVICE_ID) != RENOIR_DEVICE_ID) {
    return refuse("Renoir identity or ownership changed");
  }
  if (!(pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MEMORY) ||
      (pci_read16(address, observer.power_capability + PCI_POWER_CONTROL) &
       PCI_POWER_STATE_MASK) != PCI_POWER_D0) {
    return refuse("Renoir memory decode or D0 is unavailable");
  }
  return true;
}

static bool read_bar(phys_addr_t *physical)
{
  struct pci_address address = observer.device->address;
  for (unsigned bar = 0; bar < RENOIR_REGISTER_BAR; ++bar) {
    uint32_t low = pci_read32(address, PCI_BAR_FIRST + bar * PCI_REGISTER_BYTES);
    if (!(low & PCI_BAR_IO) && (low & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_64) {
      if (bar + 1 == RENOIR_REGISTER_BAR) {
        return refuse("BAR5 is an upper BAR half");
      }
      ++bar;
    }
  }
  uint32_t bar = pci_read32(address, PCI_BAR_FIRST + RENOIR_REGISTER_BAR * PCI_REGISTER_BYTES);
  if (bar & (PCI_BAR_IO | PCI_BAR_MEMORY_TYPE_MASK | PCI_BAR_PREFETCHABLE)) {
    return refuse("BAR5 is not the expected non-prefetchable 32-bit memory BAR");
  }
  *physical = bar & PCI_BAR_MEMORY_ADDRESS_MASK;
  if (!*physical || (*physical & (PAGE_SIZE - 1)) ||
      *physical > UINT64_C(0x100000000) - RENOIR_WINDOW_OFFSET - RENOIR_WINDOW_BYTES) {
    return refuse("BAR5 assignment or register extent is invalid");
  }
  return true;
}

static bool window_available(const struct boot_info *boot, phys_addr_t first)
{
  if (!arch_pci_mmio_available(first, RENOIR_WINDOW_BYTES)) {
    return refuse("register window overlaps platform MMIO or a WC display aperture");
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (overlap(first, RENOIR_WINDOW_BYTES, region->base, region->length) &&
        region->type != BOOT_RESERVED) {
      return refuse("register window overlaps RAM or retained boot storage");
    }
  }
  const struct boot_framebuffer *fb = &boot->framebuffer;
  size_t page_offset = fb->physical & (PAGE_SIZE - 1);
  if (fb->size > SIZE_MAX - page_offset - (PAGE_SIZE - 1)) {
    return refuse("GOP framebuffer extent overflows");
  }
  size_t fb_extent = (page_offset + fb->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (overlap(first, RENOIR_WINDOW_BYTES, fb->physical - page_offset, fb_extent)) {
    return refuse("register window overlaps the WC GOP framebuffer");
  }
  for (const struct pci_device *device = pci_device_at(0); device; device = device->next) {
    const struct pci_claim *claim = device->owner;
    if (!claim) {
      continue;
    }
    for (const struct pci_mapping *mapping = claim->mappings; mapping; mapping = mapping->next) {
      for (size_t offset = 0; offset < mapping->bytes; offset += PAGE_SIZE) {
        struct page_translation page;
        if (vm_query(vm_kernel_space(), mapping->base + offset, &page) != MM_OK ||
            overlap(first, RENOIR_WINDOW_BYTES, page.physical, PAGE_SIZE)) {
          return refuse("register window overlaps or cannot exclude another PCI mapping");
        }
      }
    }
  }
  return true;
}

static void unmap_window(uintptr_t base, size_t mapped)
{
  while (mapped) {
    mapped -= PAGE_SIZE;
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), base + mapped, &physical) == MM_OK);
  }
  KASSERT(vm_release(vm_kernel_space(), base, RENOIR_WINDOW_BYTES) == MM_OK);
}

static bool map_window(phys_addr_t physical)
{
  uintptr_t base;
  if (vm_reserve(vm_kernel_space(), RENOIR_WINDOW_BYTES, PAGE_SIZE, &base) != MM_OK) {
    return refuse("cannot reserve register window");
  }
  size_t mapped = 0;
  while (mapped < RENOIR_WINDOW_BYTES) {
    if (vm_map_mmio(base + mapped, physical + mapped) != MM_OK) {
      unmap_window(base, mapped);
      return refuse("cannot map register window UC");
    }
    mapped += PAGE_SIZE;
    if (vm_protect(vm_kernel_space(), base + mapped - PAGE_SIZE, 0) != MM_OK) {
      unmap_window(base, mapped);
      return refuse("cannot protect register window read-only");
    }
  }
  observer.window = base;
  return true;
}

static uint32_t read_register(unsigned otg, unsigned offset)
{
  KASSERT(otg < RENOIR_OTG_COUNT && !(offset % sizeof(uint32_t)));
  size_t within = offset - RENOIR_WINDOW_OFFSET + otg * RENOIR_OTG_STRIDE;
  KASSERT(within <= RENOIR_WINDOW_BYTES - sizeof(uint32_t));
  return *(const volatile uint32_t *)(observer.window + within);
}

static bool read_mode(struct renoir_otg_mode *mode)
{
  *mode = (struct renoir_otg_mode){0};
  unsigned enabled = 0;
  for (unsigned otg = 0; otg < RENOIR_OTG_COUNT; ++otg) {
    uint32_t control = read_register(otg, OTG_CONTROL);
    if (control & OTG_MASTER_ENABLE) {
      ++enabled;
      mode->otg = otg;
      mode->raw_control = control;
    }
  }
  if (enabled != 1) {
    return refuse("exactly one enabled OTG is required");
  }
  unsigned otg = mode->otg;
  mode->raw_h_total = read_register(otg, OTG_H_TOTAL);
  mode->raw_h_blank = read_register(otg, OTG_H_BLANK_START_END);
  mode->raw_h_timing = read_register(otg, OTG_H_TIMING_CNTL);
  mode->raw_v_total = read_register(otg, OTG_V_TOTAL);
  mode->raw_v_blank = read_register(otg, OTG_V_BLANK_START_END);
  mode->raw_v_total_min = read_register(otg, OTG_V_TOTAL_MIN);
  mode->raw_v_total_max = read_register(otg, OTG_V_TOTAL_MAX);
  mode->raw_v_total_control = read_register(otg, OTG_V_TOTAL_CONTROL);
  mode->raw_interlace = read_register(otg, OTG_INTERLACE_CONTROL);
  mode->h_total = (mode->raw_h_total & OTG_TIMING_MASK) + 1;
  mode->v_total = (mode->raw_v_total & OTG_TIMING_MASK) + 1;
  uint32_t h_start = mode->raw_h_blank & OTG_TIMING_MASK;
  uint32_t h_end = (mode->raw_h_blank >> OTG_HIGH_FIELD_SHIFT) & OTG_TIMING_MASK;
  mode->blank_start = mode->raw_v_blank & OTG_TIMING_MASK;
  mode->blank_end = (mode->raw_v_blank >> OTG_HIGH_FIELD_SHIFT) & OTG_TIMING_MASK;
  if ((mode->raw_interlace & OTG_INTERLACE_ENABLE) ||
      h_start >= mode->h_total || h_end >= h_start ||
      mode->blank_start >= mode->v_total || mode->blank_end >= mode->blank_start) {
    return refuse("OTG timing is interlaced or its blank endpoints are unsupported");
  }
  /* AMD optc1_get_otg_active_size uses these differences without DIV_BY2
   * scaling: the programmed geometry remains in full pixel units. */
  mode->h_active = h_start - h_end;
  mode->v_active = mode->blank_start - mode->blank_end;
  uint32_t controls = mode->raw_v_total_control;
  if ((controls & (OTG_V_TOTAL_MID_REPLACE_MAX | OTG_V_TOTAL_MID_REPLACE_MIN |
                   OTG_V_TOTAL_MIN_MASK_ENABLE)) ||
      ((controls & OTG_V_TOTAL_MIN_SELECT) &&
       (mode->raw_v_total_min & OTG_TIMING_MASK) + 1 != mode->v_total) ||
      ((controls & OTG_V_TOTAL_MAX_SELECT) &&
       (mode->raw_v_total_max & OTG_TIMING_MASK) + 1 != mode->v_total)) {
    return refuse("adaptive vertical totals are unsupported");
  }
  return true;
}

static bool same_mode(const struct renoir_otg_mode *first, const struct renoir_otg_mode *second)
{
  return first->otg == second->otg && first->h_total == second->h_total &&
    first->v_total == second->v_total && first->h_active == second->h_active &&
    first->v_active == second->v_active && first->blank_start == second->blank_start &&
    first->blank_end == second->blank_end &&
    !((first->raw_h_blank ^ second->raw_h_blank) & OTG_BLANK_MASK) &&
    !((first->raw_h_timing ^ second->raw_h_timing) & (OTG_H_DIV_BY2 | OTG_H_DIV_UPDATE_MODE)) &&
    !((first->raw_v_total_min ^ second->raw_v_total_min) & OTG_TIMING_MASK) &&
    !((first->raw_v_total_max ^ second->raw_v_total_max) & OTG_TIMING_MASK) &&
    !((first->raw_v_total_control ^ second->raw_v_total_control) & OTG_V_TOTAL_MODE_MASK);
}

bool renoir_otg_prepare(const struct boot_info *boot, struct renoir_otg_info *info)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count() && !observer.attempted && boot && info);
  observer.attempted = true;
  *info = (struct renoir_otg_info){0};
  if (!boot->framebuffer.size || !select_device() || !find_power_capability() || !device_usable()) {
    if (!boot->framebuffer.size) {
      refuse("no GOP framebuffer");
    }
    return false;
  }
  phys_addr_t physical;
  if (!read_bar(&physical) || !window_available(boot, physical + RENOIR_WINDOW_OFFSET) ||
      !map_window(physical + RENOIR_WINDOW_OFFSET)) {
    return false;
  }
  struct renoir_otg_mode first, second;
  bool valid = read_mode(&first) && read_mode(&second);
  if (valid && !same_mode(&first, &second)) {
    valid = refuse("OTG mode changed during preparation");
  }
  if (valid && (first.h_active != boot->framebuffer.width ||
      first.v_active != boot->framebuffer.height || first.h_total <= first.h_active ||
      first.v_total <= first.v_active)) {
    valid = refuse("OTG geometry does not match GOP");
  }
  phys_addr_t final_physical;
  if (valid) {
    valid = device_usable() && read_bar(&final_physical);
    if (valid && final_physical != physical) {
      valid = refuse("BAR5 assignment changed during preparation");
    }
  }
  if (!valid) {
    unmap_window(observer.window, RENOIR_WINDOW_BYTES);
    observer.window = 0;
    return false;
  }
  observer.info = (struct renoir_otg_info){
    .address = observer.device->address, .bar5 = physical, .mode = second,
  };
  *info = observer.info;
  observer.ready = true;
  reason = "available";
  return true;
}

static enum renoir_otg_result unavailable(void)
{
  observer.ready = false;
  return RENOIR_OTG_UNAVAILABLE;
}

enum renoir_otg_result renoir_otg_read(struct renoir_otg_sample *sample)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
  KASSERT(sample);
  *sample = (struct renoir_otg_sample){0};
  if (!observer.ready) {
    return RENOIR_OTG_UNAVAILABLE;
  }
  phys_addr_t physical;
  if (!device_usable() || !read_bar(&physical)) {
    return unavailable();
  }
  if (physical != observer.info.bar5) {
    refuse("BAR5 assignment changed");
    return unavailable();
  }
  struct renoir_otg_mode first, second;
  if (!read_mode(&first)) {
    return unavailable();
  }
  if (!same_mode(&first, &observer.info.mode)) {
    refuse("OTG mode changed");
    return unavailable();
  }
  unsigned otg = first.otg;
  struct renoir_otg_sample current = {.mode = first};
  current.before_ns = arch_monotonic_ns();
  uint32_t frame_first = read_register(otg, OTG_STATUS_FRAME_COUNT);
  uint32_t position_first = read_register(otg, OTG_STATUS_POSITION);
  current.raw_status = read_register(otg, OTG_STATUS);
  current.raw_position = read_register(otg, OTG_STATUS_POSITION);
  current.raw_frame_count = read_register(otg, OTG_STATUS_FRAME_COUNT);
  current.after_ns = arch_monotonic_ns();
  if (!read_mode(&second) || !device_usable() || !read_bar(&physical)) {
    return unavailable();
  }
  if (physical != observer.info.bar5 || !same_mode(&first, &second)) {
    refuse("BAR5 assignment or OTG mode changed during observation");
    return unavailable();
  }
  current.frame_count = current.raw_frame_count & OTG_FRAME_MASK;
  current.v_position = current.raw_position & OTG_TIMING_MASK;
  current.h_position = (current.raw_position >> OTG_HIGH_FIELD_SHIFT) & OTG_TIMING_MASK;
  current.in_blank = (current.raw_status & OTG_V_BLANK) != 0;
  if (current.after_ns < current.before_ns || current.v_position >= first.v_total ||
      current.h_position >= first.h_total) {
    refuse("OTG position or monotonic bracket is invalid");
    return unavailable();
  }
  bool position_blank = current.v_position >= first.blank_start ||
    current.v_position < first.blank_end;
  if ((frame_first & OTG_FRAME_MASK) != current.frame_count ||
      (position_first & OTG_TIMING_MASK) != current.v_position ||
      current.in_blank != position_blank) {
    refuse("OTG counter bracket crossed a line, frame or blank boundary");
    return RENOIR_OTG_RETRY;
  }
  *sample = current;
  reason = "available";
  return RENOIR_OTG_OK;
}

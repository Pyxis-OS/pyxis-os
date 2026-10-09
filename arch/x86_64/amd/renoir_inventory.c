#include "renoir_registers.h"
#include <arch/amd/renoir_inventory.h>
#include <arch/amd/renoir_firmware.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/memory.h>

#define RENOIR_VENDOR 0x1002
#define RENOIR_DEVICE 0x1636
#define RENOIR_BAR 5
#define RENOIR_DISPLAY_CLASS 3
#define DMCUB_CACHE_WINDOWS 8
#define DMCUB_UNCACHED_WINDOWS 2
#define MIB UINT64_C(1048576)

static const uint32_t register_pages[] = {
  0x3000, 0xa000, 0xe000, 0xf000, 0x11000,
  0x12000, 0x13000, 0x14000, 0x19000, 0x6a000,
};
#define REGISTER_PAGE_COUNT (sizeof(register_pages) / sizeof(register_pages[0]))
static uintptr_t windows[REGISTER_PAGE_COUNT];

struct hubp_state {
  uint32_t config, address_config, tiling, viewport_start, viewport_size;
  uint32_t control, clock, pitch, vmid, surface, flip, flip2, crossbar;
  uint32_t aperture_low, aperture_high, tlb;
  uint64_t primary, metadata, inuse, earliest;
};
struct otg_state { uint32_t control, interlace, h_total, h_blank, v_total, v_blank, source, format; };
struct mpcc_state { uint32_t top, bottom, opp, control, status; };
struct dmcub_window { uint32_t base, top, low, high; };
struct inventory_state {
  struct hubp_state hubp[RENOIR_PIPES];
  struct otg_state otg[RENOIR_PIPES];
  struct mpcc_state mpcc[RENOIR_MPCCS];
  uint32_t mux[RENOIR_PIPES];
  uint32_t fb_base, fb_top, fb_offset, gc_offset, mc_base, mc_top, memsize;
  uint32_t context_control, context_base_high, context_base_low;
  uint32_t context_start_high, context_start_low, context_end_high, context_end_low;
  uint32_t dmcub_control, dmcub_security;
  struct dmcub_window cache[DMCUB_CACHE_WINDOWS], uncached[DMCUB_UNCACHED_WINDOWS];
};
static struct inventory_state first, second;

static bool overlaps(uint64_t base, uint64_t bytes, uint64_t other, uint64_t size)
{
  return size && (size > UINT64_MAX - other || bytes > UINT64_MAX - base ||
      (base < other + size && other < base + bytes));
}

static bool page_available(const struct boot_info *boot, phys_addr_t physical)
{
  if (!arch_pci_mmio_available(physical, PAGE_SIZE) ||
      overlaps(physical, PAGE_SIZE, boot->framebuffer.physical, boot->framebuffer.size)) {
    return false;
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->type != BOOT_RESERVED && overlaps(physical, PAGE_SIZE, r->base, r->length)) {
      return false;
    }
  }
  for (const struct pci_device *d = pci_device_at(0); d; d = d->next) {
    if (!d->owner) {
      continue;
    }
    for (const struct pci_mapping *m = d->owner->mappings; m; m = m->next) {
      for (size_t offset = 0; offset < m->bytes; offset += PAGE_SIZE) {
        struct page_translation page;
        if (vm_query(vm_kernel_space(), m->base + offset, &page) != MM_OK ||
            overlaps(physical, PAGE_SIZE, page.physical, PAGE_SIZE)) {
          return false;
        }
      }
    }
  }
  return true;
}

static void unmap_registers(void)
{
  for (size_t i = 0; i < REGISTER_PAGE_COUNT; ++i) {
    if (windows[i]) {
      phys_addr_t physical;
      KASSERT(vm_unmap(vm_kernel_space(), windows[i], &physical) == MM_OK);
      KASSERT(vm_release(vm_kernel_space(), windows[i], PAGE_SIZE) == MM_OK);
      windows[i] = 0;
    }
  }
}

static bool map_registers(const struct boot_info *boot, phys_addr_t bar)
{
  for (size_t i = 0; i < REGISTER_PAGE_COUNT; ++i) {
    uintptr_t base;
    if (!page_available(boot, bar + register_pages[i]) ||
        vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, &base) != MM_OK) {
      unmap_registers();
      return false;
    }
    if (vm_map_mmio(base, bar + register_pages[i]) != MM_OK) {
      KASSERT(vm_release(vm_kernel_space(), base, PAGE_SIZE) == MM_OK);
      unmap_registers();
      return false;
    }
    windows[i] = base;
    if (vm_protect(vm_kernel_space(), base, 0) != MM_OK) {
      unmap_registers();
      return false;
    }
  }
  return true;
}

static uint32_t read_register(uint32_t offset)
{
  KASSERT(!(offset & 3));
  for (size_t i = 0; i < REGISTER_PAGE_COUNT; ++i) {
    if (offset >= register_pages[i] && offset < register_pages[i] + PAGE_SIZE) {
      return *(const volatile uint32_t *)(windows[i] + offset - register_pages[i]);
    }
  }
  panic("Renoir inventory register outside audited pages");
}

static uint64_t read_address(uint32_t low, uint32_t high)
{
  return read_register(low) | ((uint64_t)(read_register(high) & ADDRESS_HIGH_MASK) << 32);
}

static void snapshot(struct inventory_state *s)
{
  *s = (struct inventory_state){0};
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    unsigned h = i * HUBP_STRIDE, t = i * OTG_STRIDE, o = i * ODM_STRIDE;
    s->hubp[i] = (struct hubp_state){
      .config = read_register(HUBP_CONFIG + h), .address_config = read_register(HUBP_ADDRESS_CONFIG + h),
      .tiling = read_register(HUBP_TILING + h), .viewport_start = read_register(HUBP_VIEWPORT_START + h),
      .viewport_size = read_register(HUBP_VIEWPORT_SIZE + h), .control = read_register(HUBP_CONTROL + h),
      .clock = read_register(HUBP_CLOCK + h), .pitch = read_register(HUBP_PITCH + h),
      .vmid = read_register(HUBP_VMID + h), .surface = read_register(HUBP_SURFACE_CONTROL + h),
      .flip = read_register(HUBP_FLIP_CONTROL + h), .flip2 = read_register(HUBP_FLIP_CONTROL2 + h),
      .crossbar = read_register(HUBP_CROSSBAR + h), .aperture_low = read_register(HUBP_APERTURE_LOW + h),
      .aperture_high = read_register(HUBP_APERTURE_HIGH + h), .tlb = read_register(HUBP_TLB + h),
      .primary = read_address(HUBP_PRIMARY_LOW + h, HUBP_PRIMARY_HIGH + h),
      .metadata = read_address(HUBP_METADATA_LOW + h, HUBP_METADATA_HIGH + h),
      .inuse = read_address(HUBP_INUSE_LOW + h, HUBP_INUSE_HIGH + h),
      .earliest = read_address(HUBP_EARLIEST_LOW + h, HUBP_EARLIEST_HIGH + h),
    };
    s->otg[i] = (struct otg_state){
      .control = read_register(OTG_CONTROL + t), .interlace = read_register(OTG_INTERLACE + t),
      .h_total = read_register(OTG_H_TOTAL + t), .h_blank = read_register(OTG_H_BLANK + t),
      .v_total = read_register(OTG_V_TOTAL + t), .v_blank = read_register(OTG_V_BLANK + t),
      .source = read_register(ODM_SOURCE + o), .format = read_register(ODM_FORMAT + o),
    };
    s->mux[i] = read_register(MPC_OUT_MUX + i * MPC_OUT_STRIDE);
  }
  for (unsigned i = 0; i < RENOIR_MPCCS; ++i) {
    unsigned offset = i * MPCC_STRIDE;
    s->mpcc[i] = (struct mpcc_state){
      .top = read_register(MPCC_TOP + offset), .bottom = read_register(MPCC_BOTTOM + offset),
      .opp = read_register(MPCC_OPP + offset), .control = read_register(MPCC_CONTROL + offset),
      .status = read_register(MPCC_STATUS + offset),
    };
  }
  s->fb_base = read_register(DCN_FB_BASE);
  s->fb_top = read_register(DCN_FB_TOP);
  s->fb_offset = read_register(DCN_FB_OFFSET);
  s->gc_offset = read_register(GC_FB_OFFSET);
  s->mc_base = read_register(MMHUB_FB_BASE);
  s->mc_top = read_register(MMHUB_FB_TOP);
  s->memsize = read_register(NBIF_MEMSIZE);
  s->context_control = read_register(DCN_CONTEXT_CONTROL);
  s->context_base_high = read_register(DCN_CONTEXT_BASE_HIGH);
  s->context_base_low = read_register(DCN_CONTEXT_BASE_LOW);
  s->context_start_high = read_register(DCN_CONTEXT_START_HIGH);
  s->context_start_low = read_register(DCN_CONTEXT_START_LOW);
  s->context_end_high = read_register(DCN_CONTEXT_END_HIGH);
  s->context_end_low = read_register(DCN_CONTEXT_END_LOW);
  s->dmcub_control = read_register(DMCUB_CONTROL);
  s->dmcub_security = read_register(DMCUB_SEC_CONTROL);
  for (unsigned i = 0; i < DMCUB_CACHE_WINDOWS; ++i) {
    s->cache[i] = (struct dmcub_window){
      .base = read_register(DMCUB_CW_BASE + i * 4), .top = read_register(DMCUB_CW_TOP + i * 4),
      .low = read_register(DMCUB_CW_OFFSET_LOW + i * 8), .high = read_register(DMCUB_CW_OFFSET_HIGH + i * 8),
    };
  }
  s->uncached[0] = (struct dmcub_window){
    .top = read_register(DMCUB_REGION4_TOP), .low = read_register(DMCUB_REGION4_OFFSET_LOW),
    .high = read_register(DMCUB_REGION4_OFFSET_HIGH),
  };
  s->uncached[1] = (struct dmcub_window){
    .top = read_register(DMCUB_REGION5_TOP), .low = read_register(DMCUB_REGION5_OFFSET_LOW),
    .high = read_register(DMCUB_REGION5_OFFSET_HIGH),
  };
}

static bool device_ready(struct pci_device *device, unsigned *power, phys_addr_t *bar)
{
  if (device->owner || device->base_class != RENOIR_DISPLAY_CLASS ||
      device->header_type != PCI_HEADER_ENDPOINT ||
      pci_read16(device->address, PCI_VENDOR_ID) != RENOIR_VENDOR ||
      pci_read16(device->address, PCI_DEVICE_ID) != RENOIR_DEVICE ||
      !(pci_read16(device->address, PCI_COMMAND) & PCI_COMMAND_MEMORY) ||
      !(pci_read16(device->address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return false;
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = pci_read8(device->address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  unsigned found = 0;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES]) {
      return false;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    if (pci_read8(device->address, offset) == PCI_CAP_POWER) {
      if (found || offset > PCI_CONVENTIONAL_BYTES - PCI_POWER_BYTES) {
        return false;
      }
      found = offset;
    }
    offset = pci_read8(device->address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  if (!found || (pci_read16(device->address, found + PCI_POWER_CONTROL) & PCI_POWER_STATE_MASK) != PCI_POWER_D0) {
    return false;
  }
  if (seen[(found + PCI_REGISTER_BYTES) / PCI_REGISTER_BYTES]) {
    return false;
  }
  for (unsigned slot = 0; slot < RENOIR_BAR; ++slot) {
    uint32_t value = pci_read32(device->address, PCI_BAR_FIRST + slot * PCI_REGISTER_BYTES);
    if (!(value & PCI_BAR_IO) && (value & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_64 &&
        ++slot == RENOIR_BAR) {
      return false;
    }
  }
  uint32_t value = pci_read32(device->address, PCI_BAR_FIRST + RENOIR_BAR * PCI_REGISTER_BYTES);
  if (value & (PCI_BAR_IO | PCI_BAR_MEMORY_TYPE_MASK | PCI_BAR_PREFETCHABLE)) {
    return false;
  }
  *bar = value & PCI_BAR_MEMORY_ADDRESS_MASK;
  *power = found;
  return *bar && !(*bar & (PAGE_SIZE - 1)) &&
      *bar <= UINT64_C(0x100000000) - register_pages[REGISTER_PAGE_COUNT - 1] - PAGE_SIZE;
}

static void print_state(const struct inventory_state *s)
{
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    const struct hubp_state *h = &s->hubp[i];
    const struct otg_state *t = &s->otg[i];
    klog("renoir-inventory: OTG%u control=%x interlace=%x ht=%x hb=%x vt=%x vb=%x ODM=%x format=%x OPP%u mux=%x\n",
        i, t->control, t->interlace, t->h_total, t->h_blank, t->v_total, t->v_blank, t->source, t->format, i, s->mux[i]);
    klog("renoir-inventory: HUBP%u control=%x clock=%x config=%x addr-config=%x tiling=%x viewport-start=%x size=%x pitch=%x\n",
        i, h->control, h->clock, h->config, h->address_config, h->tiling, h->viewport_start, h->viewport_size, h->pitch);
    klog("renoir-inventory: HUBP%u VMID=%x surface=%x flip=%x flip2=%x crossbar=%x aperture=%x..%x TLB=%x\n",
        i, h->vmid, h->surface, h->flip, h->flip2, h->crossbar, h->aperture_low, h->aperture_high, h->tlb);
    klog("renoir-inventory: HUBP%u primary=%lx inuse=%lx earliest=%lx metadata=%lx\n",
        i, h->primary, h->inuse, h->earliest, h->metadata);
  }
  for (unsigned i = 0; i < RENOIR_MPCCS; ++i) {
    const struct mpcc_state *m = &s->mpcc[i];
    klog("renoir-inventory: MPCC%u top=%x bottom=%x OPP=%x control=%x status=%x\n",
        i, m->top, m->bottom, m->opp, m->control, m->status);
  }
  klog("renoir-inventory: DCN FB=%x..%x offset=%x GC-offset=%x MMHUB=%x..%x memsize=%u MiB\n",
      s->fb_base, s->fb_top, s->fb_offset, s->gc_offset, s->mc_base, s->mc_top, s->memsize);
  klog("renoir-inventory: VMID0 control=%x PT-base=%x:%x start=%x:%x end=%x:%x\n",
      s->context_control, s->context_base_high, s->context_base_low,
      s->context_start_high, s->context_start_low, s->context_end_high, s->context_end_low);
  klog("renoir-inventory: DMCUB control=%x security=%x\n", s->dmcub_control, s->dmcub_security);
  for (unsigned i = 0; i < DMCUB_CACHE_WINDOWS; ++i) {
    const struct dmcub_window *w = &s->cache[i];
    klog("renoir-inventory: DMCUB CW%u base=%x top=%x offset=%x:%x enabled=%u\n",
        i, w->base, w->top, w->high, w->low, !!(w->top & DMCUB_WINDOW_ENABLE));
  }
  for (unsigned i = 0; i < DMCUB_UNCACHED_WINDOWS; ++i) {
    const struct dmcub_window *w = &s->uncached[i];
    klog("renoir-inventory: DMCUB region%u top=%x offset=%x:%x enabled=%u\n",
        i + 4, w->top, w->high, w->low, !!(w->top & DMCUB_WINDOW_ENABLE));
  }
}

static int mono_route(const struct inventory_state *s)
{
  unsigned otg = 0, enabled = 0;
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    if (s->otg[i].control & OTG_MASTER_ENABLE) {
      otg = i;
      ++enabled;
    }
  }
  if (enabled != 1 || s->otg[otg].interlace & 1) {
    return -1;
  }
  unsigned source = s->otg[otg].source;
  unsigned opp = (source >> ODM_SEG0_SHIFT) & SELECTOR_MASK;
  unsigned other = (source >> ODM_SEG1_SHIFT) & SELECTOR_MASK;
  /* Match Linux's VBIOS stale-count workaround: disconnected SEG1 wins. */
  if (opp >= RENOIR_PIPES || ((source & ODM_INPUT_COUNT) && other != SELECTOR_NONE)) {
    return -1;
  }
  unsigned mpcc = s->mux[opp] & SELECTOR_MASK;
  if (mpcc >= RENOIR_MPCCS) {
    return -1;
  }
  const struct mpcc_state *m = &s->mpcc[mpcc];
  unsigned hubp = m->top & SELECTOR_MASK, bottom = m->bottom & SELECTOR_MASK;
  if (hubp >= RENOIR_PIPES || (m->opp & SELECTOR_MASK) != opp ||
      (bottom != SELECTOR_NONE && bottom != mpcc)) {
    return -1;
  }
  const struct hubp_state *h = &s->hubp[hubp];
  if ((h->control & (HUBP_BLANK | HUBP_DISABLE)) || (h->flip & FLIP_STEREO) ||
      !(h->clock & 1) ||
      ((h->control >> HUBP_VTG_SHIFT) & SELECTOR_MASK) != otg) {
    return -1;
  }
  klog("renoir-inventory: mono route candidate OTG%u <- OPP%u <- MPCC%u <- HUBP%u\n", otg, opp, mpcc, hubp);
  return hubp;
}

static bool translation(const struct boot_info *boot, const struct inventory_state *s, unsigned hubp)
{
  const struct hubp_state *h = &s->hubp[hubp];
  uint64_t gpu_base = (uint64_t)(s->fb_base & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  uint64_t gpu_end = ((uint64_t)(s->fb_top & FB_ADDRESS_MASK) + 1) << FB_ADDRESS_SHIFT;
  uint64_t cpu_base = (uint64_t)(s->fb_offset & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  uint64_t bytes = boot->framebuffer.size;
  uint64_t pt_base = s->context_base_low | ((uint64_t)s->context_base_high << 32);
  uint64_t pt_start = (s->context_start_low | ((uint64_t)(s->context_start_high & 15) << 32)) << 12;
  uint64_t pt_end = ((s->context_end_low | ((uint64_t)(s->context_end_high & 15) << 32)) + 1) << 12;
  if ((h->vmid & SELECTOR_MASK) || !bytes || gpu_end <= gpu_base || h->primary < gpu_base ||
      h->primary >= gpu_end || bytes > gpu_end - h->primary ||
      h->primary - gpu_base > UINT64_MAX - cpu_base ||
      s->fb_offset != s->gc_offset || s->fb_base != s->mc_base || s->fb_top != s->mc_top ||
      h->primary != h->inuse || h->primary != h->earliest ||
      (h->flip & (FLIP_PENDING | FLIP_LOCK | FLIP_MASTER_LOCK)) ||
      (pt_base && pt_end > pt_start && overlaps(h->primary, bytes, pt_start, pt_end - pt_start))) {
    return false;
  }
  uint64_t cpu = h->primary - gpu_base + cpu_base;
  klog("renoir-inventory: direct FB correlation GPU=%lx minus=%lx plus=%lx CPU=%lx GOP=%lx bytes=%lu\n",
      h->primary, gpu_base, cpu_base, cpu, boot->framebuffer.physical, bytes);
  return cpu == boot->framebuffer.physical;
}

static void print_memory_map(const struct boot_info *boot, const struct inventory_state *s)
{
  uint64_t cpu_base = (uint64_t)(s->gc_offset & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  uint64_t bytes = (uint64_t)s->memsize * MIB;
  klog("renoir-inventory: CPU carve-out candidate=%lx bytes=%lu UEFI-map=%s (reservation is not delegation)\n",
      cpu_base, bytes, boot->efi_map_valid ? "valid" : "missing/invalid/capacity-exceeded");
  if (boot->efi_map_valid) {
    for (size_t i = 0; i < boot->efi_region_count; ++i) {
      const struct boot_efi_region *r = &boot->efi_regions[i];
      if (overlaps(r->base, r->length, cpu_base, bytes) ||
          overlaps(r->base, r->length, boot->framebuffer.physical, boot->framebuffer.size)) {
        klog("renoir-inventory: UEFI type=%u base=%lx bytes=%lu attributes=%lx\n",
            r->type, r->base, r->length, r->attributes);
      }
    }
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (overlaps(r->base, r->length, cpu_base, bytes) ||
        overlaps(r->base, r->length, boot->framebuffer.physical, boot->framebuffer.size)) {
      klog("renoir-inventory: boot-map type=%u base=%lx bytes=%lu\n", r->type, r->base, r->length);
    }
  }
}

void renoir_inventory(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count());
  klog("renoir-inventory: begin read-only; no GPU/PCI/MC writes\n");
  struct pci_device *device;
  if (pci_select_device(RENOIR_VENDOR, RENOIR_DEVICE, &device) != PCI_SELECTION_UNIQUE) {
    klog("renoir-inventory: unavailable: unique Renoir endpoint absent; spare-pool=unproven\n");
    return;
  }
  renoir_inventory_firmware(device->address);
  unsigned display_count = 0;
  for (const struct pci_device *d = pci_device_at(0); d; d = d->next) {
    display_count += d->base_class == RENOIR_DISPLAY_CLASS;
  }
  unsigned power;
  phys_addr_t bar;
  if (display_count != 1 || !boot->framebuffer.size || !device_ready(device, &power, &bar) ||
      !map_registers(boot, bar)) {
    klog("renoir-inventory: unavailable: identity/D0/BAR/GOP/mapping prerequisite; spare-pool=unproven\n");
    return;
  }
  klog("renoir-inventory: PCI=%x:%x.%u BAR5=%lx GOP=%lx %zux%zu pitch=%zu RGB=%u/%u/%u\n",
      device->address.bus, device->address.device, device->address.function, bar,
      boot->framebuffer.physical, boot->framebuffer.width, boot->framebuffer.height,
      boot->framebuffer.pitch, boot->framebuffer.red_shift, boot->framebuffer.green_shift, boot->framebuffer.blue_shift);
  snapshot(&first);
  snapshot(&second);
  phys_addr_t final_bar;
  unsigned final_power;
  bool stable = !memcmp(&first, &second, sizeof(first)) &&
      device_ready(device, &final_power, &final_bar) && power == final_power && bar == final_bar;
  unmap_registers();
  print_state(&second);
  print_memory_map(boot, &second);
  int hubp = stable ? mono_route(&second) : -1;
  bool matched = hubp >= 0 && translation(boot, &second, hubp);
  klog("renoir-inventory: stable=%u mono-route=%u direct-GOP-correlation=%u\n", stable, hubp >= 0, matched);
  klog("renoir-inventory: PSP/SMU pre-OS reservations=unknown; DMCUB windows are exclusions, not completeness\n");
  klog("renoir-inventory: spare-pool=unproven; firmware/boot allocator handoff not established; no allocation or flips\n");
}

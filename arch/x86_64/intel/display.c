#include <arch/intel/display.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include "display_regs.h"
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/pci/registers.h>

#define INTEL_VENDOR 0x8086
#define PANTHER_LAKE_DEVICE 0xb0a0
#define DISPLAY_CLASS 3
#define GMD_ARCH_SHIFT 22
#define GMD_ARCH_MASK 0x3ffu
#define GMD_RELEASE_SHIFT 14
#define GMD_RELEASE_MASK 0xffu
#define GMD_STEP_MASK 0x3fu

/* The GMD read precedes profile selection; powered state requires DC admission. */
static bool overlaps(uint64_t first, uint64_t bytes, uint64_t other, uint64_t size)
{
  return size && (bytes > UINT64_MAX - first || size > UINT64_MAX - other ||
      (first < other + size && other < first + bytes));
}

static bool page_available(const struct boot_info *boot, phys_addr_t physical)
{
  if (!arch_pci_mmio_available(physical, PAGE_SIZE) ||
      vm_kernel_physical_overlap(physical, PAGE_SIZE) ||
      overlaps(physical, PAGE_SIZE, boot->framebuffer.physical, boot->framebuffer.size)) {
    return false;
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->type != BOOT_RESERVED && overlaps(physical, PAGE_SIZE, r->base, r->length)) {
      return false;
    }
  }
  return true;
}

static bool read_bar(struct pci_address address, unsigned slot, uint64_t *base)
{
  uint32_t low = pci_read32(address, PCI_BAR_FIRST + slot * PCI_REGISTER_BYTES);
  unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
  if ((low & PCI_BAR_IO) || (type != PCI_BAR_MEMORY_32 && type != PCI_BAR_MEMORY_64)) {
    return false;
  }
  *base = low & PCI_BAR_MEMORY_ADDRESS_MASK;
  if (type == PCI_BAR_MEMORY_64) {
    if (slot + 1 >= PCI_ENDPOINT_BARS) {
      return false;
    }
    *base |= (uint64_t)pci_read32(address,
        PCI_BAR_FIRST + (slot + 1) * PCI_REGISTER_BYTES) << PCI_BAR_HIGH_SHIFT;
  }
  return *base && !(*base & (PAGE_SIZE - 1));
}

static bool device_ready(const struct pci_device *device, uint16_t *power)
{
  struct pci_address address = device->address;
  if (device->owner || device->base_class != DISPLAY_CLASS ||
      device->header_type != PCI_HEADER_ENDPOINT ||
      pci_read16(address, PCI_VENDOR_ID) != INTEL_VENDOR ||
      pci_read16(address, PCI_DEVICE_ID) != PANTHER_LAKE_DEVICE ||
      !(pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MEMORY)) {
    return false;
  }
  *power = PCI_POWER_D0;
  if (!(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return true;
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  bool found = false;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES]) {
      return false;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    if (pci_read8(address, offset) == PCI_CAP_POWER) {
      if (found || offset > PCI_CONVENTIONAL_BYTES - PCI_POWER_BYTES) {
        return false;
      }
      found = true;
      *power = pci_read16(address, offset + PCI_POWER_CONTROL);
      if ((*power & PCI_POWER_STATE_MASK) != PCI_POWER_D0) {
        return false;
      }
    }
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  return true;
}


static const uint32_t register_pages[] = {
  0x41000, 0x42000, 0x45000, 0x51000,
  0x60000, 0x61000, 0x62000, 0x63000, 0x68000, 0x69000,
  0x70000, 0x71000, 0x72000, 0x73000, 0x78000,
};
static uintptr_t windows[sizeof(register_pages) / sizeof(register_pages[0])];
static uintptr_t gtt_windows[GGTT_PAGES];
static const struct boot_info *boot_info;
static struct pci_device *display_device;
static uint64_t register_bar, aperture_bar;
static uint32_t inherited_bios, inherited_driver, owned_requests;
static uint32_t saved_target, gmd_identity;
static bool fenced, dc_confirmed, gtt_mapping_failed;

static bool map_page(uint32_t offset, bool writable, uintptr_t *window)
{
  phys_addr_t physical = register_bar + offset;
  if (!page_available(boot_info, physical) ||
      vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, window) != MM_OK) {
    return false;
  }
  enum mm_result result = vm_map_mmio(*window, physical);
  if (result == MM_OK && !writable) {
    result = vm_protect(vm_kernel_space(), *window, 0);
  }
  if (result != MM_OK) {
    /* Keep a possible mapping for the ordinary unmap path. No MMIO is read. */
    struct page_translation page;
    if (vm_query(vm_kernel_space(), *window, &page) != MM_OK) {
      vm_release(vm_kernel_space(), *window, PAGE_SIZE);
      *window = 0;
    }
    return false;
  }
  return true;
}

static uint32_t read_register(uint32_t offset)
{
  for (size_t i = 0; i < sizeof(register_pages) / sizeof(register_pages[0]); ++i) {
    if ((offset & ~(uint32_t)(PAGE_SIZE - 1)) == register_pages[i]) {
      KASSERT(windows[i] && !(offset & 3));
      return *(const volatile uint32_t *)(windows[i] + (offset & (PAGE_SIZE - 1)));
    }
  }
  KASSERT(false);
  return 0;
}

static uint32_t write_control(uint32_t offset, uint32_t clear, uint32_t set)
{
  KASSERT(!fenced && ((offset == DC_STATE_EN &&
      !((clear | set) & ~(DC_TARGET_MASK | DC3CO_STATUS))) ||
      (offset == POWER_DRIVER && !((clear | set) & ~POWER_REQUEST_MASK))));
  uint32_t value = (read_register(offset) & ~clear) | set;
  for (size_t i = 0; i < sizeof(register_pages) / sizeof(register_pages[0]); ++i) {
    if ((offset & ~(uint32_t)(PAGE_SIZE - 1)) == register_pages[i]) {
      *(volatile uint32_t *)(windows[i] + (offset & (PAGE_SIZE - 1))) = value;
      return value;
    }
  }
  KASSERT(false);
  return 0;
}

static void fence_access(const char *reason)
{
  fenced = true;
  kerror("intel-display: access cleanup unconfirmed: %s; DC0-confirmed=%u retained-requests=%x; fenced until reboot\n",
      reason, dc_confirmed, owned_requests);
}

static bool set_dc_target(uint32_t target)
{
  /* Linux gen9_write_dc_state: seven consecutive matching reads, <=100 rewrites.
   * Each rewrite preserves current unrelated fields instead of a stale image. */
  uint32_t expected = write_control(DC_STATE_EN, DC_TARGET_MASK, target);
  unsigned matches = 0, rewrites = 0;
  for (;;) {
    uint32_t current = read_register(DC_STATE_EN);
    if ((current & DC_TARGET_MASK) != target || current != expected) {
      if (rewrites++ == 100) {
        return false;
      }
      expected = write_control(DC_STATE_EN, DC_TARGET_MASK, target);
      matches = 0;
    } else if (++matches == 7) {
      return true;
    }
  }
}

static bool wait_register(uint32_t offset, uint32_t mask, uint32_t expected)
{
  uint64_t deadline = arch_monotonic_ns() + UINT64_C(1000000);
  do {
    if ((read_register(offset) & mask) == expected) {
      return true;
    }
    cpu_relax();
  } while (arch_monotonic_ns() < deadline);
  return false;
}

struct power_well {
  const char *name;
  unsigned index, fuse;
  int parent;
  enum { WELL_UNCHECKED, WELL_SKIPPED, WELL_HELD } state;
};

static struct power_well wells[] = {
  {"PG1", 0, 26, -1, WELL_UNCHECKED},
  {"PG2", 1, 25, 0, WELL_UNCHECKED},
  {"PGA", 5, 21, 0, WELL_UNCHECKED},
  {"PGB", 6, 20, 0, WELL_UNCHECKED},
  {"PGC", 7, 19, 1, WELL_UNCHECKED},
  {"PGD", 8, 18, 1, WELL_UNCHECKED},
};
static const unsigned pipe_wells[] = {2, 3, 4, 5};

static bool inherited_hold(unsigned well)
{
  uint32_t request = 2u << (2 * wells[well].index);
  return ((read_register(POWER_BIOS) & inherited_bios) |
      (read_register(POWER_DRIVER) & inherited_driver)) & request;
}

static bool identity_stable(void)
{
  uint16_t power;
  uint64_t registers, aperture;
  return device_ready(display_device, &power) &&
      read_bar(display_device->address, 0, &registers) && registers == register_bar &&
      read_bar(display_device->address, 2, &aperture) && aperture == aperture_bar &&
      read_register(GMD_ID_DISPLAY) == gmd_identity;
}

static bool holds_stable(void)
{
  if (!identity_stable() || (read_register(DC_STATE_EN) & DC_TARGET_MASK)) {
    return false;
  }
  uint32_t driver = read_register(POWER_DRIVER), fuse = read_register(FUSE_STATUS);
  for (unsigned i = 0; i < sizeof(wells) / sizeof(wells[0]); ++i) {
    if (wells[i].state == WELL_HELD && (!inherited_hold(i) ||
        !(driver & (1u << (2 * wells[i].index))) ||
        !(driver & (2u << (2 * wells[i].index))) || !(fuse & PG0_FUSE) ||
        !(fuse & (1u << wells[i].fuse)))) {
      return false;
    }
  }
  return true;
}

static bool retain_well(unsigned well)
{
  struct power_well *w = &wells[well];
  if (w->state != WELL_UNCHECKED) {
    return w->state == WELL_HELD;
  }
  w->state = WELL_SKIPPED;
  if (w->parent >= 0 && !retain_well((unsigned)w->parent)) {
    return false;
  }
  uint32_t request = 2u << (2 * w->index);
  uint32_t state = 1u << (2 * w->index);
  uint32_t driver = read_register(POWER_DRIVER);
  if (!inherited_hold(well) || !(driver & state) ||
      !(read_register(FUSE_STATUS) & (1u << w->fuse))) {
    kinfo("intel-display: %s skipped: no qualified inherited request/state/fuse\n", w->name);
    return false;
  }
  if (!(inherited_driver & request)) {
    if ((driver & request) || !holds_stable()) {
      fence_access("request ownership changed before acquire");
      return false;
    }
    owned_requests |= request;
    write_control(POWER_DRIVER, 0, request);
  }
  if (!wait_register(POWER_DRIVER, request | state, request | state) ||
      !wait_register(FUSE_STATUS, 1u << w->fuse, 1u << w->fuse) ||
      !inherited_hold(well)) {
    fence_access("power hold/fuse confirmation failed");
    return false;
  }
  w->state = WELL_HELD;
  kdebug("intel-display: hold %s inherited-bios=%x inherited-driver=%x added=%u\n",
      w->name, inherited_bios & request, inherited_driver & request,
      !!(owned_requests & request));
  return true;
}

struct plane_state {
  uint32_t control, stride, position, size, surface, offset, live, color;
};
struct pipe_state {
  uint32_t valid, transcoder_valid, conf, misc, source, ddi, dp2;
  uint32_t timing[6], psr, psr2, joiner[2];
  uint32_t cursor[4], scaler[SCALER_COUNT][3];
  struct plane_state planes[PLANE_COUNT];
};
struct display_state {
  struct pipe_state pipes[PIPE_COUNT];
};
static struct display_state first_state, second_state;
static uint32_t pipe_mask, scaler_count;

static bool snapshot(struct display_state *s)
{
  memset(s, 0, sizeof(*s));
  for (unsigned p = 0; p < PIPE_COUNT; ++p) {
    if (!(pipe_mask & (1u << p))) {
      continue;
    }
    struct pipe_state *pipe = &s->pipes[p];
    if (!holds_stable()) {
      fence_access("inherited power lifetime changed during readout");
      return false;
    }
    unsigned trans_well = p >= 2 ? 1 : 0;
    if (wells[trans_well].state == WELL_HELD) {
      pipe->transcoder_valid = 1;
      pipe->conf = read_register(TRANS_CONF(p));
      if (pipe->conf & PLANE_ENABLE) {
        for (unsigned n = 0; n < 6; ++n) {
          pipe->timing[n] = read_register(TRANS_BASE(p) + 4 * n);
        }
        pipe->ddi = read_register(TRANS_DDI(p));
        pipe->dp2 = read_register(TRANS_DP2(p));
        pipe->psr = read_register(PSR_CONTROL(p));
        pipe->psr2 = read_register(PSR2_CONTROL(p));
        kdebug("intel-display: TC%c PSR-status=%x PSR2-status=%x\n", 'A' + p,
            read_register(PSR_STATUS(p)), read_register(PSR2_STATUS(p)));
      }
    }
    if (wells[pipe_wells[p]].state != WELL_HELD) {
      continue;
    }
    pipe->valid = 1;
    pipe->source = read_register(PIPE_SOURCE(p));
    pipe->misc = read_register(PIPE_MISC(p));
    pipe->joiner[0] = read_register(JOINER_BASE(p));
    pipe->joiner[1] = read_register(JOINER_BASE(p) + 4);
    for (unsigned n = 0; n < PLANE_COUNT; ++n) {
      struct plane_state *plane = &pipe->planes[n];
      uint32_t base = PLANE_BASE(p, n);
      plane->control = read_register(base);
      if (!(plane->control & PLANE_ENABLE)) {
        continue;
      }
      plane->stride = read_register(base + 8);
      plane->position = read_register(base + 12);
      plane->size = read_register(base + 16);
      plane->surface = read_register(base + 28);
      plane->offset = read_register(base + 36);
      plane->live = read_register(base + 44);
      plane->color = read_register(base + 76);
    }
    uint32_t cursor = CURSOR_BASE(p);
    pipe->cursor[0] = read_register(cursor);
    if (pipe->cursor[0] & 0x27) {
      pipe->cursor[1] = read_register(cursor + 4);
      pipe->cursor[2] = read_register(cursor + 8);
      pipe->cursor[3] = read_register(cursor + 44);
    }
    for (unsigned n = 0; n < scaler_count; ++n) {
      uint32_t base = SCALER_BASE(p, n);
      pipe->scaler[n][0] = read_register(base + 128);
      if (pipe->scaler[n][0] & SCALER_ENABLE) {
        pipe->scaler[n][1] = read_register(base + 112);
        pipe->scaler[n][2] = read_register(base + 116);
      }
    }
  }
  return true;
}

static bool read_gtt(uint64_t gpu, uint64_t *value)
{
  if (gtt_mapping_failed || gpu >= GGTT_ADDRESS_BYTES) {
    return false;
  }
  uint32_t offset = (uint32_t)((gpu / PAGE_SIZE) * sizeof(uint64_t));
  unsigned index = offset / PAGE_SIZE;
  if (!gtt_windows[index] &&
      !map_page(GGTT_BASE + index * PAGE_SIZE, false, &gtt_windows[index])) {
    gtt_mapping_failed = true;
    return false;
  }
  *value = *(const volatile uint64_t *)(gtt_windows[index] + (offset & (PAGE_SIZE - 1)));
  return true;
}

static void inspect_gtt(void)
{
  const struct boot_framebuffer *fb = &boot_info->framebuffer;
  if (!fb->size || fb->physical < aperture_bar) {
    kinfo("intel-display: GOP GGTT unavailable: no aperture-relative extent\n");
    return;
  }
  uint64_t gpu = fb->physical - aperture_bar;
  if (gpu >= GGTT_ADDRESS_BYTES || fb->size > GGTT_ADDRESS_BYTES - gpu) {
    kinfo("intel-display: GOP GGTT unavailable: extent outside source table\n");
    return;
  }
  uint64_t base = gpu & ~(PAGE_SIZE - 1);
  size_t count = ((gpu - base) + fb->size + PAGE_SIZE - 1) / PAGE_SIZE;
  uint64_t *ptes = kmalloc(count * sizeof(*ptes));
  if (!ptes) {
    kinfo("intel-display: GOP GGTT unavailable: snapshot allocation failed\n");
    return;
  }
  bool complete = true, stable = true;
  size_t present = 0, device_memory = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!read_gtt(base + i * PAGE_SIZE, &ptes[i])) {
      complete = false;
      break;
    }
    present += !!(ptes[i] & GGTT_PTE_PRESENT);
    device_memory += !!(ptes[i] & GGTT_PTE_DM);
  }
  if (complete) {
    for (size_t i = 0; i < count; ++i) {
      uint64_t value;
      if (!read_gtt(base + i * PAGE_SIZE, &value)) {
        complete = false;
        break;
      }
      stable &= value == ptes[i];
    }
  }
  kinfo("intel-display: GOP GGTT gpu=%lx pages=%zu complete=%u stable=%u present=%zu DM=%zu\n",
      gpu, count, complete, stable, present, device_memory);
  if (complete) {
    kdebug("intel-display: GOP PTE first=%lx last=%lx; DMA addresses, not CPU-physical ownership\n",
        ptes[0], ptes[count - 1]);
  }
  kfree(ptes);
}

static void report_state(const struct display_state *s, bool stable)
{
  const struct boot_framebuffer *fb = &boot_info->framebuffer;
  for (unsigned p = 0; p < PIPE_COUNT; ++p) {
    const struct pipe_state *pipe = &s->pipes[p];
    if (pipe->transcoder_valid) {
      kdebug("intel-display: TC%c conf=%x DDI=%x DP2=%x PSR=%x PSR2=%x\n",
          'A' + p, pipe->conf, pipe->ddi, pipe->dp2, pipe->psr, pipe->psr2);
      if (pipe->conf & PLANE_ENABLE) {
        kinfo("intel-display: TC%c active=%ux%u total=%ux%u; pixel clock/link unchanged\n", 'A' + p,
            (pipe->timing[0] & 0xffff) + 1, (pipe->timing[3] & 0xffff) + 1,
            (pipe->timing[0] >> 16) + 1, (pipe->timing[3] >> 16) + 1);
        kdebug("intel-display: TC%c timings HT/HB/HS/VT/VB/VS=%x/%x/%x/%x/%x/%x\n", 'A' + p,
            pipe->timing[0], pipe->timing[1], pipe->timing[2], pipe->timing[3],
            pipe->timing[4], pipe->timing[5]);
      }
    }
    if (!pipe->valid) {
      continue;
    }
    kdebug("intel-display: PIPE%c source=%x misc=%x DSS=%x/%x cursor=%x/%x/%x/%x\n", 'A' + p,
        pipe->source, pipe->misc, pipe->joiner[0], pipe->joiner[1], pipe->cursor[0], pipe->cursor[1],
        pipe->cursor[2], pipe->cursor[3]);
    for (unsigned n = 0; n < scaler_count; ++n) {
      kdebug("intel-display: PIPE%c scaler%u control=%x position=%x size=%x\n", 'A' + p, n,
          pipe->scaler[n][0], pipe->scaler[n][1], pipe->scaler[n][2]);
    }
    for (unsigned n = 0; n < PLANE_COUNT; ++n) {
      const struct plane_state *plane = &pipe->planes[n];
      kdebug("intel-display: PIPE%c plane%u ctl=%x stride=%x pos=%x size=%x surf=%x offset=%x live=%x color=%x\n",
          'A' + p, n + 1, plane->control, plane->stride, plane->position,
          plane->size, plane->surface, plane->offset, plane->live, plane->color);
      if (!(plane->control & PLANE_ENABLE)) {
        continue;
      }
      unsigned width = (plane->size & 0xffff) + 1, height = (plane->size >> 16) + 1;
      unsigned pitch = (plane->stride & 0xfff) * 64;
      bool linear = !(plane->surface & PLANE_DECRYPT) &&
          !(plane->control & (PLANE_TILING_MASK | PLANE_COMPRESSION_MASK |
          PLANE_ROTATION_MASK | PLANE_FLIP_HORIZONTAL)) &&
          (plane->control & PLANE_FORMAT_MASK) == PLANE_FORMAT_8888 &&
          !(pipe->joiner[0] & JOINER_MASK);
      bool channels = fb->green_shift == 8 &&
          (plane->control & PLANE_RGBX ? fb->red_shift == 0 && fb->blue_shift == 16 :
           fb->red_shift == 16 && fb->blue_shift == 0);
      bool candidate = stable && pipe->transcoder_valid && (pipe->conf & PLANE_ENABLE) &&
          (pipe->ddi & PLANE_ENABLE) && linear && channels && !plane->position && !plane->offset &&
          width == fb->width && height == fb->height && pitch == fb->pitch &&
          fb->physical >= aperture_bar &&
          fb->physical - aperture_bar == (plane->surface & PLANE_SURFACE_MASK) &&
          (plane->live & PLANE_SURFACE_MASK) == (plane->surface & PLANE_SURFACE_MASK);
      kinfo("intel-display: PIPE%c plane%u %ux%u pitch=%u linear8888=%u GOP-source-candidate=%u\n",
          'A' + p, n + 1, width, height, pitch, linear, candidate);
      if (linear && pitch && height && !plane->offset) {
        uint64_t start = plane->surface & PLANE_SURFACE_MASK;
        uint64_t bytes = (uint64_t)pitch * height;
        uint64_t first, last;
        if (bytes <= GGTT_ADDRESS_BYTES - start && read_gtt(start, &first) &&
            read_gtt(start + bytes - 1, &last)) {
          kdebug("intel-display: PIPE%c plane%u GGTT edge PTE=%lx/%lx\n", 'A' + p, n + 1, first, last);
        }
      }
    }
  }
}

static void observe_timing(void)
{
  uint32_t initial_line[PIPE_COUNT] = {0}, initial_stamp[PIPE_COUNT] = {0};
  uint32_t last_line[PIPE_COUNT] = {0}, last_stamp[PIPE_COUNT] = {0};
  unsigned line_changes[PIPE_COUNT] = {0}, stamp_changes[PIPE_COUNT] = {0};
  bool enabled[PIPE_COUNT] = {0};
  for (unsigned p = 0; p < PIPE_COUNT; ++p) {
    enabled[p] = first_state.pipes[p].valid && first_state.pipes[p].transcoder_valid &&
        (first_state.pipes[p].conf & PLANE_ENABLE);
    if (enabled[p]) {
      initial_line[p] = last_line[p] = read_register(PIPE_SCANLINE(p));
      initial_stamp[p] = last_stamp[p] = read_register(FRAME_TIMESTAMP(p));
    }
  }
  bool any = false;
  for (unsigned p = 0; p < PIPE_COUNT; ++p) {
    any |= enabled[p];
  }
  if (!any) {
    kinfo("intel-display: timing unavailable: no qualified active pipe\n");
    return;
  }
  uint64_t start = arch_monotonic_ns(), end = start + UINT64_C(40000000);
  do {
    if (!holds_stable()) {
      fence_access("power lifetime changed during timing observation");
      return;
    }
    for (unsigned p = 0; p < PIPE_COUNT; ++p) {
      if (!enabled[p]) {
        continue;
      }
      uint32_t line = read_register(PIPE_SCANLINE(p));
      uint32_t stamp = read_register(FRAME_TIMESTAMP(p));
      line_changes[p] += line != last_line[p];
      stamp_changes[p] += stamp != last_stamp[p];
      last_line[p] = line;
      last_stamp[p] = stamp;
    }
    cpu_relax();
  } while (arch_monotonic_ns() < end);
  uint64_t elapsed = arch_monotonic_ns() - start;
  for (unsigned p = 0; p < PIPE_COUNT; ++p) {
    if (enabled[p]) {
      kinfo("intel-display: PIPE%c timing ns=%lu line=%x/%x changes=%u stamp=%x/%x changes=%u; no PSR exit\n",
          'A' + p, elapsed, initial_line[p], last_line[p], line_changes[p],
          initial_stamp[p], last_stamp[p], stamp_changes[p]);
    }
  }
}

static bool release_holds(void)
{
  if (!holds_stable()) {
    fence_access("inherited holds changed before cleanup");
    return false;
  }
  for (unsigned i = sizeof(wells) / sizeof(wells[0]); i-- > 0;) {
    uint32_t request = 2u << (2 * wells[i].index);
    if (!(owned_requests & request)) {
      continue;
    }
    if (!holds_stable()) {
      fence_access("inherited holds changed during cleanup");
      return false;
    }
    write_control(POWER_DRIVER, request, 0);
    if (!wait_register(POWER_DRIVER, request, 0)) {
      fence_access("request release not confirmed");
      return false;
    }
    owned_requests &= ~request;
    wells[i].state = WELL_SKIPPED;
    if (!inherited_hold(i) ||
        !(read_register(POWER_DRIVER) & (1u << (2 * wells[i].index))) ||
        !(read_register(FUSE_STATUS) & (1u << wells[i].fuse))) {
      fence_access("inherited hold changed after request release");
      return false;
    }
  }
  if (!holds_stable()) {
    fence_access("identity changed before DC restoration");
    return false;
  }
  if (!set_dc_target(saved_target)) {
    dc_confirmed = false;
    fence_access("DC target restoration not confirmed");
    return false;
  }
  dc_confirmed = false;
  kinfo("intel-display: cleanup confirmed; inherited DC target=%x restored, added requests=%x\n",
      saved_target, owned_requests);
  return true;
}

static void unmap_window(uintptr_t *window)
{
  if (!*window) {
    return;
  }
  phys_addr_t physical;
  if (vm_unmap(vm_kernel_space(), *window, &physical) != MM_OK ||
      vm_release(vm_kernel_space(), *window, PAGE_SIZE) != MM_OK) {
    fenced = true;
    kerror("intel-display: temporary CPU mapping retained until reboot\n");
    return;
  }
  *window = 0;
}

static void unmap_windows(void)
{
  for (size_t i = 0; i < sizeof(windows) / sizeof(windows[0]); ++i) {
    unmap_window(&windows[i]);
  }
  for (size_t i = 0; i < GGTT_PAGES; ++i) {
    unmap_window(&gtt_windows[i]);
  }
}

void intel_display_prepare(const struct boot_info *boot)
{
  if (boot_info || pci_select_device(INTEL_VENDOR, PANTHER_LAKE_DEVICE, &display_device) != PCI_SELECTION_UNIQUE) {
    return;
  }
  boot_info = boot;
  uint16_t power;
  if (!device_ready(display_device, &power) || !read_bar(display_device->address, 0, &register_bar) ||
      !read_bar(display_device->address, 2, &aperture_bar) ||
      register_bar > UINT64_MAX - GGTT_BASE - GGTT_TABLE_BYTES) {
    kinfo("intel-display: unavailable: PCI identity/D0/memory decode/BAR prerequisite\n");
    return;
  }
  size_t gmd_page = 0;
  while (register_pages[gmd_page] != (GMD_ID_DISPLAY & ~(uint32_t)(PAGE_SIZE - 1))) {
    ++gmd_page;
  }
  if (!map_page(register_pages[gmd_page], false, &windows[gmd_page])) {
    unmap_windows();
    kinfo("intel-display: unavailable: GMD mapping\n");
    return;
  }
  uint32_t gmd = read_register(GMD_ID_DISPLAY), gmd_second = read_register(GMD_ID_DISPLAY);
  kinfo("intel-display: GMD=%x/%x PCI=%x:%x.%u BAR0=%lx BAR2=%lx\n",
      gmd, gmd_second, display_device->address.bus, display_device->address.device,
      display_device->address.function, register_bar, aperture_bar);
  kinfo("intel-display: GOP=%lx bytes=%zu %zux%zu pitch=%zu RGB=%u/%u/%u\n",
      boot->framebuffer.physical, boot->framebuffer.size, boot->framebuffer.width,
      boot->framebuffer.height, boot->framebuffer.pitch, boot->framebuffer.red_shift,
      boot->framebuffer.green_shift, boot->framebuffer.blue_shift);
  if (gmd != gmd_second || ((gmd >> GMD_ARCH_SHIFT) & GMD_ARCH_MASK) != 30 ||
      ((gmd >> GMD_RELEASE_SHIFT) & GMD_RELEASE_MASK) || (gmd & GMD_STEP_MASK) != 4) {
    kinfo("intel-display: unavailable: power profile is display 30.0 B0 only\n");
    unmap_windows();
    return;
  }
  gmd_identity = gmd;
  for (size_t i = 0; i < sizeof(register_pages) / sizeof(register_pages[0]); ++i) {
    if (!windows[i] && !map_page(register_pages[i], register_pages[i] == 0x45000, &windows[i])) {
      kinfo("intel-display: unavailable: register mapping at %x\n", register_pages[i]);
      unmap_windows();
      return;
    }
  }
  if (!identity_stable()) {
    kinfo("intel-display: unavailable: identity changed before power admission\n");
    unmap_windows();
    return;
  }
  uint32_t initial_dc = read_register(DC_STATE_EN);
  saved_target = initial_dc & DC_TARGET_MASK;
  if (saved_target && saved_target != 1 && saved_target != 2 && saved_target != DC3CO_TARGET) {
    kinfo("intel-display: unavailable: unqualified inherited DC target=%x\n", saved_target);
    unmap_windows();
    return;
  }
  kinfo("intel-display: DC0 admission inherited=%x; no mode/clock/link/plane/GGTT programming\n", initial_dc);
  if (saved_target == DC3CO_TARGET) {
    write_control(DC_STATE_EN, DC3CO_STATUS, 0);
  }
  if (!set_dc_target(0)) {
    fence_access("DC0 admission not confirmed");
    return;
  }
  dc_confirmed = true;
  if (saved_target == DC3CO_TARGET) {
    uint64_t deadline = arch_monotonic_ns() + UINT64_C(200000);
    while (arch_monotonic_ns() < deadline) {
      cpu_relax();
    }
  }
  inherited_bios = read_register(POWER_BIOS);
  inherited_driver = read_register(POWER_DRIVER);
  kdebug("intel-display: power BIOS=%x DRIVER=%x KVMR=%x DEBUG=%x FUSE=%x\n",
      inherited_bios, inherited_driver, read_register(POWER_KVMR), read_register(POWER_DEBUG),
      read_register(FUSE_STATUS));
  if (wait_register(FUSE_STATUS, PG0_FUSE, PG0_FUSE)) {
    retain_well(0);
    if (!fenced && wells[0].state == WELL_HELD) {
      uint32_t dfsm = read_register(DFSM), cap = read_register(DE_CAP);
      static const unsigned disable_bits[] = {30, 21, 28, 22};
      for (unsigned p = 0; p < PIPE_COUNT; ++p) {
        if (!(dfsm & (1u << disable_bits[p]))) {
          pipe_mask |= 1u << p;
        }
      }
      scaler_count = ((cap >> 26) & 3) == 1 ? 1 : SCALER_COUNT;
      kinfo("intel-display: inherited pipe mask=%x scalers=%u DFSM=%x DE_CAP=%x\n",
          pipe_mask, scaler_count, dfsm, cap);
      for (unsigned p = 0; p < PIPE_COUNT && !fenced; ++p) {
        if (pipe_mask & (1u << p)) {
          retain_well(pipe_wells[p]);
        }
      }
    }
  } else {
    kinfo("intel-display: PG0 fuse unavailable; dependent inventory skipped\n");
  }
  bool stable = false;
  if (!fenced && snapshot(&first_state)) {
    inspect_gtt();
    if (holds_stable()) {
      observe_timing();
    } else {
      fence_access("inherited state changed before timing");
    }
    if (!fenced && snapshot(&second_state)) {
      stable = !memcmp(&first_state, &second_state, sizeof(first_state));
      report_state(&second_state, stable);
    }
  }
  if (!fenced && release_holds()) {
    unmap_windows();
    if (fenced) {
      return;
    }
    unsigned qualified_pipes = 0;
    for (unsigned p = 0; p < PIPE_COUNT; ++p) {
      qualified_pipes |= second_state.pipes[p].valid << p;
    }
    kinfo("intel-display: startup readout complete stable=%u qualified-pipes=%x; scanout ownership/latch/retirement unproven\n",
        stable, qualified_pipes);
  }
}

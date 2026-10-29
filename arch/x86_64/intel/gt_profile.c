/* Portions adapted from Linux: Copyright © 2021, 2022, 2023, 2024 Intel Corporation.
 * Linux v6.19 05f7e89a: xe_force_wake.c, xe_gt_mcr.c, xe_pat.c,
 * xe_ggtt.c, xe_wa_oob.rules, xe_pci.c, xe_hw_engine.c and xe_gsc.c.
 * Register layout from xe_engine_regs.h/xe_gt_regs.h. See NOTICE. */
#include <arch/intel/gt_profile.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

#define GMD_ID 0x00d8cu
#define MEDIA_BASE 0x380000u
#define GT_ACK 0x00dfcu
#define STEER_SEMAPHORE 0x00fd0u
#define MCR_SELECTOR 0x00fd4u
#define MCR_MULTICAST (1u << 31)
#define PAT_INDEX0 0x04800u
#define GGTT_PAT_ENTRIES 4u
#define PAT_DISPLAY_INDEX 3u
#define GUC_MIA_IN_RESET (1u << 0)
#define GSC_INIT_COMPLETE (1u << 9)
#define EXECLIST_QUEUE_INVALID (1u << 0)
#define EXECLIST_PENDING ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | \
    (1u << 7) | (1u << 30))
#define PAT_CACHE_MASK ((1u << 9) | 0x3fu)
#define PAT_UC_NONCOHERENT 0x3cu
#define GT_FORCEWAKE 0x0a188u
#define FORCEWAKE_KERNEL 1u
#define FORCEWAKE_KERNEL_MASK (1u << 16)
#define FORCEWAKE_TIMEOUT_NS UINT64_C(50000000)
#define SEMAPHORE_TIMEOUT_NS UINT64_C(10000)
#define VF_CAP 0x1901f8u
#define GMD_ARCH_SHIFT 22
#define GMD_RELEASE_SHIFT 14
#define GMD_STEP_MASK 0x3fu
#define GMD_STEP_B0 4u

enum {
  FW_GT, FW_MEDIA_GT, FW_RENDER, FW_VDBOX0,
  FW_VEBOX0 = FW_VDBOX0 + 8, FW_GSC = FW_VEBOX0 + 4, FW_COUNT,
};
enum engine_presence { ENGINE_ALWAYS, ENGINE_CCS, ENGINE_VDBOX, ENGINE_VEBOX };
static const struct {
  const char *name;
  uint32_t base;
  unsigned domain;
  enum engine_presence presence;
  unsigned bit;
} engines[] = {
  {"rcs0", 0x002000, FW_RENDER, ENGINE_ALWAYS, 0},
  {"bcs0", 0x022000, FW_RENDER, ENGINE_ALWAYS, 0},
  {"bcs8", 0x3ee000, FW_RENDER, ENGINE_ALWAYS, 0},
  {"ccs0", 0x01a000, FW_RENDER, ENGINE_CCS, 0},
  {"ccs1", 0x01c000, FW_RENDER, ENGINE_CCS, 1},
  {"ccs2", 0x01e000, FW_RENDER, ENGINE_CCS, 2},
  {"ccs3", 0x026000, FW_RENDER, ENGINE_CCS, 3},
  {"vcs0", 0x1c0000, FW_VDBOX0 + 0, ENGINE_VDBOX, 0},
  {"vcs1", 0x1c4000, FW_VDBOX0 + 1, ENGINE_VDBOX, 1},
  {"vcs2", 0x1d0000, FW_VDBOX0 + 2, ENGINE_VDBOX, 2},
  {"vcs3", 0x1d4000, FW_VDBOX0 + 3, ENGINE_VDBOX, 3},
  {"vcs4", 0x1e0000, FW_VDBOX0 + 4, ENGINE_VDBOX, 4},
  {"vcs5", 0x1e4000, FW_VDBOX0 + 5, ENGINE_VDBOX, 5},
  {"vcs6", 0x1f0000, FW_VDBOX0 + 6, ENGINE_VDBOX, 6},
  {"vcs7", 0x1f4000, FW_VDBOX0 + 7, ENGINE_VDBOX, 7},
  {"vecs0", 0x1c8000, FW_VEBOX0 + 0, ENGINE_VEBOX, 0},
  {"vecs1", 0x1d8000, FW_VEBOX0 + 1, ENGINE_VEBOX, 1},
  {"vecs2", 0x1e8000, FW_VEBOX0 + 2, ENGINE_VEBOX, 2},
  {"vecs3", 0x1f8000, FW_VEBOX0 + 3, ENGINE_VEBOX, 3},
  {"gsccs0", 0x11a000, FW_GSC, ENGINE_ALWAYS, 0},
};
/* xe graphics30.1/media30.0: two fuses, both GuCs, and GSC HECI1 status. */
static const uint32_t inventory_registers[] = {
  0x009114, 0x389140, 0x00c000, 0x38c000, 0x116c40,
};
enum {
  BASE_MAPPINGS = 6,
  MMIO_WINDOWS = BASE_MAPPINGS + sizeof(inventory_registers) / sizeof(*inventory_registers) +
      sizeof(engines) / sizeof(*engines),
};
_Static_assert(MMIO_WINDOWS <= 64, "bounded bootstrap mapping mask");
static struct {
  uint32_t page;
  uintptr_t window;
} mappings[MMIO_WINDOWS] = {
  {0x000000, 0}, {0x004000, 0}, {0x00a000, 0},
  {0x190000, 0}, {0x380000, 0}, {0x38a000, 0},
};
static size_t mapping_count = BASE_MAPPINGS;
static struct {
  const char *name;
  uint32_t request, ack;
  bool enabled, held, owned;
} forcewake[FW_COUNT] = {
  [FW_GT] = {.name = "gt", .request = GT_FORCEWAKE, .ack = GT_ACK},
  [FW_MEDIA_GT] = {.name = "media-gt", .request = MEDIA_BASE + GT_FORCEWAKE, .ack = MEDIA_BASE + GT_ACK},
  [FW_RENDER] = {.name = "render", .request = 0x00a278, .ack = 0x000d84},
  [FW_VDBOX0 + 0] = {.name = "vdbox0", .request = 0x38a540, .ack = 0x380d50},
  [FW_VDBOX0 + 1] = {.name = "vdbox1", .request = 0x38a544, .ack = 0x380d54},
  [FW_VDBOX0 + 2] = {.name = "vdbox2", .request = 0x38a548, .ack = 0x380d58},
  [FW_VDBOX0 + 3] = {.name = "vdbox3", .request = 0x38a54c, .ack = 0x380d5c},
  [FW_VDBOX0 + 4] = {.name = "vdbox4", .request = 0x38a550, .ack = 0x380d60},
  [FW_VDBOX0 + 5] = {.name = "vdbox5", .request = 0x38a554, .ack = 0x380d64},
  [FW_VDBOX0 + 6] = {.name = "vdbox6", .request = 0x38a558, .ack = 0x380d68},
  [FW_VDBOX0 + 7] = {.name = "vdbox7", .request = 0x38a55c, .ack = 0x380d6c},
  [FW_VEBOX0 + 0] = {.name = "vebox0", .request = 0x38a560, .ack = 0x380d70},
  [FW_VEBOX0 + 1] = {.name = "vebox1", .request = 0x38a564, .ack = 0x380d74},
  [FW_VEBOX0 + 2] = {.name = "vebox2", .request = 0x38a568, .ack = 0x380d78},
  [FW_VEBOX0 + 3] = {.name = "vebox3", .request = 0x38a56c, .ack = 0x380d7c},
  [FW_GSC] = {.name = "gsc", .request = 0x38a618, .ack = 0x380df8},
};
static uint32_t graphics_gmd, media_gmd;
static unsigned pending_writes;
static unsigned pat_index = GGTT_PAT_ENTRIES;
static uint32_t consumer_ccs, consumer_media;
static bool consumers_mapped, pat_write_armed, pat_admitted;
static bool attempted, active, fenced, semaphore_owned, write_workaround;
static uint64_t started_ns, phase_ns;

static void log_phase(const char *name)
{
  uint64_t now = arch_monotonic_ns();
  kinfo("intel-gt: phase=%s t-us=%lu elapsed-us=%lu step-us=%lu\n",
      name, now / 1000, (now - started_ns) / 1000, (now - phase_ns) / 1000);
  phase_ns = now;
}

static bool fail(const char *reason)
{
  fenced = true;
  active = false;
  kerror("intel-gt: %s; requests/mappings retained, fenced until reboot\n", reason);
  return false;
}

static bool overlaps(uint64_t a, uint64_t bytes, uint64_t b, uint64_t size)
{
  return size && (bytes > UINT64_MAX - a || size > UINT64_MAX - b ||
      (a < b + size && b < a + bytes));
}

static bool physical_available(const struct boot_info *boot, uint64_t physical)
{
  if (!arch_pci_mmio_available(physical, PAGE_SIZE) ||
      overlaps(physical, PAGE_SIZE, boot->framebuffer.physical, boot->framebuffer.size)) {
    return false;
  }
  for (size_t n = 0; n < boot->region_count; ++n) {
    const struct boot_region *r = &boot->regions[n];
    if (r->type != BOOT_RESERVED && overlaps(physical, PAGE_SIZE, r->base, r->length)) {
      return false;
    }
  }
  return true;
}

static bool map_page(uint64_t bar, unsigned i, bool writable)
{
  phys_addr_t physical = bar + mappings[i].page;
  uintptr_t *window = &mappings[i].window;
  if (vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, window) != MM_OK) {
    return false;
  }
  enum mm_result result = vm_map_mmio(*window, physical);
  if (result == MM_OK && !writable) {
    result = vm_protect(vm_kernel_space(), *window, 0);
  }
  if (result != MM_OK) {
    struct page_translation translation;
    if (vm_query(vm_kernel_space(), *window, &translation) != MM_OK) {
      if (vm_release(vm_kernel_space(), *window, PAGE_SIZE) != MM_OK) {
        return fail("CPU reservation cleanup failed");
      }
      *window = 0;
    }
    return false;
  }
  return true;
}

static bool map_pages(const struct boot_info *boot, uint64_t bar, uint64_t mask,
    uint64_t writable)
{
  struct vm_physical_range ranges[MMIO_WINDOWS];
  size_t count = 0;
  for (unsigned i = 0; i < mapping_count; ++i) {
    if ((mask & (UINT64_C(1) << i)) && !mappings[i].window) {
      ranges[count] = (struct vm_physical_range){bar + mappings[i].page, PAGE_SIZE};
      if (!physical_available(boot, ranges[count].physical)) {
        return false;
      }
      ++count;
    }
  }
  if (vm_kernel_physical_overlap_any(ranges, count)) {
    return false;
  }
  /* Bootstrap has no AP/interleaved mapper; checked reserved device pages
   * cannot become page-table/allocator backing while filling these windows. */
  for (unsigned i = 0; i < mapping_count; ++i) {
    if ((mask & (UINT64_C(1) << i)) && !mappings[i].window &&
        !map_page(bar, i, writable & (UINT64_C(1) << i))) {
      return false;
    }
  }
  return true;
}

static volatile uint32_t *reg(uint32_t offset)
{
  for (unsigned i = 0; i < mapping_count; ++i) {
    if ((offset & ~(uint32_t)(PAGE_SIZE - 1)) == mappings[i].page) {
      KASSERT(mappings[i].window && !(offset & 3));
      return (volatile uint32_t *)(mappings[i].window + (offset & (PAGE_SIZE - 1)));
    }
  }
  KASSERT(false);
  return NULL;
}

static uint32_t read_reg(uint32_t offset)
{
  return *reg(offset);
}

static bool write_allowed(uint32_t offset, uint32_t value)
{
  for (unsigned i = 0; i < FW_COUNT; ++i) {
    if (forcewake[i].enabled && offset == forcewake[i].request) {
      return value == FORCEWAKE_KERNEL_MASK ||
          value == (FORCEWAKE_KERNEL_MASK | FORCEWAKE_KERNEL);
    }
  }
  return (offset == PAT_INDEX0 + PAT_DISPLAY_INDEX * sizeof(uint32_t) &&
          value == PAT_UC_NONCOHERENT && pat_write_armed) ||
      (offset == MCR_SELECTOR && value == MCR_MULTICAST) ||
      (offset == STEER_SEMAPHORE && value == 1) ||
      (offset == MEDIA_BASE + GMD_ID && value == 0 && write_workaround);
}

static void write_reg(uint32_t offset, uint32_t value)
{
  KASSERT(!fenced && write_allowed(offset, value));
  *reg(offset) = value;
}

static bool wait_ack(unsigned i, bool awake)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint32_t ack = read_reg(forcewake[i].ack);
    if (ack == UINT32_MAX) {
      return false;
    }
    if (!!(ack & FORCEWAKE_KERNEL) == awake) {
      return true;
    }
    cpu_relax();
  } while (arch_monotonic_ns() - start < FORCEWAKE_TIMEOUT_NS);
  return false;
}

static bool hold_forcewake(unsigned i)
{
  KASSERT(i < FW_COUNT && forcewake[i].enabled);
  if (forcewake[i].held) {
    return true;
  }
  uint32_t request = read_reg(forcewake[i].request);
  if (request == UINT32_MAX) {
    return fail("unreliable forcewake request read");
  }
  if (!(request & FORCEWAKE_KERNEL)) {
    forcewake[i].owned = true;
    write_reg(forcewake[i].request,
        FORCEWAKE_KERNEL_MASK | FORCEWAKE_KERNEL);
  }
  if (!wait_ack(i, true)) {
    kerror("intel-consumer: hold acknowledgment failed domain=%s\n", forcewake[i].name);
    return fail("GT forcewake awake acknowledgment failed");
  }
  forcewake[i].held = true;
  kinfo("intel-consumer: hold domain=%s request=%x inherited=%u\n",
      forcewake[i].name, request, !forcewake[i].owned);
  return true;
}

static bool holds_valid(void)
{
  if (read_reg(GMD_ID) != graphics_gmd || read_reg(MEDIA_BASE + GMD_ID) != media_gmd) {
    return fail("graphics/media identity changed");
  }
  for (unsigned i = 0; i < FW_COUNT; ++i) {
    if (!forcewake[i].held) {
      continue;
    }
    uint32_t request = read_reg(forcewake[i].request);
    uint32_t ack = read_reg(forcewake[i].ack);
    if (request == UINT32_MAX || ack == UINT32_MAX ||
        !(request & FORCEWAKE_KERNEL) || !(ack & FORCEWAKE_KERNEL)) {
      return fail("GT forcewake hold changed");
    }
  }
  return true;
}

static bool release_forcewake(unsigned i)
{
  if (!forcewake[i].held && !forcewake[i].owned) {
    return true;
  }
  if (!holds_valid()) {
    return false;
  }
  bool owned = forcewake[i].owned;
  if (owned) {
    write_reg(forcewake[i].request, FORCEWAKE_KERNEL_MASK);
    if (!wait_ack(i, false) || (read_reg(forcewake[i].request) & FORCEWAKE_KERNEL)) {
      return fail("forcewake release acknowledgment failed");
    }
  }
  forcewake[i].held = false;
  forcewake[i].owned = false;
  kinfo("intel-consumer: release domain=%s added=%u confirmed=1\n", forcewake[i].name, owned);
  return true;
}

static bool release_semaphore(void)
{
  /* Linux's handoff leaves multicast selected, never a stale saved selector.
   * Do not read the semaphore here: reading 1 acquires it again. */
  write_reg(STEER_SEMAPHORE, 1);
  semaphore_owned = false;
  return read_reg(GMD_ID) == graphics_gmd || fail("MCR handoff posting read failed");
}

static bool acquire_semaphore(void)
{
  if (!holds_valid()) {
    return false;
  }
  uint64_t start = arch_monotonic_ns();
  do {
    uint32_t value = read_reg(STEER_SEMAPHORE);
    if (value == UINT32_MAX) {
      return fail("unreliable MCR semaphore read");
    }
    if (value & 1) {
      semaphore_owned = true;
      break;
    }
    cpu_relax();
  } while (arch_monotonic_ns() - start < SEMAPHORE_TIMEOUT_NS);
  if (!semaphore_owned) {
    return fail("MCR semaphore acquisition timed out");
  }
  if (!holds_valid()) {
    return false;
  }
  write_reg(MCR_SELECTOR, MCR_MULTICAST);
  return true;
}

static bool read_pat(uint32_t pat[GGTT_PAT_ENTRIES])
{
  if (!acquire_semaphore()) {
    return false;
  }
  for (unsigned i = 0; i < GGTT_PAT_ENTRIES; ++i) {
    pat[i] = read_reg(PAT_INDEX0 + i * sizeof(uint32_t));
    uint32_t second = read_reg(PAT_INDEX0 + i * sizeof(uint32_t));
    if (pat[i] == UINT32_MAX || pat[i] != second || !holds_valid()) {
      return fail("PAT readback/forcewake unstable");
    }
  }
  return release_semaphore();
}

static unsigned select_pat(const uint32_t pat[GGTT_PAT_ENTRIES])
{
  for (unsigned i = 0; i < GGTT_PAT_ENTRIES; ++i) {
    /* Xe2/Xe3: L3/L4 UC, no snoop, compression disabled. CLOS and
     * no-promote do not change this uncached policy; leave them untouched. */
    if ((pat[i] & PAT_CACHE_MASK) == PAT_UC_NONCOHERENT) {
      return i;
    }
  }
  return GGTT_PAT_ENTRIES;
}

static bool drain_writes(void)
{
  if (write_workaround && pending_writes) {
    write_reg(MEDIA_BASE + GMD_ID, 0);
    if (read_reg(MEDIA_BASE + GMD_ID) != media_gmd) {
      return fail("GGTT write workaround posting read failed");
    }
    pending_writes = 0;
  }
  /* xe_ggtt_invalidate uses this tile-level read to post GGTT CPU writes.
   * This performs no GT or display translation invalidation. */
  if (read_reg(VF_CAP) == UINT32_MAX) {
    return fail("GGTT posting read failed");
  }
  return holds_valid();
}

static bool unmap_pages(void)
{
  for (unsigned i = 0; i < mapping_count; ++i) {
    uintptr_t *window = &mappings[i].window;
    if (!*window) {
      continue;
    }
    phys_addr_t physical;
    if (vm_unmap(vm_kernel_space(), *window, &physical) != MM_OK ||
        vm_release(vm_kernel_space(), *window, PAGE_SIZE) != MM_OK) {
      return fail("temporary CPU mapping cleanup failed");
    }
    *window = 0;
  }
  return true;
}

bool intel_gt_profile_fenced(void)
{
  return fenced;
}

bool intel_gt_profile_ready(void)
{
  if (!active || fenced) {
    return false;
  }
  if (cpu_interrupts_enabled()) {
    return fail("bootstrap CPU ownership changed");
  }
  return holds_valid();
}

unsigned intel_gt_profile_pat_index(void)
{
  KASSERT(active && !fenced && pat_admitted && pat_index < GGTT_PAT_ENTRIES);
  return pat_index;
}

bool intel_gt_note_ggtt_write(void)
{
  if (!active || fenced) {
    return false;
  }
  if (cpu_interrupts_enabled()) {
    return fail("bootstrap CPU ownership changed");
  }
  if (write_workaround && ++pending_writes == INTEL_GT_GGTT_WRITE_BATCH) {
    return intel_gt_profile_ready() && drain_writes();
  }
  return true;
}

bool intel_gt_profile_finish(void)
{
  if (!intel_gt_profile_ready() || !drain_writes()) {
    return false;
  }
  for (unsigned i = FW_COUNT; i-- > 0;) {
    if (!release_forcewake(i)) {
      return false;
    }
  }
  active = false;
  if (!unmap_pages()) {
    return false;
  }
  log_phase("released");
  kinfo("intel-gt: bootstrap complete; owned forcewake released, inherited requests preserved\n");
  return true;
}

/* CPU mappings are RO; adding a window does not access an unpowered register. */
static bool map_inventory_registers(const struct boot_info *boot, uint64_t bar,
    const uint32_t *offsets, size_t count)
{
  uint64_t mask = 0;
  for (size_t n = 0; n < count; ++n) {
    uint32_t page = offsets[n] & ~(uint32_t)(PAGE_SIZE - 1);
    size_t i = 0;
    while (i < mapping_count && mappings[i].page != page) {
      ++i;
    }
    if (i == mapping_count) {
      if (mapping_count == MMIO_WINDOWS) {
        return false;
      }
      mappings[mapping_count++].page = page;
    }
    mask |= UINT64_C(1) << i;
  }
  return map_pages(boot, bar, mask, 0);
}

static bool engine_present(size_t i, uint32_t ccs, uint32_t media)
{
  unsigned mask;
  switch (engines[i].presence) {
  case ENGINE_ALWAYS: return true;
  case ENGINE_CCS: mask = (ccs >> 16) & 0xf; break;
  case ENGINE_VDBOX: mask = media & 0xff; break;
  case ENGINE_VEBOX: mask = (media >> 16) & 0xf; break;
  default: KASSERT(false); return false;
  }
  return mask & (1u << engines[i].bit);
}

static bool inventory_consumers(const struct boot_info *boot, uint64_t bar)
{
  if (!map_inventory_registers(boot, bar, inventory_registers,
          sizeof(inventory_registers) / sizeof(*inventory_registers))) {
    kinfo("intel-consumer: unavailable: read-only topology/status windows\n");
    return false;
  }
  if (!holds_valid()) {
    return false;
  }
  uint32_t ccs = read_reg(0x009114), media = read_reg(0x389140);
  if (ccs == UINT32_MAX || media == UINT32_MAX ||
      ccs != read_reg(0x009114) || media != read_reg(0x389140) || !holds_valid()) {
    kinfo("intel-consumer: unavailable: topology fuse read unstable or unqualified\n");
    return false;
  }
  kinfo("intel-consumer: topology graphics=3001 media=3000 CCS_FUSE=%x MEDIA_FUSE=%x ccs=%x vdbox=%x vebox=%x\n",
      ccs, media, (ccs >> 16) & 0xf, media & 0xff, (media >> 16) & 0xf);
  uint32_t offsets[sizeof(engines) / sizeof(*engines)];
  size_t count = 0;
  for (size_t i = 0; i < sizeof(engines) / sizeof(*engines); ++i) {
    if (engine_present(i, ccs, media)) {
      offsets[count++] = engines[i].base;
      forcewake[engines[i].domain].enabled = true;
    }
  }
  if (!map_inventory_registers(boot, bar, offsets, count)) {
    kinfo("intel-consumer: unavailable: read-only engine windows\n");
    return false;
  }
  log_phase("consumer-windows-ready");
  consumer_ccs = ccs;
  consumer_media = media;
  for (size_t i = 0; i < sizeof(engines) / sizeof(*engines); ++i) {
    if (engine_present(i, ccs, media) && !hold_forcewake(engines[i].domain)) {
      return false;
    }
  }
  consumers_mapped = true;
  return true;
}

static bool engine_idle(const uint32_t values[8])
{
  /* Modern execution queue: invalid, no valid/pending/active context.
   * Do not use the Gen8 port masks or demand the undefined reset context ID. */
  return values[0] != UINT32_MAX && values[0] == values[1] && !values[2] &&
      (values[4] & (EXECLIST_QUEUE_INVALID | EXECLIST_PENDING)) ==
          EXECLIST_QUEUE_INVALID && !values[6];
}

static bool check_consumers(void)
{
  if (!consumers_mapped || !holds_valid() ||
      read_reg(0x009114) != consumer_ccs || read_reg(0x389140) != consumer_media) {
    return false;
  }
  uint32_t guc[2][2];
  bool qualified = true;
  for (unsigned i = 0; i < 2; ++i) {
    uint32_t offset = i ? 0x38c000 : 0x00c000;
    guc[i][0] = read_reg(offset);
    guc[i][1] = read_reg(offset);
    qualified &= guc[i][0] != UINT32_MAX && guc[i][0] == guc[i][1] &&
        (guc[i][0] & GUC_MIA_IN_RESET);
    kinfo("intel-consumer: guc gt=%s status=%x/%x reset-bit=%u/%u; submitter-check\n",
        i ? "media" : "graphics", guc[i][0], guc[i][1], guc[i][0] & 1, guc[i][1] & 1);
  }
  /* Accepted boot-only submitter proof; no context memory is dereferenced. */
  static const uint32_t registers[] = {0x34, 0x30, 0x3c, 0x9c, 0x234, 0x238, 0x240, 0x29c};
  for (size_t i = 0; i < sizeof(engines) / sizeof(*engines); ++i) {
    if (!engine_present(i, consumer_ccs, consumer_media)) {
      continue;
    }
    if (!hold_forcewake(engines[i].domain) || !holds_valid()) {
      return false;
    }
    uint32_t values[2][sizeof(registers) / sizeof(*registers)];
    for (unsigned sample = 0; sample < 2; ++sample) {
      for (size_t n = 0; n < sizeof(registers) / sizeof(*registers); ++n) {
        values[sample][n] = read_reg(engines[i].base + registers[n]);
      }
      qualified &= engine_idle(values[sample]);
      if (sample) {
        qualified &= values[0][0] == values[1][0] && values[0][1] == values[1][1] &&
            values[0][2] == values[1][2] && values[0][6] == values[1][6] &&
            !((values[0][4] ^ values[1][4]) & (EXECLIST_QUEUE_INVALID | EXECLIST_PENDING));
      }
      kinfo("intel-consumer: engine=%s domain=%s base=%x sample=%u head=%x tail=%x ctl=%x mi=%x el=%x/%x lrca=%x mode=%x\n",
          engines[i].name, forcewake[engines[i].domain].name, engines[i].base, sample,
          values[sample][0], values[sample][1], values[sample][2], values[sample][3],
          values[sample][4], values[sample][5], values[sample][6], values[sample][7]);
    }
    if (!holds_valid()) {
      return false;
    }
  }
  /* GSCCS is part of this descriptor even if Linux has no firmware file.
   * Status is read only after the matching GSC domain was acknowledged. */
  KASSERT(forcewake[FW_GSC].held);
  uint32_t gsc = read_reg(0x116c40), gsc_second = read_reg(0x116c40);
  qualified &= gsc != UINT32_MAX && gsc == gsc_second && !(gsc & GSC_INIT_COMPLETE);
  kinfo("intel-consumer: gsc FWSTS1=%x/%x loaded-bit=%u/%u; submitter-check\n",
      gsc, gsc_second, !!(gsc & (1u << 9)), !!(gsc_second & (1u << 9)));
  if (!holds_valid()) {
    return false;
  }
  kinfo("intel-consumer: submitter proof qualified=%u; host submission/firmware start excluded at bootstrap\n",
      qualified);
  log_phase("consumer-proof-checked");
  return qualified;
}

bool intel_gt_profile_admit_pat(bool live_gop_pat3_unused)
{
  if (!intel_gt_profile_ready() || pat_admitted || !check_consumers()) {
    kinfo("intel-gt: PAT admission refused: submitter proof incomplete; keeping GOP copy\n");
    return false;
  }
  uint32_t pat[GGTT_PAT_ENTRIES];
  if (!read_pat(pat)) {
    return false;
  }
  pat_index = select_pat(pat);
  if (pat_index == GGTT_PAT_ENTRIES) {
    if (!live_gop_pat3_unused) {
      kinfo("intel-gt: PAT3 has a live GOP consumer; keeping its policy and GOP copy\n");
      return false;
    }
    /* Only this page becomes writable, after the live GOP and submitter
     * proof. The whitelist admits exactly PAT3=0x3c. */
    if (vm_protect(vm_kernel_space(), mappings[1].window, PAGE_WRITE) != MM_OK ||
        !check_consumers() || !acquire_semaphore()) {
      return false;
    }
    uint32_t before = read_reg(PAT_INDEX0 + PAT_DISPLAY_INDEX * sizeof(uint32_t));
    if (before == UINT32_MAX || !holds_valid()) {
      return fail("PAT3 pre-write state uncertain");
    }
    pat_write_armed = true;
    write_reg(PAT_INDEX0 + PAT_DISPLAY_INDEX * sizeof(uint32_t), PAT_UC_NONCOHERENT);
    pat_write_armed = false;
    uint32_t after = read_reg(PAT_INDEX0 + PAT_DISPLAY_INDEX * sizeof(uint32_t));
    kinfo("intel-gt: PAT3 programmed before=%x after=%x; PAT0-2 untouched, submitter/live-GOP proof confirmed\n",
        before, after);
    if (!release_semaphore() || !holds_valid()) {
      return false;
    }
    /* Admit the required cache policy, not equality of unrelated/reserved bits. */
    if ((after & PAT_CACHE_MASK) != PAT_UC_NONCOHERENT) {
      return fail("PAT3 uncached display policy not established");
    }
    pat_index = PAT_DISPLAY_INDEX;
    log_phase("PAT3-programmed");
  }
  pat_admitted = true;
  kinfo("intel-gt: PAT%u admitted for new UMA PTEs only\n", pat_index);
  return true;
}

bool intel_gt_profile_prepare(const struct boot_info *boot, uint64_t bar)
{
  if (attempted || cpu_interrupts_enabled()) {
    return false;
  }
  attempted = true;
  started_ns = phase_ns = arch_monotonic_ns();
  log_phase("begin");
  if (!boot || !bar || (bar & (PAGE_SIZE - 1)) || bar > UINT64_MAX - 0x400000) {
    return false;
  }
  /* GMD has an early, forcewake-free read path in xe_pci.c. */
  if (!map_pages(boot, bar, (1u << 0) | (1u << 4), (1u << 0) | (1u << 4))) {
    if (!fenced) {
      unmap_pages();
    }
    return false;
  }
  log_phase("identity-windows-ready");
  graphics_gmd = read_reg(GMD_ID);
  media_gmd = read_reg(MEDIA_BASE + GMD_ID);
  unsigned release = (graphics_gmd >> GMD_RELEASE_SHIFT) & 0xff;
  bool profile = (graphics_gmd >> GMD_ARCH_SHIFT) == 30 &&
      (release == 0 || release == 1 || release == 3 || release == 4 || release == 5) &&
      (media_gmd >> GMD_ARCH_SHIFT) == 30 &&
      !((media_gmd >> GMD_RELEASE_SHIFT) & 0xff) &&
      read_reg(GMD_ID) == graphics_gmd && read_reg(MEDIA_BASE + GMD_ID) == media_gmd;
  kinfo("intel-gt: graphics GMD=%x media GMD=%x profile=%u\n", graphics_gmd, media_gmd, profile);
  if (!profile) {
    unmap_pages();
    return false;
  }
  write_workaround = (media_gmd & GMD_STEP_MASK) < GMD_STEP_B0;
  bool inventory_profile = release == 1;
  unsigned mask = (1u << 1) | (1u << 2) | (1u << 3) | ((write_workaround || inventory_profile) ? 1u << 5 : 0);
  if (!map_pages(boot, bar, mask, (1u << 2) | (1u << 5))) {
    if (!fenced) {
      unmap_pages();
    }
    return false;
  }
  log_phase("cache-windows-ready");
  forcewake[FW_GT].enabled = true;
  forcewake[FW_MEDIA_GT].enabled = write_workaround || inventory_profile;
  if (!hold_forcewake(FW_GT) ||
      (forcewake[FW_MEDIA_GT].enabled && !hold_forcewake(FW_MEDIA_GT))) {
    return false;
  }
  active = true;
  log_phase("forcewake-acknowledged");
  if (!inventory_profile || !inventory_consumers(boot, bar) || !check_consumers()) {
    if (!fenced) {
      intel_gt_profile_finish();
    }
    return false;
  }
  uint32_t pat[GGTT_PAT_ENTRIES];
  if (!read_pat(pat)) {
    return false;
  }
  log_phase("PAT-read");
  kinfo("intel-gt: inherited PAT0=%x PAT1=%x PAT2=%x PAT3=%x media-A0-write-WA=%u\n",
      pat[0], pat[1], pat[2], pat[3], write_workaround);
  /* Keep every qualified hold until GGTT import and the final submitter
   * recheck. Pyxis has no other GT submitter or firmware-start path. */
  return true;
}

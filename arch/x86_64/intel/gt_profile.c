/* Portions adapted from Linux: Copyright © 2022, 2023, 2024 Intel Corporation.
 * Linux v6.19 05f7e89a: xe_force_wake.c, xe_gt_mcr.c, xe_pat.c,
 * xe_ggtt.c and xe_wa_oob.rules. See NOTICE. */
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
#define GGTT_WRITE_BATCH 63u

static struct {
  uint32_t page;
  uintptr_t window;
} mappings[] = {
  {0x000000, 0}, {0x004000, 0}, {0x00a000, 0},
  {0x190000, 0}, {0x380000, 0}, {0x38a000, 0},
};
static struct {
  uint32_t base;
  bool held, owned;
} forcewake[] = {{0, false, false}, {MEDIA_BASE, false, false}};
static uint32_t graphics_gmd, media_gmd;
static unsigned pending_writes;
static unsigned pat_index = GGTT_PAT_ENTRIES;
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

static bool map_pages(const struct boot_info *boot, uint64_t bar, unsigned mask,
    unsigned writable)
{
  struct vm_physical_range ranges[sizeof(mappings) / sizeof(mappings[0])];
  size_t count = 0;
  for (unsigned i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
    if (mask & (1u << i)) {
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
  for (unsigned i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
    if ((mask & (1u << i)) && !map_page(bar, i, writable & (1u << i))) {
      return false;
    }
  }
  return true;
}

static volatile uint32_t *reg(uint32_t offset)
{
  for (unsigned i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
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

static void write_reg(uint32_t offset, uint32_t value)
{
  KASSERT(!fenced);
  KASSERT((offset == GT_FORCEWAKE || offset == MEDIA_BASE + GT_FORCEWAKE) ?
      value == FORCEWAKE_KERNEL_MASK || value == (FORCEWAKE_KERNEL_MASK | FORCEWAKE_KERNEL) :
      (offset == MCR_SELECTOR && value == MCR_MULTICAST) ||
      (offset == STEER_SEMAPHORE && value == 1) ||
      (offset == MEDIA_BASE + GMD_ID && value == 0 && write_workaround));
  *reg(offset) = value;
}

static bool wait_ack(unsigned i, bool awake)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint32_t ack = read_reg(forcewake[i].base + GT_ACK);
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
  uint32_t request = read_reg(forcewake[i].base + GT_FORCEWAKE);
  if (request == UINT32_MAX) {
    return fail("unreliable forcewake request read");
  }
  if (!(request & FORCEWAKE_KERNEL)) {
    forcewake[i].owned = true;
    write_reg(forcewake[i].base + GT_FORCEWAKE,
        FORCEWAKE_KERNEL_MASK | FORCEWAKE_KERNEL);
  }
  if (!wait_ack(i, true)) {
    return fail("GT forcewake awake acknowledgment failed");
  }
  forcewake[i].held = true;
  return true;
}

static bool holds_valid(void)
{
  if (read_reg(GMD_ID) != graphics_gmd || read_reg(MEDIA_BASE + GMD_ID) != media_gmd) {
    return fail("graphics/media identity changed");
  }
  for (unsigned i = 0; i < 2; ++i) {
    if (!forcewake[i].held) {
      continue;
    }
    uint32_t request = read_reg(forcewake[i].base + GT_FORCEWAKE);
    uint32_t ack = read_reg(forcewake[i].base + GT_ACK);
    if (request == UINT32_MAX || ack == UINT32_MAX ||
        !(request & FORCEWAKE_KERNEL) || !(ack & FORCEWAKE_KERNEL)) {
      return fail("GT forcewake hold changed");
    }
  }
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

static bool read_pat(uint32_t pat[GGTT_PAT_ENTRIES])
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
  for (unsigned i = 0; i < GGTT_PAT_ENTRIES; ++i) {
    pat[i] = read_reg(PAT_INDEX0 + i * sizeof(uint32_t));
    uint32_t second = read_reg(PAT_INDEX0 + i * sizeof(uint32_t));
    if (pat[i] != second || !holds_valid()) {
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
  for (unsigned i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
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
  KASSERT(active && !fenced && pat_index < GGTT_PAT_ENTRIES);
  return pat_index;
}

bool intel_gt_note_ggtt_write(void)
{
  if (!intel_gt_profile_ready()) {
    return false;
  }
  if (write_workaround && ++pending_writes == GGTT_WRITE_BATCH) {
    return drain_writes();
  }
  return true;
}

bool intel_gt_profile_finish(void)
{
  if (!intel_gt_profile_ready() || !drain_writes()) {
    return false;
  }
  for (unsigned i = 2; i-- > 0;) {
    if (!forcewake[i].owned) {
      continue;
    }
    if (!holds_valid()) {
      return false;
    }
    write_reg(forcewake[i].base + GT_FORCEWAKE, FORCEWAKE_KERNEL_MASK);
    if (!wait_ack(i, false) ||
        (read_reg(forcewake[i].base + GT_FORCEWAKE) & FORCEWAKE_KERNEL)) {
      return fail("GT forcewake release acknowledgment failed");
    }
    forcewake[i].held = false;
    forcewake[i].owned = false;
  }
  active = false;
  if (!unmap_pages()) {
    return false;
  }
  log_phase("released");
  kinfo("intel-gt: bootstrap complete; owned forcewake released, inherited requests preserved\n");
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
  if (!boot || !bar || (bar & (PAGE_SIZE - 1)) || bar > UINT64_MAX - MEDIA_BASE - 0xb000) {
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
  unsigned mask = (1u << 1) | (1u << 2) | (1u << 3) | (write_workaround ? 1u << 5 : 0);
  if (!map_pages(boot, bar, mask, (1u << 2) | (1u << 5))) {
    if (!fenced) {
      unmap_pages();
    }
    return false;
  }
  log_phase("cache-windows-ready");
  if (!hold_forcewake(0) || (write_workaround && !hold_forcewake(1))) {
    return false;
  }
  log_phase("forcewake-acknowledged");
  uint32_t pat[GGTT_PAT_ENTRIES];
  if (!read_pat(pat)) {
    return false;
  }
  active = true;
  log_phase("PAT-read");
  kinfo("intel-gt: inherited PAT0=%x PAT1=%x PAT2=%x PAT3=%x media-A0-write-WA=%u\n",
      pat[0], pat[1], pat[2], pat[3], write_workaround);
  pat_index = select_pat(pat);
  if (pat_index == GGTT_PAT_ENTRIES) {
    if (intel_gt_profile_finish()) {
      kinfo("intel-gt: no inherited GGTT-reachable uncached PAT entry; keeping GOP copy\n");
    }
    return false;
  }
  kinfo("intel-gt: selected inherited PAT%u=%x for new UMA PTEs only; table unchanged\n",
      pat_index, pat[pat_index]);
  return intel_gt_profile_ready();
}

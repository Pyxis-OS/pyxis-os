/* Intel inherited-mode plane flips. Linux v6.19 reference adaptations: NOTICE. */
#include <arch/intel/display.h>
#include <arch/intel/dma.h>
#include <arch/intel/gt_profile.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/display.h>
#include <kernel/boot/options.h>
#include <kernel/mm/scanout.h>
#include <kernel/mm/heap.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include "display_internal.h"
#include "display_regs.h"

#define PIPE_A_FRAME_COUNT 0x70040u
#define PIPE_A_FLIP_COUNT 0x70044u
#define PIPE_A_FLIP_DONE_TIME 0x70054u
#define PIPE_A_MISC2 0x7002cu
#define PIPE_A_IIR 0x44408u
#define PIPE_A_IMR 0x44404u
#define PIPE_A_IER 0x4440cu
#define DISPLAY_TIMESTAMP 0x44070u
#define TRANS_A_VRR 0x60420u
#define TRANS_A_VRR_VMAX 0x60424u
#define TRANS_A_VRR_VMIN 0x60434u
#define TRANS_A_VRR_FLIPLINE 0x60438u
#define VRR_TIMING_ENABLE (1u << 31)
#define VRR_FLIPLINE_ENABLE (1u << 29)
#define VRR_CMRR_ENABLE (1u << 27)
#define VRR_GUARDBAND_MASK 0xffffu
#define VRR_TIMING_MASK (VRR_TIMING_ENABLE | VRR_FLIPLINE_ENABLE | VRR_CMRR_ENABLE | VRR_GUARDBAND_MASK)
#define TRANS_A_CONTEXT_LATENCY 0x6007cu
#define PLANE1_DONE (1u << 3)
#define PIPE_FAULTS ((1u << 31) | (1u << 18) | (1u << 7))
#define GGTT_RESERVED_LOW UINT64_C(0x800000)
#define GGTT_ALLOC_END UINT64_C(0xfee00000)
#define TIMING_TIMEOUT_NS UINT64_C(200000000)
#define SETUP_TIMEOUT_NS UINT64_C(5000000000)
#define FLIP_TIMEOUT_NS UINT64_C(100000000)
#define KEEPOUT_US 100u
#define SCANLINE_MASK 0x1fffu
#define PLANE1 (PLANE_BASE(0, 0))

static enum intel_flip_state state;
static struct framebuffer surfaces[3];
static struct scanout_pages pages[2];
static uint32_t gpu_surfaces[3];
static uint32_t inherited_control, inherited_color, inherited_stride, inherited_size;
static uint32_t inherited_trans, inherited_ddi, inherited_misc, inherited_misc2;
static uint32_t inherited_timing[6], inherited_source, inherited_context_latency;
static uint32_t inherited_vrr, inherited_vmin, inherited_vmax, inherited_flipline;
static uint32_t front, back = 1, pending, sequence;
static uint32_t submit_frame, submit_flip, submit_done_time, submit_clock;
static uint64_t deadline;
static uint64_t setup_deadline;
static unsigned vtotal, vblank_start, keepout_lines;
static uint64_t submitted, confirmed, deferred, timeouts;
static bool metrics, attempted;

static enum intel_flip_state fail(const char *reason)
{
  state = INTEL_FLIP_FAILED;
  intel_display_fence(reason);
  kerror("intel-flip: %s; surfaces/mappings retained until reboot, no further writes\n", reason);
  return state;
}

static bool setup_in_time(const char *phase)
{
  if (arch_monotonic_ns() < setup_deadline) {
    return true;
  }
  kinfo("intel-flip: setup deadline during %s; ordinary GOP copying requested\n", phase);
  return false;
}

static bool fixed_layout(void)
{
  if (!intel_display_holds_valid() || !intel_display_single_pipe_a() || display_is_panicking()) {
    return false;
  }
  if (intel_display_read(TRANS_CONF(0)) != inherited_trans ||
      intel_display_read(TRANS_DDI(0)) != inherited_ddi ||
      intel_display_read(PIPE_SOURCE(0)) != inherited_source ||
      intel_display_read(PIPE_MISC(0)) != inherited_misc ||
      intel_display_read(PIPE_A_MISC2) != inherited_misc2 ||
      intel_display_read(TRANS_A_CONTEXT_LATENCY) != inherited_context_latency ||
      (intel_display_read(PSR_CONTROL(0)) & TRANS_ENABLE) ||
      (intel_display_read(PSR2_CONTROL(0)) & TRANS_ENABLE) ||
      (intel_display_read(TRANS_DP2(0)) & (1u << 30)) ||
      ((intel_display_read(TRANS_A_VRR) ^ inherited_vrr) & VRR_TIMING_MASK) ||
      intel_display_read(PLANE1 + PLANE_CONTROL) != inherited_control ||
      intel_display_read(PLANE1 + PLANE_COLOR) != inherited_color ||
      intel_display_read(PLANE1 + PLANE_STRIDE) != inherited_stride ||
      intel_display_read(PLANE1 + PLANE_SIZE) != inherited_size ||
      intel_display_read(PLANE1 + PLANE_POSITION) ||
      intel_display_read(PLANE1 + PLANE_OFFSET) ||
      (intel_display_read(CURSOR_BASE(0)) & CURSOR_MODE_MASK) ||
      (intel_display_read(JOINER_BASE(0)) & JOINER_MASK) ||
      (intel_display_read(JOINER_BASE(0) + 4) & JOINER_MASK) ||
      (intel_display_read(PIPE_A_IIR) & PIPE_FAULTS)) {
    return false;
  }
  if ((inherited_vrr & VRR_TIMING_ENABLE) &&
      (intel_display_read(TRANS_A_VRR_VMIN) != inherited_vmin ||
       intel_display_read(TRANS_A_VRR_VMAX) != inherited_vmax ||
       intel_display_read(TRANS_A_VRR_FLIPLINE) != inherited_flipline)) {
    return false;
  }
  for (unsigned i = 0; i < 6; ++i) {
    if (intel_display_read(TRANS_BASE(0) + i * 4) != inherited_timing[i]) {
      return false;
    }
  }
  for (unsigned p = 1; p < PLANE_COUNT; ++p) {
    if (intel_display_read(PLANE_BASE(0, p)) & PLANE_ENABLE) {
      return false;
    }
  }
  return true;
}

static bool read_fixed_timing(void)
{
  inherited_context_latency = intel_display_read(TRANS_A_CONTEXT_LATENCY);
  inherited_vrr = intel_display_read(TRANS_A_VRR);
  inherited_vmin = intel_display_read(TRANS_A_VRR_VMIN);
  inherited_vmax = intel_display_read(TRANS_A_VRR_VMAX);
  inherited_flipline = intel_display_read(TRANS_A_VRR_FLIPLINE);
  kinfo("intel-flip: timing CTL=%x VMIN=%x VMAX=%x FLIPLINE=%x VTOTAL=%x SCL=%x\n",
      inherited_vrr, inherited_vmin, inherited_vmax, inherited_flipline,
      inherited_timing[3], inherited_context_latency);
  if (inherited_vrr & VRR_TIMING_ENABLE) {
    /* Display30 uses this timing generator even for fixed refresh. Linux's
     * intel_vrr_get_config() identifies fixed RR by equal min/max/flipline,
     * derives vtotal from VMIN and vblank start from its guardband. */
    if (!(inherited_vrr & VRR_FLIPLINE_ENABLE) ||
        (inherited_vrr & VRR_CMRR_ENABLE) ||
        inherited_vmin != inherited_vmax || inherited_vmin != inherited_flipline) {
      kinfo("intel-flip: unavailable: timing generator is not fixed refresh\n");
      return false;
    }
    vtotal = inherited_vmin + 1;
    unsigned guardband = inherited_vrr & VRR_GUARDBAND_MASK;
    if (!vtotal || vtotal > SCANLINE_MASK + 1 || guardband >= vtotal) {
      kinfo("intel-flip: unavailable: fixed timing exceeds scanline/guardband range\n");
      return false;
    }
    vblank_start = vtotal - guardband;
  } else {
    vtotal = (inherited_timing[3] >> 16) + 1;
    /* Display13+ ignores VBLANK_START; non-VRR timing uses active + SCL. */
    vblank_start = (inherited_timing[3] & 0xffff) + 1 + inherited_context_latency;
  }
  return true;
}

static bool qualify_timing(void)
{
  uint32_t clock0 = intel_display_read(DISPLAY_TIMESTAMP);
  uint64_t start = arch_monotonic_ns();
  uint32_t frame0 = intel_display_read(PIPE_A_FRAME_COUNT);
  uint32_t frame = frame0, first_frame = 0, first_stamp = 0;
  bool have_first = false;
  while (arch_monotonic_ns() - start < TIMING_TIMEOUT_NS) {
    uint32_t next = intel_display_read(PIPE_A_FRAME_COUNT);
    if (next == frame) {
      cpu_relax();
      continue;
    }
    frame = next;
    uint32_t stamp = intel_display_read(FRAME_TIMESTAMP(0));
    if (intel_display_read(PIPE_A_FRAME_COUNT) != frame) {
      continue;
    }
    if (!have_first) {
      first_frame = frame;
      first_stamp = stamp;
      have_first = true;
      continue;
    }
    uint32_t count = frame - first_frame;
    uint32_t elapsed = stamp - first_stamp;
    uint64_t wall = arch_monotonic_ns() - start;
    uint64_t ticks_ns = (uint64_t)(intel_display_read(DISPLAY_TIMESTAMP) - clock0) * 1000;
    if (!count || !elapsed || ticks_ns < wall * 95 / 100 || ticks_ns > wall * 105 / 100) {
      return false;
    }
    uint64_t period_us = elapsed / count;
    if (!period_us || period_us * 1000 > FLIP_TIMEOUT_NS / 2) {
      return false;
    }
    keepout_lines = ((uint64_t)vtotal * KEEPOUT_US + period_us - 1) / period_us;
    if (!keepout_lines || keepout_lines >= vblank_start) {
      return false;
    }
    unsigned htotal = (inherited_timing[0] >> 16) + 1;
    uint64_t pixel_khz = (uint64_t)htotal * vtotal * 1000 / period_us;
    kinfo("intel-flip: fixed timing period=%lu us htotal=%u vtotal=%u pixel-clock=%lu kHz keepout=%u lines; timestamp unit checked\n",
        period_us, htotal, vtotal, pixel_khz, keepout_lines);
    return true;
  }
  return false;
}

static bool choose_ggtt(size_t count)
{
  /* Import every nonzero inherited entry, then allocate only within our
   * exclusive ledger above maximum WOPCM and below the reference GuC ceiling. */
  kinfo("intel-flip: setup importing GGTT ownership\n");
  if (!intel_display_prepare_gtt(setup_deadline)) {
    return false;
  }
  size_t entries = GGTT_ADDRESS_BYTES / PAGE_SIZE;
  uint8_t *occupied = kmalloc(entries / 8);
  if (!occupied) {
    return false;
  }
  memset(occupied, 0, entries / 8);
  for (size_t i = 0; i < entries; ++i) {
    uint64_t value;
    if (!(i % (PAGE_SIZE / sizeof(uint64_t))) && !setup_in_time("GGTT import")) {
      kfree(occupied);
      return false;
    }
    if (!intel_display_gtt_read(i * PAGE_SIZE, &value)) {
      kfree(occupied);
      return false;
    }
    if (value || i * PAGE_SIZE < GGTT_RESERVED_LOW || i * PAGE_SIZE >= GGTT_ALLOC_END) {
      occupied[i / 8] |= 1u << (i % 8);
    }
  }
  size_t run = 0;
  for (size_t i = GGTT_RESERVED_LOW / PAGE_SIZE; i < GGTT_ALLOC_END / PAGE_SIZE; ++i) {
    run = occupied[i / 8] & (1u << (i % 8)) ? 0 : run + 1;
    if (run == 2 * count) {
      gpu_surfaces[1] = (i + 1 - run) * PAGE_SIZE;
      gpu_surfaces[2] = gpu_surfaces[1] + count * PAGE_SIZE;
      kfree(occupied);
      return true;
    }
  }
  kfree(occupied);
  return false;
}

static bool abandon_setup(const char *reason)
{
  /* No SURF has been armed: uncertain auxiliary/GGTT state need not prevent
   * CPU copying into a still-proven GOP front. Keep every new resource and
   * hardware hold, fencing register writes without disabling presentation. */
  if (fixed_layout() && intel_display_read(PLANE1 + PLANE_SURFACE) == gpu_surfaces[0] &&
      intel_display_read(PLANE1 + PLANE_LIVE) == gpu_surfaces[0]) {
    state = INTEL_FLIP_OFF;
    intel_display_fence(reason);
    kwarn("intel-flip: %s before SURF; ordinary GOP copying continues; setup resources retained until reboot\n", reason);
  } else {
    fail(reason);
  }
  return false;
}

bool intel_flip_prepare_inherited(const struct boot_info *boot, struct pci_device *device,
    uint64_t bar, uint64_t aperture)
{
  if (attempted) {
    return false;
  }
  attempted = true;
  setup_deadline = arch_monotonic_ns() + SETUP_TIMEOUT_NS;
  const struct boot_framebuffer *fb = &boot->framebuffer;
  inherited_trans = intel_display_read(TRANS_CONF(0));
  inherited_ddi = intel_display_read(TRANS_DDI(0));
  inherited_source = intel_display_read(PIPE_SOURCE(0));
  inherited_misc = intel_display_read(PIPE_MISC(0));
  inherited_misc2 = intel_display_read(PIPE_A_MISC2);
  inherited_control = intel_display_read(PLANE1 + PLANE_CONTROL);
  inherited_stride = intel_display_read(PLANE1 + PLANE_STRIDE);
  inherited_size = intel_display_read(PLANE1 + PLANE_SIZE);
  inherited_color = intel_display_read(PLANE1 + PLANE_COLOR);
  gpu_surfaces[0] = intel_display_read(PLANE1 + PLANE_SURFACE);
  for (unsigned i = 0; i < 6; ++i) {
    inherited_timing[i] = intel_display_read(TRANS_BASE(0) + i * 4);
  }
  kinfo("intel-flip: admission conf=%x control=%x color=%x misc2=%x IIR=%x VRR=%x\n",
      inherited_trans, inherited_control, inherited_color, inherited_misc2,
      intel_display_read(PIPE_A_IIR), intel_display_read(TRANS_A_VRR));
  if (!read_fixed_timing()) {
    return false;
  }
  bool linear = !(inherited_control & (PLANE_TILING_MASK | PLANE_COMPRESSION_MASK |
      PLANE_ROTATION_MASK | PLANE_FLIP_HORIZONTAL | PLANE_RGBX | PLANE_ASYNC_FLIP)) &&
      (inherited_control & PLANE_FORMAT_MASK) == PLANE_FORMAT_8888;
  if (!(inherited_trans & TRANS_ENABLE) || !(inherited_control & PLANE_ENABLE) ||
      !linear || vblank_start >= vtotal || (inherited_color & 0x30) || (inherited_trans & (7u << 21)) || (inherited_misc2 & 7) ||
      (gpu_surfaces[0] & ~PLANE_SURFACE_MASK) ||
      gpu_surfaces[0] != intel_display_read(PLANE1 + PLANE_LIVE) ||
      (inherited_size & 0xffff) + 1 != fb->width ||
      (inherited_size >> 16) + 1 != fb->height ||
      (inherited_stride & PLANE_STRIDE_MASK) * LINEAR_STRIDE_BYTES != fb->pitch ||
      fb->red_shift != 16 || fb->green_shift != 8 || fb->blue_shift != 0 ||
      fb->physical < aperture || !fixed_layout() || !qualify_timing()) {
    kinfo("intel-flip: unavailable: inherited fixed linear pipe/plane/timing not qualified\n");
    return false;
  }
  size_t count = (fb->size + PAGE_SIZE - 1) / PAGE_SIZE;
  for (size_t i = 0; i < count; ++i) {
    uint64_t value, expected = fb->physical - aperture + i * PAGE_SIZE;
    if (!intel_display_gtt_read(gpu_surfaces[0] + i * PAGE_SIZE, &value) ||
        (value & ~GGTT_PTE_PAT_MASK) != (expected | GGTT_PTE_PRESENT | GGTT_PTE_DM)) {
      kinfo("intel-flip: unavailable: complete GOP alias changed\n");
      return false;
    }
  }
  kinfo("intel-flip: setup DMA admission; total budget=%lu ms\n", SETUP_TIMEOUT_NS / 1000000);
  if (!setup_in_time("DMA admission") || !intel_display_claim() ||
      !intel_dma_identity(boot, device->address)) {
    kinfo("intel-flip: unavailable: exclusive device/DMA admission failed\n");
    return false;
  }
  if (!setup_in_time("GT cache admission")) {
    return false;
  }
  if (!intel_gt_profile_prepare(boot, bar)) {
    if (intel_gt_profile_fenced()) {
      return abandon_setup("GT setup ownership uncertain");
    }
    return false;
  }
  /* Cache policy is a prerequisite: reject it before scanning all GGTT PTEs. */
  uint64_t pat = GGTT_PTE_PAT(intel_gt_profile_pat_index());
  if (!choose_ggtt(count)) {
    if (!intel_gt_profile_finish()) {
      return abandon_setup("GT cleanup after GGTT import refusal uncertain");
    }
    kinfo("intel-flip: unavailable: GGTT import refused; keeping GOP copy\n");
    return false;
  }
  kinfo("intel-flip: setup preparing two UMA back buffers\n");
  if (!scanout_pages_prepare(fb->size, setup_deadline, &pages[0]) ||
      !scanout_pages_prepare(fb->size, setup_deadline, &pages[1])) {
    if (pages[0].address) {
      scanout_pages_discard(&pages[0]);
    }
    if (!intel_gt_profile_finish()) {
      return abandon_setup("GT cleanup after backing allocation failure uncertain");
    }
    kinfo("intel-flip: backing allocation failed or setup deadline expired; keeping GOP copy\n");
    return false;
  }
  surfaces[0] = (struct framebuffer){fb->address, fb->size, fb->pitch,
      fb->width, fb->height, fb->red_shift, fb->green_shift, fb->blue_shift};
  for (unsigned n = 0; n < 2; ++n) {
    surfaces[n + 1] = surfaces[0];
    surfaces[n + 1].address = pages[n].address;
    for (size_t i = 0; i < count; ++i) {
      uint64_t old;
      if (!setup_in_time("GGTT mapping publication")) {
        return abandon_setup("GGTT setup deadline");
      }
      if (!intel_display_holds_valid() || !intel_gt_profile_ready() ||
          !intel_display_gtt_read(gpu_surfaces[n + 1] + i * PAGE_SIZE, &old) || old) {
        return abandon_setup("GGTT ownership changed before new mapping");
      }
      uint64_t *entry = intel_display_gtt_write_window(gpu_surfaces[n + 1] + i * PAGE_SIZE);
      if (!entry) {
        return abandon_setup("GGTT mapping access uncertain");
      }
      *(volatile uint64_t *)entry = pages[n].frames[i] | GGTT_PTE_PRESENT | pat;
      if (!intel_gt_note_ggtt_write()) {
        return abandon_setup("GGTT write completion uncertain");
      }
    }
  }
  if (!intel_gt_profile_finish() || !fixed_layout()) {
    return abandon_setup("GGTT finalization or inherited layout uncertain");
  }
  if (!setup_in_time("flip-event admission")) {
    return abandon_setup("setup deadline before flip-event admission");
  }
  intel_display_flip_event_control(false);
  metrics = boot_options_get()->display_flip_metrics;
  state = INTEL_FLIP_READY;
  kinfo("intel-flip: prepared pipe A plane1 GOP=%x backs=%x/%x bytes=%zu; fixed mappings, DC0 held\n",
      gpu_surfaces[0], gpu_surfaces[1], gpu_surfaces[2], fb->size);
  return true;
}

void intel_flip_cancel_prepare(void)
{
  if (state == INTEL_FLIP_READY && !submitted) {
    state = INTEL_FLIP_OFF;
    kinfo("intel-flip: no staging; inactive backing and power owner retained until reboot\n");
  }
}

enum intel_flip_state intel_flip_state(void)
{
  return state;
}

const struct framebuffer *intel_flip_surface(unsigned index)
{
  return index < 3 && surfaces[index].address ? &surfaces[index] : NULL;
}

const struct framebuffer *intel_flip_back(void)
{
  return state == INTEL_FLIP_READY ? &surfaces[back] : NULL;
}

static enum intel_flip_state arm_pending(void)
{
  if (!fixed_layout() ||
      intel_display_read(PLANE1 + PLANE_SURFACE) != gpu_surfaces[front] ||
      intel_display_read(PLANE1 + PLANE_LIVE) != gpu_surfaces[front]) {
    return fail("ownership changed before surface arm");
  }
  if (display_is_panicking()) {
    return state;
  }
  uint64_t flags = cpu_save_interrupts();
  unsigned line = ((intel_display_read(PIPE_SCANLINE(0)) & SCANLINE_MASK) + 1) % vtotal;
  if (line >= vblank_start - keepout_lines && line < vblank_start) {
    cpu_restore_interrupts(flags);
    ++deferred;
    return state;
  }
  if (display_is_panicking()) {
    cpu_restore_interrupts(flags);
    return state;
  }
  intel_display_flip_event_control(false);
  intel_display_clear_flip_event();
  intel_display_clear_flip_event();
  if (intel_display_read(PIPE_A_IIR) & PLANE1_DONE) {
    cpu_restore_interrupts(flags);
    return fail("flip event baseline not empty");
  }
  intel_display_flip_event_control(true);
  submit_frame = intel_display_read(PIPE_A_FRAME_COUNT);
  submit_flip = intel_display_read(PIPE_A_FLIP_COUNT);
  submit_done_time = intel_display_read(PIPE_A_FLIP_DONE_TIME);
  /* Controls/baselines above can cross the keepout boundary; check last. */
  line = ((intel_display_read(PIPE_SCANLINE(0)) & SCANLINE_MASK) + 1) % vtotal;
  if (line >= vblank_start - keepout_lines && line < vblank_start) {
    cpu_restore_interrupts(flags);
    ++deferred;
    return state;
  }
  cpu_store_fence();
  submit_clock = intel_display_read(DISPLAY_TIMESTAMP);
  intel_display_write_surface(gpu_surfaces[pending]);
  state = INTEL_FLIP_PENDING;
  ++submitted;
  cpu_restore_interrupts(flags);
  return state;
}

enum intel_flip_state intel_flip_submit(void)
{
  KASSERT(state == INTEL_FLIP_READY);
  pending = back;
  deadline = arch_monotonic_ns() + FLIP_TIMEOUT_NS;
  state = INTEL_FLIP_DEFERRED;
  return arm_pending();
}

enum intel_flip_state intel_flip_retry(void)
{
  KASSERT(state == INTEL_FLIP_DEFERRED);
  return arm_pending();
}

enum intel_flip_state intel_flip_poll(void)
{
  if (state == INTEL_FLIP_OFF || state == INTEL_FLIP_FAILED || display_is_panicking()) {
    return state;
  }
  if (!fixed_layout()) {
    return fail("inherited layout or power changed");
  }
  if (state == INTEL_FLIP_READY) {
    if (intel_display_read(PLANE1 + PLANE_SURFACE) != gpu_surfaces[front] ||
        intel_display_read(PLANE1 + PLANE_LIVE) != gpu_surfaces[front]) {
      return fail("idle front changed");
    }
    return state;
  }
  if (state == INTEL_FLIP_DEFERRED) {
    if (arch_monotonic_ns() >= deadline) {
      ++timeouts;
      return fail("queued arm timed out");
    }
    return state;
  }
  uint32_t event = intel_display_read(PIPE_A_IIR);
  uint32_t live = intel_display_read(PLANE1 + PLANE_LIVE);
  uint32_t done_time = intel_display_read(PIPE_A_FLIP_DONE_TIME);
  uint32_t frame = intel_display_read(PIPE_A_FRAME_COUNT);
  uint32_t flip = intel_display_read(PIPE_A_FLIP_COUNT);
  if ((event & PLANE1_DONE) && live == gpu_surfaces[pending] &&
      done_time != submit_done_time && frame != submit_frame && flip - submit_flip == 1 &&
      intel_display_read(PLANE1 + PLANE_SURFACE) == gpu_surfaces[pending]) {
    if ((uint32_t)(done_time - submit_clock) > FLIP_TIMEOUT_NS / 1000) {
      ++timeouts;
      return fail("hardware latch exceeded transaction deadline");
    }
    front = pending;
    /* The first owned-front completion retires GOP, allowing one ordinary
     * full-frame copy and confirmed return before continuous alternation. */
    back = confirmed == 0 ? 0 : front == 1 ? 2 : 1;
    state = INTEL_FLIP_READY;
    ++confirmed;
    if (confirmed == 2) {
      kinfo("intel-flip: initial owned flip and GOP return confirmed; continuous reuse admitted\n");
    }
    ++sequence;
    if (metrics && (confirmed <= 2 || confirmed % 120 == 0)) {
      kinfo("intel-flip-metrics: submitted=%lu confirmed=%lu deferred=%lu timeouts=%lu front=%x seq=%u; event/latch/live/frame correlated\n",
          submitted, confirmed, deferred, timeouts, gpu_surfaces[front], sequence);
    }
  } else if (arch_monotonic_ns() >= deadline) {
    ++timeouts;
    return fail("flip completion timed out");
  }
  return state;
}

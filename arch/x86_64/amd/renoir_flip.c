/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

/*
 * Copyright 2012-2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
/*
* Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <arch/amd/renoir_flip.h>
#include <arch/amd/renoir_firmware.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/display.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/scanout.h>
#include <kernel/panic.h>
#include "renoir_state.h"
#include "renoir_cursor_inventory.h"

#define RENOIR_VENDOR 0x1002
#define RENOIR_DEVICE 0x1636
#define RENOIR_UMA_BYTES (512 * 1024 * 1024u)
#define RENOIR_NATIVE_WIDTH 1920u
#define RENOIR_NATIVE_HEIGHT 1080u
#define RENOIR_NATIVE_PITCH 7680u
#define RENOIR_NATIVE_PITCH_RAW 0x780u
#define RENOIR_NATIVE_CROSSBAR 0xe40000u
#define RENOIR_FLIP_TIMEOUT_NS UINT64_C(50000000)
#define RENOIR_SPARE_OFFSET 0x900000u
#define RENOIR_POLL_LIMIT 50u
#define RENOIR_METRIC_FRAMES 120u

struct scaler_state {
  uint32_t h_ratio, v_ratio, h_ratio_c, v_ratio_c;
  uint32_t h_init, v_init, h_init_c, v_init_c, v_init_bottom, v_init_bottom_c;
  uint32_t taps, control, two_tap_control, replicate_control;
};

struct layout_state {
  struct inventory_state registers;
  uint32_t scaler_mode[RENOIR_PIPES], scaler_autocal[RENOIR_PIPES];
  struct scaler_state scaler[RENOIR_PIPES];
  uint32_t recout_start[RENOIR_PIPES], recout_size[RENOIR_PIPES], mpc_size[RENOIR_PIPES];
  uint32_t stereo[RENOIR_PIPES], lock[RENOIR_PIPES], gsl[RENOIR_PIPES];
  uint32_t mpcc_stereo[RENOIR_MPCCS], flip_interrupt[RENOIR_PIPES];
};
static struct {
  enum renoir_flip_state state;
  struct pci_claim claim;
  struct framebuffer surfaces[2];
  uint64_t addresses[2];
  struct layout_state inherited;
  struct framebuffer_bar aperture;
  phys_addr_t bar;
  unsigned power, hubp, front, requested, request_polls;
  uint64_t submitted, deadline;
  size_t occupied_end;
  bool prepared, submitted_once, metrics;
  uint64_t submissions, confirmations, polls, timeouts, wait_total, wait_max;
  uint64_t validation_count, validation_total, validation_max;
  uint64_t submit_validation_count, submit_validation_total, submit_validation_max;
  uint64_t light_count, light_total, light_max;
} flip;

static void snapshot_layout(struct layout_state *s)
{
  *s = (struct layout_state){0};
  renoir_snapshot(&s->registers);
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    unsigned d = i * DSCL_STRIDE, t = i * OTG_STRIDE;
    s->flip_interrupt[i] = renoir_read_register(HUBP_FLIP_INTERRUPT + i * HUBP_STRIDE);
    s->scaler[i] = (struct scaler_state){
      .h_ratio = renoir_read_register(DSCL_H_RATIO + d),
      .v_ratio = renoir_read_register(DSCL_V_RATIO + d),
      .h_ratio_c = renoir_read_register(DSCL_H_RATIO_C + d),
      .v_ratio_c = renoir_read_register(DSCL_V_RATIO_C + d),
      .h_init = renoir_read_register(DSCL_H_INIT + d),
      .v_init = renoir_read_register(DSCL_V_INIT + d),
      .h_init_c = renoir_read_register(DSCL_H_INIT_C + d),
      .v_init_c = renoir_read_register(DSCL_V_INIT_C + d),
      .v_init_bottom = renoir_read_register(DSCL_V_INIT_BOTTOM + d),
      .v_init_bottom_c = renoir_read_register(DSCL_V_INIT_BOTTOM_C + d),
      .taps = renoir_read_register(DSCL_TAPS + d),
      .control = renoir_read_register(DSCL_CONTROL + d),
      .two_tap_control = renoir_read_register(DSCL_TWO_TAP_CONTROL + d),
      .replicate_control = renoir_read_register(DSCL_REPLICATE_CONTROL + d),
    };
    s->scaler_mode[i] = renoir_read_register(DSCL_MODE + d);
    s->scaler_autocal[i] = renoir_read_register(DSCL_AUTOCAL + d);
    s->recout_start[i] = renoir_read_register(DSCL_RECOUT_START + d);
    s->recout_size[i] = renoir_read_register(DSCL_RECOUT_SIZE + d);
    s->mpc_size[i] = renoir_read_register(DSCL_MPC_SIZE + d);
    s->stereo[i] = renoir_read_register(OTG_STEREO + t);
    s->lock[i] = renoir_read_register(OTG_MASTER_LOCK + t);
    s->gsl[i] = renoir_read_register(OTG_GSL + t);
  }
  for (unsigned i = 0; i < RENOIR_MPCCS; ++i) {
    s->mpcc_stereo[i] = renoir_read_register(MPCC_STEREO + i * MPCC_STRIDE);
  }
}

static bool dimensions(uint32_t value, size_t width, size_t height)
{
  return (value & VIEWPORT_MASK) == width && ((value >> 16) & VIEWPORT_MASK) == height;
}

static bool active_extent(uint32_t total_register, uint32_t blank, size_t active)
{
  unsigned total = (total_register & OTG_TIMING_MASK) + 1;
  unsigned start = blank & OTG_TIMING_MASK, end = (blank >> 16) & OTG_TIMING_MASK;
  return end < start && start < total && start - end == active;
}

static bool qualified_scaler(const struct layout_state *s, unsigned hubp)
{
  if (!(s->scaler_mode[hubp] & SCALER_MODE_MASK)) {
    return true;
  }
  const struct scaler_state *scale = &s->scaler[hubp];
  /* The correctly displayed GOP proves this inherited sampling state. Address
   * flips preserve it; same_layout compares every scaler field at runtime. */
  return s->scaler_mode[hubp] == SCALER_RGB_ENABLE &&
    s->scaler_autocal[hubp] == SCALER_NATIVE_AUTOCAL &&
    (scale->h_ratio & SCALER_RATIO_MASK) == SCALER_RATIO_UNITY &&
    (scale->v_ratio & SCALER_RATIO_MASK) == SCALER_RATIO_UNITY;
}

static int qualified_route(const struct layout_state *s)
{
  const struct inventory_state *r = &s->registers;
  unsigned otg = 0, enabled = 0;
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    if (r->otg[i].control & OTG_MASTER_ENABLE) {
      otg = i;
      ++enabled;
    }
  }
  if (enabled != 1 || !(r->otg[otg].control & OTG_CURRENT_MASTER_ENABLE) ||
      !active_extent(r->otg[otg].h_total, r->otg[otg].h_blank, RENOIR_NATIVE_WIDTH) ||
      !active_extent(r->otg[otg].v_total, r->otg[otg].v_blank, RENOIR_NATIVE_HEIGHT) ||
      (r->otg[otg].interlace & OTG_INTERLACE_ENABLE) ||
      (s->stereo[otg] & OTG_STEREO_ENABLE) || (s->lock[otg] & OTG_LOCK_MASK) ||
      (s->gsl[otg] & OTG_GSL_MASK)) {
    return -1;
  }
  unsigned source = r->otg[otg].source;
  unsigned opp = (source >> ODM_SEG0_SHIFT) & SELECTOR_MASK;
  unsigned other = (source >> ODM_SEG1_SHIFT) & SELECTOR_MASK;
  if (opp >= RENOIR_PIPES || ((source & ODM_INPUT_COUNT) && other != SELECTOR_NONE)) {
    return -1;
  }
  unsigned mpcc = r->mux[opp] & SELECTOR_MASK;
  if (mpcc >= RENOIR_MPCCS) {
    return -1;
  }
  const struct mpcc_state *m = &r->mpcc[mpcc];
  unsigned hubp = m->top & SELECTOR_MASK, bottom = m->bottom & SELECTOR_MASK;
  if (hubp >= RENOIR_PIPES || (m->opp & SELECTOR_MASK) != opp ||
      (bottom != SELECTOR_NONE && bottom != mpcc) ||
      (m->control & MPCC_MODE_MASK) != MPCC_TOP_PASSTHROUGH ||
      (m->control & MPCC_ALPHA_MODE_MASK) != MPCC_OPAQUE_GLOBAL_ALPHA ||
      (m->control & MPCC_GLOBAL_ALPHA_GAIN_MASK) != MPCC_GLOBAL_ALPHA_GAIN_MASK || (s->mpcc_stereo[mpcc] & MPCC_STEREO_ENABLE)) {
    return -1;
  }
  const struct hubp_state *h = &r->hubp[hubp];
  if ((h->control & (HUBP_BLANK | HUBP_DISABLE)) || !(h->clock & HUBP_CLOCK_ENABLE) ||
      ((h->control >> HUBP_VTG_SHIFT) & SELECTOR_MASK) != otg ||
      (h->config & (FORMAT_MASK | ROTATION_MASK | MIRROR_MASK)) != FORMAT_ARGB8888 ||
      (h->tiling & SWIZZLE_MASK) || h->viewport_start ||
      !dimensions(h->viewport_size, RENOIR_NATIVE_WIDTH, RENOIR_NATIVE_HEIGHT) ||
      h->pitch != RENOIR_NATIVE_PITCH_RAW || (h->vmid & SELECTOR_MASK) ||
      (h->surface & (SURFACE_TMZ | SURFACE_DCC)) || h->tlb ||
      (h->crossbar & CROSSBAR_MASK) != RENOIR_NATIVE_CROSSBAR ||
      (h->flip & (FLIP_LOCK | FLIP_MASTER_LOCK | FLIP_STEREO)) ||
      (h->flip2 & (FLIP_GSL | FLIP_TRIPLE)) ||
      (s->flip_interrupt[hubp] & HUBP_FLIP_INTERRUPT_ENABLE) ||
      !qualified_scaler(s, hubp) || (s->scaler_autocal[hubp] & SCALER_AUTOCAL_MASK) ||
      (s->recout_start[hubp] & RECOUT_ORIGIN_MASK) ||
      !dimensions(s->recout_size[hubp], RENOIR_NATIVE_WIDTH, RENOIR_NATIVE_HEIGHT) ||
      !dimensions(s->mpc_size[hubp], RENOIR_NATIVE_WIDTH, RENOIR_NATIVE_HEIGHT) ||
      r->context_control || r->context_base_high || r->context_base_low ||
      r->context_start_high || r->context_start_low || r->context_end_high || r->context_end_low) {
    return -1;
  }
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    if (i != hubp && (r->hubp[i].clock & HUBP_CLOCK_ENABLE) &&
        !(r->hubp[i].control & (HUBP_BLANK | HUBP_DISABLE))) {
      return -1;
    }
  }
  /* Enabled DMCUB or a new live window changes the reservation evidence.
   * Rather than guessing its extent, refuse this first qualified slice. */
  if (r->dmcub_control & DMCUB_ENABLE) {
    return -1;
  }
  for (unsigned i = 0; i < DMCUB_CACHE_WINDOWS; ++i) {
    if (r->cache[i].top & DMCUB_WINDOW_ENABLE) {
      return -1;
    }
  }
  for (unsigned i = 0; i < DMCUB_UNCACHED_WINDOWS; ++i) {
    if (r->uncached[i].top & DMCUB_WINDOW_ENABLE) {
      return -1;
    }
  }
  return hubp;
}

static void static_layout(struct layout_state *s)
{
  struct hubp_state *h = &s->registers.hubp[flip.hubp];
  h->primary = h->earliest = h->inuse = 0;
  h->flip &= ~(FLIP_PENDING | FLIP_IMMEDIATE);
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    s->registers.hubp[i].control &= ~(HUBP_REQUEST_STATUS | HUBP_IN_BLANK);
    s->registers.hubp[i].clock &= ~HUBP_CLOCK_STATUS;
    if (!(s->scaler_mode[i] & SCALER_MODE_MASK)) {
      s->scaler_mode[i] &= ~SCALER_CURRENT_BANK;
      s->scaler[i] = (struct scaler_state){0};
    }
    s->flip_interrupt[i] &= ~HUBP_FLIP_INTERRUPT_STATUS;
  }
  for (unsigned i = 0; i < RENOIR_MPCCS; ++i) {
    s->registers.mpcc[i].status = 0;
  }
}

static bool same_layout(struct layout_state a, struct layout_state b)
{
  static_layout(&a);
  static_layout(&b);
  return !memcmp(&a, &b, sizeof(a));
}

/* Failure-only diagnostics name fields using the same normalization as the
 * immutable comparison, while retaining raw register values in the output. */
static void report_layout_difference(const struct layout_state *current, const struct layout_state *inherited)
{
  struct layout_state a = *current, b = *inherited;
  static_layout(&a);
  static_layout(&b);
  unsigned i = 0;
#define REPORT_FIELD(field) do { \
  if (a.field != b.field) { \
    klog("renoir-flip: difference field=%s index=%u inherited=%lx current=%lx\n", \
        #field, i, (uint64_t)inherited->field, (uint64_t)current->field); \
    return; \
  } \
} while (0)
  for (i = 0; i < RENOIR_PIPES; ++i) {
    REPORT_FIELD(registers.hubp[i].config);
    REPORT_FIELD(registers.hubp[i].address_config);
    REPORT_FIELD(registers.hubp[i].tiling);
    REPORT_FIELD(registers.hubp[i].viewport_start);
    REPORT_FIELD(registers.hubp[i].viewport_size);
    REPORT_FIELD(registers.hubp[i].control);
    REPORT_FIELD(registers.hubp[i].clock);
    REPORT_FIELD(registers.hubp[i].pitch);
    REPORT_FIELD(registers.hubp[i].vmid);
    REPORT_FIELD(registers.hubp[i].surface);
    REPORT_FIELD(registers.hubp[i].flip);
    REPORT_FIELD(registers.hubp[i].flip2);
    REPORT_FIELD(registers.hubp[i].crossbar);
    REPORT_FIELD(registers.hubp[i].aperture_low);
    REPORT_FIELD(registers.hubp[i].aperture_high);
    REPORT_FIELD(registers.hubp[i].tlb);
    REPORT_FIELD(registers.hubp[i].primary);
    REPORT_FIELD(registers.hubp[i].metadata);
    REPORT_FIELD(registers.hubp[i].inuse);
    REPORT_FIELD(registers.hubp[i].earliest);
    REPORT_FIELD(registers.otg[i].control);
    REPORT_FIELD(registers.otg[i].interlace);
    REPORT_FIELD(registers.otg[i].h_total);
    REPORT_FIELD(registers.otg[i].h_blank);
    REPORT_FIELD(registers.otg[i].v_total);
    REPORT_FIELD(registers.otg[i].v_blank);
    REPORT_FIELD(registers.otg[i].source);
    REPORT_FIELD(registers.otg[i].format);
    REPORT_FIELD(scaler[i].h_ratio);
    REPORT_FIELD(scaler[i].v_ratio);
    REPORT_FIELD(scaler[i].h_ratio_c);
    REPORT_FIELD(scaler[i].v_ratio_c);
    REPORT_FIELD(scaler[i].h_init);
    REPORT_FIELD(scaler[i].v_init);
    REPORT_FIELD(scaler[i].h_init_c);
    REPORT_FIELD(scaler[i].v_init_c);
    REPORT_FIELD(scaler[i].v_init_bottom);
    REPORT_FIELD(scaler[i].v_init_bottom_c);
    REPORT_FIELD(scaler[i].taps);
    REPORT_FIELD(scaler[i].control);
    REPORT_FIELD(scaler[i].two_tap_control);
    REPORT_FIELD(scaler[i].replicate_control);
    REPORT_FIELD(registers.mux[i]);
    REPORT_FIELD(scaler_mode[i]);
    REPORT_FIELD(scaler_autocal[i]);
    REPORT_FIELD(recout_start[i]);
    REPORT_FIELD(recout_size[i]);
    REPORT_FIELD(mpc_size[i]);
    REPORT_FIELD(stereo[i]);
    REPORT_FIELD(lock[i]);
    REPORT_FIELD(gsl[i]);
    REPORT_FIELD(flip_interrupt[i]);
  }
  for (i = 0; i < RENOIR_MPCCS; ++i) {
    REPORT_FIELD(registers.mpcc[i].top);
    REPORT_FIELD(registers.mpcc[i].bottom);
    REPORT_FIELD(registers.mpcc[i].opp);
    REPORT_FIELD(registers.mpcc[i].control);
    REPORT_FIELD(registers.mpcc[i].status);
    REPORT_FIELD(mpcc_stereo[i]);
  }
  for (i = 0; i < DMCUB_CACHE_WINDOWS; ++i) {
    REPORT_FIELD(registers.cache[i].base);
    REPORT_FIELD(registers.cache[i].top);
    REPORT_FIELD(registers.cache[i].low);
    REPORT_FIELD(registers.cache[i].high);
  }
  for (i = 0; i < DMCUB_UNCACHED_WINDOWS; ++i) {
    REPORT_FIELD(registers.uncached[i].base);
    REPORT_FIELD(registers.uncached[i].top);
    REPORT_FIELD(registers.uncached[i].low);
    REPORT_FIELD(registers.uncached[i].high);
  }
  i = 0;
  REPORT_FIELD(registers.fb_base);
  REPORT_FIELD(registers.fb_top);
  REPORT_FIELD(registers.fb_offset);
  REPORT_FIELD(registers.gc_offset);
  REPORT_FIELD(registers.mc_base);
  REPORT_FIELD(registers.mc_top);
  REPORT_FIELD(registers.memsize);
  REPORT_FIELD(registers.context_control);
  REPORT_FIELD(registers.context_base_high);
  REPORT_FIELD(registers.context_base_low);
  REPORT_FIELD(registers.context_start_high);
  REPORT_FIELD(registers.context_start_low);
  REPORT_FIELD(registers.context_end_high);
  REPORT_FIELD(registers.context_end_low);
  REPORT_FIELD(registers.dmcub_control);
  REPORT_FIELD(registers.dmcub_security);
#undef REPORT_FIELD
  const unsigned char *x = (const unsigned char *)&a, *y = (const unsigned char *)&b;
  for (size_t offset = 0; offset < sizeof(a); ++offset) {
    if (x[offset] != y[offset]) {
      klog("renoir-flip: difference field=padding offset=%zu inherited=%x current=%x\n",
          offset, (unsigned)y[offset], (unsigned)x[offset]);
      return;
    }
  }
}

static bool owns(uint64_t address)
{
  return address == flip.addresses[0] || address == flip.addresses[1];
}

static bool device_unchanged(void)
{
  unsigned power;
  phys_addr_t bar;
  struct framebuffer_bar aperture;
  return flip.claim.device->owner == &flip.claim &&
    renoir_device_ready(flip.claim.device, &power, &bar) &&
    power == flip.power && bar == flip.bar &&
    renoir_read_framebuffer_bar(flip.claim.device->address, &aperture) &&
    aperture.low == flip.aperture.low && aperture.high == flip.aperture.high;
}

/* Two matching snapshots bound address/control tearing. A transition can make
 * the tuple unstable without changing the immutable pipe; retry on next poll. */
static bool current_state(struct hubp_state *out, bool *unstable)
{
  *unstable = false;
  if (!device_unchanged()) {
    if (flip.metrics) {
      klog("renoir-flip: check=device_unchanged failed\n");
    }
    return false;
  }
  struct layout_state samples[2];
  snapshot_layout(&samples[0]);
  snapshot_layout(&samples[1]);
  for (unsigned i = 0; i < 2; ++i) {
    int route = qualified_route(&samples[i]);
    if (route != (int)flip.hubp) {
      if (flip.metrics) {
        klog("renoir-flip: check=route mismatch snapshot=%u inherited=%u current=%d\n",
            i + 1, flip.hubp, route);
        report_layout_difference(&samples[i], &flip.inherited);
      }
      return false;
    }
  }
  for (unsigned i = 0; i < 2; ++i) {
    if (!same_layout(samples[i], flip.inherited)) {
      if (flip.metrics) {
        klog("renoir-flip: check=same_layout failed snapshot=%u\n", i + 1);
        report_layout_difference(&samples[i], &flip.inherited);
      }
      return false;
    }
    const struct hubp_state *h = &samples[i].registers.hubp[flip.hubp];
    if (!owns(h->primary) || !owns(h->earliest)) {
      if (flip.metrics) {
        klog("renoir-flip: check=owns failed snapshot=%u primary=%lx earliest=%lx owned=%lx/%lx\n",
            i + 1, h->primary, h->earliest, flip.addresses[0], flip.addresses[1]);
      }
      return false;
    }
  }
  const struct hubp_state *x = &samples[0].registers.hubp[flip.hubp];
  const struct hubp_state *y = &samples[1].registers.hubp[flip.hubp];
  *out = *y;
  *unstable = x->primary != y->primary || x->earliest != y->earliest ||
    ((x->flip ^ y->flip) & FLIP_PENDING);
  return true;
}

static void fail(const char *reason, const char *operation)
{
  if (flip.metrics) {
    const char *phase = flip.state == RENOIR_FLIP_READY ? "READY" :
      flip.state == RENOIR_FLIP_PENDING ? "PENDING" : "FALLBACK";
    klog("renoir-flip: failure operation=%s phase=%s submitted=%lu confirmed=%lu polls=%lu front=%u requested=%u\n",
        operation, phase, flip.submissions, flip.confirmations, flip.polls,
        flip.front, flip.requested);
  }
  flip.state = RENOIR_FLIP_FAILED;
  klog("renoir-flip: unavailable: %s; surfaces pinned, GPU writes stopped\n", reason);
}

static void print_scaler(const struct layout_state *state, unsigned pipe)
{
  const struct scaler_state *s = &state->scaler[pipe];
  klog("renoir-flip: scaler pipe=%u ratios H=%x V=%x HC=%x VC=%x; unity=%x mask=%x\n",
      pipe, s->h_ratio, s->v_ratio, s->h_ratio_c, s->v_ratio_c,
      SCALER_RATIO_UNITY, SCALER_RATIO_MASK);
  klog("renoir-flip: scaler pipe=%u taps=%x control=%x two-tap=%x replicate=%x init H=%x V=%x HC=%x VC=%x VB=%x VBC=%x\n",
      pipe, s->taps, s->control, s->two_tap_control, s->replicate_control,
      s->h_init, s->v_init, s->h_init_c, s->v_init_c, s->v_init_bottom, s->v_init_bottom_c);
}

bool renoir_flip_prepare(const struct boot_info *boot, const struct framebuffer *gop, bool metrics)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count() && !flip.prepared);
  struct pci_device *device;
  if (pci_select_device(RENOIR_VENDOR, RENOIR_DEVICE, &device) != PCI_SELECTION_UNIQUE || device->owner) {
    klog("renoir-flip: refused: unique unclaimed Renoir absent; keeping GOP copy\n");
    return false;
  }
  unsigned displays = 0;
  for (const struct pci_device *d = pci_device_at(0); d; d = d->next) {
    displays += d->base_class == 3;
  }
  if (displays != 1 || !boot->framebuffer.size ||
      gop->width != RENOIR_NATIVE_WIDTH || gop->height != RENOIR_NATIVE_HEIGHT ||
      gop->pitch != RENOIR_NATIVE_PITCH || gop->size != gop->pitch * gop->height ||
      gop->red_shift != 16 || gop->green_shift != 8 || gop->blue_shift != 0 ||
      !renoir_device_ready(device, &flip.power, &flip.bar) ||
      !renoir_read_framebuffer_bar(device->address, &flip.aperture) ||
      !renoir_map_registers(boot, flip.bar)) {
    klog("renoir-flip: refused: device/GOP/BAR/mapping prerequisite; keeping GOP copy\n");
    return false;
  }
  snapshot_layout(&flip.inherited);
  struct layout_state second;
  snapshot_layout(&second);
  int hubp = qualified_route(&second);
  if (hubp < 0) {
    goto refuse;
  }
  flip.hubp = hubp;
  if (!same_layout(second, flip.inherited)) {
    goto refuse;
  }
  const struct inventory_state *r = &second.registers;
  const struct hubp_state *h = &r->hubp[hubp];
  uint64_t gpu_base = (uint64_t)(r->fb_base & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  uint64_t gpu_end = ((uint64_t)(r->fb_top & FB_ADDRESS_MASK) + 1) << FB_ADDRESS_SHIFT;
  uint64_t cpu_base = (uint64_t)(r->gc_offset & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  uint64_t mc_base = (uint64_t)(r->mc_base & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  uint64_t mc_end = ((uint64_t)(r->mc_top & FB_ADDRESS_MASK) + 1) << FB_ADDRESS_SHIFT;
  if (gpu_end < gpu_base || gpu_end - gpu_base != RENOIR_UMA_BYTES ||
      mc_base != gpu_base || mc_end != gpu_end ||
      r->memsize != RENOIR_UMA_BYTES / (1024 * 1024u) ||
      (uint64_t)(r->fb_offset & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT != cpu_base ||
      cpu_base > UINT64_MAX - RENOIR_UMA_BYTES || gpu_base > UINT64_MAX - RENOIR_UMA_BYTES ||
      (h->aperture_low & SYSTEM_APERTURE_MASK) != gpu_base >> SYSTEM_APERTURE_SHIFT ||
      (h->aperture_high & SYSTEM_APERTURE_MASK) != gpu_end >> SYSTEM_APERTURE_SHIFT ||
      h->primary != gpu_base || h->earliest != gpu_base || (h->flip & FLIP_PENDING) ||
      flip.inherited.registers.hubp[hubp].primary != h->primary ||
      flip.inherited.registers.hubp[hubp].earliest != h->earliest ||
      (flip.inherited.registers.hubp[hubp].flip & FLIP_PENDING) ||
      flip.aperture.base != boot->framebuffer.physical ||
      !renoir_firmware_qualifies(device->address, gpu_base)) {
    goto refuse;
  }
  /* Recheck PCI/BAR before reserving software ownership, without config writes. */
  struct framebuffer_bar aperture;
  unsigned power;
  phys_addr_t bar;
  if (!renoir_device_ready(device, &power, &bar) || power != flip.power || bar != flip.bar ||
      !renoir_read_framebuffer_bar(device->address, &aperture) ||
      aperture.low != flip.aperture.low || aperture.high != flip.aperture.high ||
      !pci_reserve_device(device, &flip.claim)) {
    goto refuse;
  }
  struct scanout_storage spare;
  if (!scanout_prepare(boot, cpu_base, RENOIR_UMA_BYTES, gop->size, gop->size, &spare)) {
    pci_cancel_reservation(&flip.claim);
    goto refuse;
  }
  if (spare.offset != RENOIR_SPARE_OFFSET || !renoir_enable_flip_page(hubp)) {
    scanout_discard(&spare);
    pci_cancel_reservation(&flip.claim);
    goto refuse;
  }
  flip.surfaces[0] = *gop;
  flip.surfaces[1] = *gop;
  flip.surfaces[1].address = spare.address;
  flip.addresses[0] = gpu_base;
  flip.addresses[1] = gpu_base + spare.offset;
  flip.occupied_end = spare.offset + spare.bytes;
  flip.prepared = true;
  flip.metrics = metrics;
  flip.state = RENOIR_FLIP_READY;
  klog("renoir-flip: prepared HUBP%u; two surfaces; inherited pitch raw=%x effective=%zu; spare offset=%lx\n",
      flip.hubp, h->pitch, gop->pitch, spare.offset);
  if (metrics) {
    print_scaler(&second, hubp);
    klog("renoir-flip: GPU=%lx/%lx CPU spare=%lx WC; BAR0=%lx BAR5=%lx; payload=%zu backing=%zu\n",
        flip.addresses[0], flip.addresses[1], spare.physical, flip.aperture.base, flip.bar,
        gop->size, spare.bytes);
    klog("renoir-flip: inherited control=%x flip2=%x scaler=%x/%x recout=%x mpc=%x; no mode/clock/power/VM writes\n",
        h->flip, h->flip2, second.scaler_mode[hubp], second.scaler_autocal[hubp],
        second.recout_size[hubp], second.mpc_size[hubp]);
  }
  return true;
refuse:
  if (metrics) {
    const struct inventory_state *state = &second.registers;
    klog("renoir-flip: refusal memory FB=%x..%x offset=%x GC=%x MC=%x..%x MiB=%u\n",
        state->fb_base, state->fb_top, state->fb_offset, state->gc_offset,
        state->mc_base, state->mc_top, state->memsize);
    for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
      const struct hubp_state *plane = &state->hubp[i];
      print_scaler(&second, i);
      klog("renoir-flip: refusal pipe=%u OTG=%x HUBP=%x clock=%x primary=%lx earliest=%lx flip=%x/%x\n",
          i, state->otg[i].control, plane->control, plane->clock,
          plane->primary, plane->earliest, plane->flip, plane->flip2);
      klog("renoir-flip: refusal pipe=%u flip-interrupt=%x\n", i, second.flip_interrupt[i]);
      klog("renoir-flip: refusal pipe=%u scaler=%x autocal=%x start=%x recout=%x mpc=%x pitch=%x format=%x\n",
          i, second.scaler_mode[i], second.scaler_autocal[i], second.recout_start[i],
          second.recout_size[i], second.mpc_size[i], plane->pitch, plane->config);
    }
  }
  renoir_unmap_registers();
  klog("renoir-flip: refused: route/layout/translation/reservation proof; keeping GOP copy\n");
  return false;
}

void renoir_flip_cursor_inventory(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count());
  if (!flip.prepared || flip.state != RENOIR_FLIP_READY) {
    klog("renoir-cursor-inventory: unavailable: qualified flip owner absent; no register reads or cursor allocation\n");
    return;
  }
  struct hubp_state hubp;
  bool unstable;
  if (!current_state(&hubp, &unstable) || unstable) {
    klog("renoir-cursor-inventory: unavailable: device/route/layout changed; no cursor allocation\n");
    return;
  }
  renoir_cursor_inventory(boot, flip.claim.device->address, &flip.inherited.registers,
      flip.hubp, flip.occupied_end);
}

void renoir_flip_cancel_prepare(void)
{
  KASSERT(!flip.submitted_once);
  flip.state = RENOIR_FLIP_OFF;
}

enum renoir_flip_state renoir_flip_state(void)
{
  return flip.state;
}

const struct framebuffer *renoir_flip_surface(unsigned index)
{
  return flip.prepared && index < 2 ? &flip.surfaces[index] : NULL;
}

const struct framebuffer *renoir_flip_back(void)
{
  return flip.state == RENOIR_FLIP_READY ? &flip.surfaces[1 - flip.front] : NULL;
}

enum renoir_flip_state renoir_flip_submit(void)
{
  KASSERT(flip.state == RENOIR_FLIP_READY);
  struct hubp_state h;
  bool unstable;
  uint64_t started = flip.metrics ? arch_monotonic_ns() : 0;
  bool valid = current_state(&h, &unstable);
  if (flip.metrics) {
    uint64_t elapsed = arch_monotonic_ns() - started;
    ++flip.submit_validation_count;
    flip.submit_validation_total += elapsed;
    flip.submit_validation_max = MAX(flip.submit_validation_max, elapsed);
  }
  if (!valid || unstable || (h.flip & FLIP_PENDING) ||
      h.primary != flip.addresses[flip.front] || h.earliest != flip.addresses[flip.front]) {
    fail("submission ownership/layout changed", "submit");
    return flip.state;
  }
  unsigned requested = 1 - flip.front;
  uint64_t address = flip.addresses[requested];
  uint64_t flags = cpu_save_interrupts();
  if (display_is_panicking()) {
    cpu_restore_interrupts(flags);
    return flip.state;
  }
  /* Adapted DCN2.1 mono ordering: preserve every unrelated firmware field.
   * The primary low write triggers the flip with update lock unused. */
  renoir_write_flip_register(flip.hubp, HUBP_FLIP_CONTROL, h.flip & ~FLIP_IMMEDIATE);
  renoir_write_flip_register(flip.hubp, HUBP_PRIMARY_HIGH, address >> 32);
  if (display_is_panicking()) {
    cpu_restore_interrupts(flags);
    return flip.state;
  }
  renoir_write_flip_register(flip.hubp, HUBP_PRIMARY_LOW, address);
  (void)renoir_read_register(HUBP_PRIMARY_LOW + flip.hubp * HUBP_STRIDE);
  flip.submitted = arch_monotonic_ns();
  flip.deadline = flip.submitted + RENOIR_FLIP_TIMEOUT_NS;
  flip.requested = requested;
  flip.submitted_once = true;
  flip.state = RENOIR_FLIP_PENDING;
  flip.request_polls = 0;
  ++flip.submissions;
  cpu_restore_interrupts(flags);
  return flip.state;
}

struct flip_observation {
  uint32_t control;
  uint64_t earliest;
};

static struct flip_observation observe_flip(void)
{
  unsigned offset = flip.hubp * HUBP_STRIDE;
  uint32_t control = renoir_read_register(HUBP_FLIP_CONTROL + offset);
  uint32_t low = renoir_read_register(HUBP_EARLIEST_LOW + offset);
  uint32_t high = renoir_read_register(HUBP_EARLIEST_HIGH + offset);
  return (struct flip_observation){
    .control = control,
    .earliest = low | ((uint64_t)(high & ADDRESS_HIGH_MASK) << 32),
  };
}

enum renoir_flip_state renoir_flip_poll(void)
{
  if (flip.state != RENOIR_FLIP_PENDING && flip.state != RENOIR_FLIP_FALLBACK &&
      flip.state != RENOIR_FLIP_READY) {
    return flip.state;
  }
  bool unexpected = false;
  if (flip.state == RENOIR_FLIP_PENDING) {
    ++flip.polls;
    ++flip.request_polls;
    uint64_t started = flip.metrics ? arch_monotonic_ns() : 0;
    struct flip_observation a = observe_flip();
    struct flip_observation b = observe_flip();
    bool stable = a.earliest == b.earliest && !((a.control ^ b.control) & FLIP_PENDING);
    uint32_t inherited = flip.inherited.registers.hubp[flip.hubp].flip;
    bool expected = owns(a.earliest) && owns(b.earliest) &&
      !((a.control ^ inherited) & ~(FLIP_PENDING | FLIP_IMMEDIATE)) &&
      !((b.control ^ inherited) & ~(FLIP_PENDING | FLIP_IMMEDIATE));
    unexpected = !expected;
    uint64_t now = arch_monotonic_ns();
    if (flip.metrics) {
      uint64_t elapsed = now - started;
      ++flip.light_count;
      flip.light_total += elapsed;
      flip.light_max = MAX(flip.light_max, elapsed);
      if (unexpected) {
        klog("renoir-flip: check=light_observation unexpected control=%x/%x inherited=%x earliest=%lx/%lx owned=%lx/%lx\n",
            a.control, b.control, inherited, a.earliest, b.earliest,
            flip.addresses[0], flip.addresses[1]);
      }
    }
    /* A light observation can only keep a write-free request pending. Every
     * completion, timeout or unexpected tuple still needs the full check. */
    if (expected && now < flip.deadline && flip.request_polls < RENOIR_POLL_LIMIT &&
        (!stable || (b.control & FLIP_PENDING) || b.earliest != flip.addresses[flip.requested])) {
      return flip.state;
    }
  }
  struct hubp_state h;
  bool unstable;
  uint64_t started = flip.metrics ? arch_monotonic_ns() : 0;
  bool valid = current_state(&h, &unstable);
  if (flip.metrics) {
    uint64_t elapsed = arch_monotonic_ns() - started;
    ++flip.validation_count;
    flip.validation_total += elapsed;
    flip.validation_max = MAX(flip.validation_max, elapsed);
  }
  if (!valid || unexpected) {
    fail("poll ownership/layout changed", "poll");
    return flip.state;
  }
  if (flip.state == RENOIR_FLIP_READY) {
    if (unstable || (h.flip & FLIP_PENDING) || h.primary != flip.addresses[flip.front] ||
        h.earliest != flip.addresses[flip.front]) {
      fail("free-surface ownership changed", "poll");
    }
    return flip.state;
  }
  if (flip.state == RENOIR_FLIP_FALLBACK) {
    ++flip.polls;
    return flip.state;
  }
  uint64_t now = arch_monotonic_ns();
  if (now >= flip.deadline || flip.request_polls >= RENOIR_POLL_LIMIT) {
    flip.state = RENOIR_FLIP_FALLBACK;
    ++flip.timeouts;
    klog("renoir-flip: timeout; GPU writes stopped; both surfaces pinned; dual unsynchronized copies\n");
    return flip.state;
  }
  if (!unstable && !(h.flip & FLIP_PENDING) && h.earliest == flip.addresses[flip.requested] &&
      h.primary == flip.addresses[flip.requested]) {
    flip.front = flip.requested;
    flip.state = RENOIR_FLIP_READY;
    ++flip.confirmations;
    uint64_t elapsed = now - flip.submitted;
    flip.wait_total += elapsed;
    if (elapsed > flip.wait_max) {
      flip.wait_max = elapsed;
    }
    if (flip.metrics && flip.confirmations % RENOIR_METRIC_FRAMES == 0) {
      klog("renoir-flip: metrics submitted=%lu confirmed=%lu polls=%lu timeouts=%lu wait-mean=%lu ns wait-max=%lu ns front=%lx\n",
          flip.submissions, flip.confirmations, flip.polls, flip.timeouts,
          flip.wait_total / flip.confirmations, flip.wait_max, h.earliest);
      klog("renoir-flip: metrics validation-count=%lu validation-mean=%lu ns validation-max=%lu ns validation-total=%lu ns\n",
          flip.validation_count, flip.validation_total / flip.validation_count, flip.validation_max,
          flip.validation_total);
      klog("renoir-flip: metrics submit-validation-count=%lu submit-validation-mean=%lu ns submit-validation-max=%lu ns submit-validation-total=%lu ns\n",
          flip.submit_validation_count, flip.submit_validation_total / flip.submit_validation_count,
          flip.submit_validation_max, flip.submit_validation_total);
      klog("renoir-flip: metrics light-count=%lu light-mean=%lu ns light-max=%lu ns light-total=%lu ns\n",
          flip.light_count, flip.light_count ? flip.light_total / flip.light_count : 0,
          flip.light_max, flip.light_total);
      klog("renoir-flip: metrics poll-observation-per-frame=%lu ns bsp-observation-per-frame=%lu ns\n",
          (flip.validation_total + flip.light_total) / flip.confirmations,
          (flip.validation_total + flip.light_total + flip.submit_validation_total) / flip.confirmations);
    }
  }
  return flip.state;
}

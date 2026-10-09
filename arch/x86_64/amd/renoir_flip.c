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

struct layout_state {
  struct inventory_state registers;
  uint32_t scaler_mode[RENOIR_PIPES], scaler_autocal[RENOIR_PIPES];
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
  bool prepared, submitted_once, metrics;
  uint64_t submissions, confirmations, polls, timeouts, wait_total, wait_max;
  uint64_t validation_count, validation_total, validation_max;
} flip;

static void snapshot_layout(struct layout_state *s)
{
  *s = (struct layout_state){0};
  renoir_snapshot(&s->registers);
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    unsigned d = i * DSCL_STRIDE, t = i * OTG_STRIDE;
    s->flip_interrupt[i] = renoir_read_register(HUBP_FLIP_INTERRUPT + i * HUBP_STRIDE);
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
      (s->scaler_mode[hubp] & SCALER_MODE_MASK) || (s->scaler_autocal[hubp] & SCALER_AUTOCAL_MASK) ||
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
    s->registers.hubp[i].control &= ~HUBP_REQUEST_STATUS;
    s->registers.hubp[i].clock &= ~HUBP_CLOCK_STATUS;
    s->scaler_mode[i] &= ~SCALER_CURRENT_BANK;
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
    return false;
  }
  struct layout_state a, b;
  snapshot_layout(&a);
  snapshot_layout(&b);
  if (qualified_route(&a) != (int)flip.hubp || qualified_route(&b) != (int)flip.hubp ||
      !same_layout(a, flip.inherited) || !same_layout(b, flip.inherited)) {
    return false;
  }
  const struct hubp_state *x = &a.registers.hubp[flip.hubp];
  const struct hubp_state *y = &b.registers.hubp[flip.hubp];
  if (!owns(x->primary) || !owns(y->primary) || !owns(x->earliest) || !owns(y->earliest)) {
    return false;
  }
  *out = *y;
  *unstable = x->primary != y->primary || x->earliest != y->earliest ||
    ((x->flip ^ y->flip) & FLIP_PENDING);
  return true;
}

static void fail(const char *reason)
{
  flip.state = RENOIR_FLIP_FAILED;
  klog("renoir-flip: unavailable: %s; surfaces pinned, GPU writes stopped\n", reason);
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
  flip.prepared = true;
  flip.metrics = metrics;
  flip.state = RENOIR_FLIP_READY;
  klog("renoir-flip: prepared HUBP%u; two surfaces; inherited pitch raw=%x effective=%zu; spare offset=%lx\n",
      flip.hubp, h->pitch, gop->pitch, spare.offset);
  if (metrics) {
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
  if (!current_state(&h, &unstable) || unstable || (h.flip & FLIP_PENDING) ||
      h.primary != flip.addresses[flip.front] || h.earliest != flip.addresses[flip.front]) {
    fail("submission ownership/layout changed");
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

enum renoir_flip_state renoir_flip_poll(void)
{
  if (flip.state != RENOIR_FLIP_PENDING && flip.state != RENOIR_FLIP_FALLBACK &&
      flip.state != RENOIR_FLIP_READY) {
    return flip.state;
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
  if (!valid) {
    fail("poll ownership/layout changed");
    return flip.state;
  }
  if (flip.state == RENOIR_FLIP_READY) {
    if (unstable || (h.flip & FLIP_PENDING) || h.primary != flip.addresses[flip.front] ||
        h.earliest != flip.addresses[flip.front]) {
      fail("free-surface ownership changed");
    }
    return flip.state;
  }
  ++flip.polls;
  if (flip.state == RENOIR_FLIP_FALLBACK) {
    return flip.state;
  }
  uint64_t now = arch_monotonic_ns();
  if (now >= flip.deadline || ++flip.request_polls >= RENOIR_POLL_LIMIT) {
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
      klog("renoir-flip: metrics validation-count=%lu validation-mean=%lu ns validation-max=%lu ns\n",
          flip.validation_count, flip.validation_total / flip.validation_count, flip.validation_max);
    }
  }
  return flip.state;
}

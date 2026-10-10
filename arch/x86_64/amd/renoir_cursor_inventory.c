/*
 * Copyright (C) 2019 Advanced Micro Devices, Inc.
 * Copyright 2016 Advanced Micro Devices, Inc.
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "renoir_cursor_inventory.h"
#include <arch/amd/renoir_firmware.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/scanout.h>

#define SEGMENT1(reg) ((0xc0u + (reg)) * 4)
#define SEGMENT2(reg) ((0x34c0u + (reg)) * 4)
#define CURSOR_BYTES (64 * 1024u)
#define CURSOR_ENABLE 1u
#define CURSOR_MODE_SHIFT 8
#define CURSOR_MODE_MASK 7u
#define CURSOR_TMZ 0x1000u
#define CURSOR_SNOOP 0x2000u
#define CURSOR_SYSTEM 0x4000u
#define CURSOR_PITCH_SHIFT 16
#define CURSOR_PITCH_MASK 3u
#define CURSOR_ADDRESS_HIGH_MASK 0xffffu
#define CURSOR_DIMENSION_MASK 0x1ffu
#define CURSOR_POSITION_MASK 0x3fffu
#define CURSOR_HOTSPOT_MASK 0xffu
#define DPP_CURSOR_PENDING 0x10000u
#define REFCLK_CONTROL SEGMENT1(0x49)
#define REFCLK_ALTERNATE 1u
#define GLOBAL_TIMER_CONTROL SEGMENT2(0x51f)
#define GLOBAL_TIMER_ENABLED 0x1000u
#define GLOBAL_TIMER_DIVISOR_MASK 0xfu
#define GLOBAL_TIMER_HALF 2u
#define MPC_CURSOR_STATUS SEGMENT2(0x1357)
#define MPC_ROUTE_STATUS SEGMENT2(0x1359)
#define MPC_CURSOR_ACK SEGMENT2(0x135b)
#define OTG_POSITION SEGMENT2(0x1b4a)
#define OTG_VSTARTUP SEGMENT2(0x1b87)
#define OTG_VUPDATE SEGMENT2(0x1b88)
#define OTG_GLOBAL_SYNC SEGMENT2(0x1b8a)
#define OTG_KEEPOUT SEGMENT2(0x1b8f)
#define OTG_PIPE_UPDATE SEGMENT2(0x1b9a)
#define OTG_VSTARTUP_MASK 0x3ffu
#define OTG_POSITION_FIELD_MASK 0x7fffu

static const uint32_t cursor_control[RENOIR_PIPES] = {
  SEGMENT2(0x678), SEGMENT2(0x754), SEGMENT2(0x830), SEGMENT2(0x90c),
};
static const uint32_t cursor_settings[RENOIR_PIPES] = {
  SEGMENT2(0x65e), SEGMENT2(0x73a), SEGMENT2(0x816), SEGMENT2(0x8f2),
};
static const uint32_t dpp_control[RENOIR_PIPES] = {
  SEGMENT2(0xcc5), SEGMENT2(0xe30), SEGMENT2(0xf9b), SEGMENT2(0x1106),
};
static const uint32_t dpp_cursor_control[RENOIR_PIPES] = {
  SEGMENT2(0xce0), SEGMENT2(0xe4b), SEGMENT2(0xfb6), SEGMENT2(0x1121),
};
static const uint32_t cursor_lock[RENOIR_PIPES] = {
  SEGMENT2(0x1361), SEGMENT2(0x1366), SEGMENT2(0x136b), SEGMENT2(0x1370),
};
static const uint32_t pixel_control[RENOIR_PIPES] = {
  SEGMENT1(0x80), SEGMENT1(0x84), SEGMENT1(0x88), SEGMENT1(0x8c),
};
static const uint32_t pending_mask[RENOIR_PIPES] = {0x10, 0x400, 0x10000, 0x400000};
static const uint32_t taken_mask[RENOIR_PIPES] = {0x20, 0x800, 0x20000, 0x800000};
static const uint32_t ack_mask[RENOIR_PIPES] = {4, 0x20, 0x100, 0x800};

struct cursor_state {
  uint32_t control, low, high, size, position, hotspot, stereo, destination;
  uint32_t power, power_status, settings, reference_ratio, dpp, dpp_clock, lock;
};

struct cursor_sample {
  struct cursor_state cursor[RENOIR_PIPES];
  uint32_t pixel[RENOIR_PIPES], phase[RENOIR_PIPES], modulo[RENOIR_PIPES];
  uint32_t reference, timer, pending, route, ack;
  uint32_t position, startup, update, sync, keepout, pipe;
};

static void snapshot(struct cursor_sample *sample, unsigned otg)
{
  *sample = (struct cursor_sample){0};
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    uint32_t base = cursor_control[i];
    sample->cursor[i] = (struct cursor_state){
      .control = renoir_read_register(base), .low = renoir_read_register(base + 4),
      .high = renoir_read_register(base + 8), .size = renoir_read_register(base + 12),
      .position = renoir_read_register(base + 16), .hotspot = renoir_read_register(base + 20),
      .stereo = renoir_read_register(base + 24), .destination = renoir_read_register(base + 28),
      .power = renoir_read_register(base + 32), .power_status = renoir_read_register(base + 36),
      .settings = renoir_read_register(cursor_settings[i]),
      .reference_ratio = renoir_read_register(cursor_settings[i] + 4),
      .dpp = renoir_read_register(dpp_cursor_control[i]),
      .dpp_clock = renoir_read_register(dpp_control[i]), .lock = renoir_read_register(cursor_lock[i]),
    };
    sample->pixel[i] = renoir_read_register(pixel_control[i]);
    sample->phase[i] = renoir_read_register(pixel_control[i] + 4);
    sample->modulo[i] = renoir_read_register(pixel_control[i] + 8);
  }
  sample->reference = renoir_read_register(REFCLK_CONTROL);
  sample->timer = renoir_read_register(GLOBAL_TIMER_CONTROL);
  sample->pending = renoir_read_register(MPC_CURSOR_STATUS);
  sample->route = renoir_read_register(MPC_ROUTE_STATUS);
  sample->ack = renoir_read_register(MPC_CURSOR_ACK);
  unsigned offset = otg * OTG_STRIDE;
  sample->position = renoir_read_register(OTG_POSITION + offset);
  sample->startup = renoir_read_register(OTG_VSTARTUP + offset);
  sample->update = renoir_read_register(OTG_VUPDATE + offset);
  sample->sync = renoir_read_register(OTG_GLOBAL_SYNC + offset);
  sample->keepout = renoir_read_register(OTG_KEEPOUT + offset);
  sample->pipe = renoir_read_register(OTG_PIPE_UPDATE + offset);
}

static void print_sample(const struct cursor_sample *sample, unsigned index,
                         unsigned otg, unsigned dpp, unsigned opp)
{
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    const struct cursor_state *c = &sample->cursor[i];
    uint64_t address = c->low | ((uint64_t)(c->high & CURSOR_ADDRESS_HIGH_MASK) << 32);
    klog("renoir-cursor-inventory: sample=%u instance=%u control=%x address=%lx size=%x position=%x hotspot=%x stereo=%x destination=%x\n",
        index, i, c->control, address, c->size, c->position, c->hotspot, c->stereo, c->destination);
    klog("renoir-cursor-inventory: sample=%u instance=%u enable=%u mode=%u pitch-field=%u TMZ=%u snoop=%u system=%u size=%ux%u position=%u,%u hotspot=%u,%u\n",
        index, i, !!(c->control & CURSOR_ENABLE), (c->control >> CURSOR_MODE_SHIFT) & CURSOR_MODE_MASK,
        (c->control >> CURSOR_PITCH_SHIFT) & CURSOR_PITCH_MASK, !!(c->control & CURSOR_TMZ),
        !!(c->control & CURSOR_SNOOP), !!(c->control & CURSOR_SYSTEM),
        (c->size >> 16) & CURSOR_DIMENSION_MASK, c->size & CURSOR_DIMENSION_MASK,
        (c->position >> 16) & CURSOR_POSITION_MASK, c->position & CURSOR_POSITION_MASK,
        (c->hotspot >> 16) & CURSOR_HOTSPOT_MASK, c->hotspot & CURSOR_HOTSPOT_MASK);
    klog("renoir-cursor-inventory: sample=%u instance=%u power=%x power-status=%x settings=%x ref-ratio=%x DPP-clock=%x DPP-cursor=%x OPP-lock=%x\n",
        index, i, c->power, c->power_status, c->settings, c->reference_ratio, c->dpp_clock, c->dpp, c->lock);
    klog("renoir-cursor-inventory: sample=%u pixel-instance=%u control=%x phase=%u modulo=%u; pixel/reference provenance not qualified\n",
        index, i, sample->pixel[i], sample->phase[i], sample->modulo[i]);
  }
  klog("renoir-cursor-inventory: sample=%u REFCLK=%x global-timer=%x MPC-pending-taken=%x route=%x ACK=%x DPP%u pending=%u taken=%u ack=%u DPP-control-pending=%u OPP%u lock=%u\n",
      index, sample->reference, sample->timer, sample->pending, sample->route, sample->ack,
      dpp, !!(sample->pending & pending_mask[dpp]), !!(sample->pending & taken_mask[dpp]),
      !!(sample->ack & ack_mask[dpp]), !!(sample->cursor[dpp].dpp & DPP_CURSOR_PENDING),
      opp, sample->cursor[opp].lock & 1u);
  klog("renoir-cursor-inventory: sample=%u OTG%u position=%x (%u,%u) startup=%x vupdate=%x global-sync=%x keepout=%x pipe-update=%x\n",
      index, otg, sample->position, (sample->position >> 16) & OTG_POSITION_FIELD_MASK,
      sample->position & OTG_POSITION_FIELD_MASK, sample->startup, sample->update,
      sample->sync, sample->keepout, sample->pipe);
}

void renoir_cursor_inventory(const struct boot_info *boot, struct pci_address device,
    const struct inventory_state *state, unsigned hubp, size_t occupied_end)
{
  unsigned otg = (state->hubp[hubp].control >> HUBP_VTG_SHIFT) & SELECTOR_MASK;
  unsigned opp = (state->otg[otg].source >> ODM_SEG0_SHIFT) & SELECTOR_MASK;
  unsigned mpcc = state->mux[opp] & SELECTOR_MASK;
  unsigned dpp = state->mpcc[mpcc].top & SELECTOR_MASK;
  struct cursor_sample a, b;
  snapshot(&a, otg);
  snapshot(&b, otg);
  klog("renoir-cursor-inventory: read-only qualified route OTG%u <- OPP%u <- MPCC%u <- DPP%u/HUBP%u; no cursor/PCI/power writes\n",
      otg, opp, mpcc, dpp, hubp);
  print_sample(&a, 0, otg, dpp, opp);
  print_sample(&b, 1, otg, dpp, opp);
  bool stable = !memcmp(a.cursor, b.cursor, sizeof(a.cursor));
  bool disabled = true;
  for (unsigned i = 0; i < RENOIR_PIPES; ++i) {
    disabled &= !((a.cursor[i].control | a.cursor[i].dpp |
        b.cursor[i].control | b.cursor[i].dpp) & CURSOR_ENABLE);
  }
  uint64_t gpu_base = (uint64_t)(state->fb_base & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  phys_addr_t cpu_base = (uint64_t)(state->gc_offset & FB_ADDRESS_MASK) << FB_ADDRESS_SHIFT;
  size_t pool_bytes = (size_t)state->memsize * 1024 * 1024;
  struct scanout_storage candidate = {0};
  bool memory = stable && disabled && renoir_firmware_qualifies(device, gpu_base) &&
      scanout_candidate(boot, cpu_base, pool_bytes, occupied_end, CURSOR_BYTES, &candidate);
  klog("renoir-cursor-inventory: cursor-pair-stable=%u all-disabled=%u UMA-policy-candidate=%u offset=%lx CPU=%lx GPU=%lx bytes=%zu occupied-end=%zu; old cursor fetch retirement unproven; not reserved/mapped/stored\n",
      stable, disabled, memory, candidate.offset, candidate.physical,
      memory ? gpu_base + candidate.offset : 0, candidate.bytes, occupied_end);
  uint32_t crystal = renoir_cursor_reference_clock(device);
  uint32_t reference = 0;
  if (crystal && a.reference == b.reference && a.timer == b.timer &&
      !(b.reference & REFCLK_ALTERNATE) && (b.timer & GLOBAL_TIMER_ENABLED)) {
    reference = crystal / ((b.timer & GLOBAL_TIMER_DIVISOR_MASK) == GLOBAL_TIMER_HALF ? 2 : 1);
  }
  klog("renoir-cursor-inventory: Linux DCHUB reference candidate=%u kHz range-40-60MHz=%u; pixel clock still unqualified\n",
      reference, reference >= 40000 && reference <= 60000);
  const struct otg_state *timing = &state->otg[otg];
  int32_t total = (timing->v_total & OTG_POSITION_FIELD_MASK) + 1;
  int32_t start = (int32_t)((timing->v_blank >> 16) & OTG_POSITION_FIELD_MASK) -
      (int32_t)(b.startup & OTG_VSTARTUP_MASK) + 1;
  if (start >= 0) {
    start -= (start / total) * total;
  } else {
    start += ((-start / total) + 1) * total - 1;
  }
  if (start >= 0 && start < total) {
    klog("renoir-cursor-inventory: inherited H-total=%u V-total=%u blank=%x Linux VUPDATE candidate=%u..%u; keepout execution bound unproven\n",
        (timing->h_total & OTG_POSITION_FIELD_MASK) + 1, (uint32_t)total, timing->v_blank,
        (uint32_t)start, (uint32_t)((start + 2) % total));
  }
  klog("renoir-cursor-inventory: latch/disable/old-image retirement unproven; idle pending/taken/ACK is not a transaction proof; no allocation or cursor writes\n");
}

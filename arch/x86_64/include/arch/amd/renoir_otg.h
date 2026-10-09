#ifndef ARCH_AMD_RENOIR_OTG_H
#define ARCH_AMD_RENOIR_OTG_H

#include <kernel/boot.h>
#include <kernel/pci.h>

enum renoir_otg_result {
  RENOIR_OTG_OK,
  RENOIR_OTG_RETRY,
  RENOIR_OTG_UNAVAILABLE,
};

struct renoir_otg_mode {
  uint32_t h_total, v_total, h_active, v_active;
  uint32_t blank_start, blank_end;
  uint32_t raw_h_total, raw_h_blank, raw_h_timing;
  uint32_t raw_v_total, raw_v_blank;
  uint32_t raw_v_total_min, raw_v_total_max, raw_v_total_control;
  uint32_t raw_control, raw_interlace;
  unsigned otg;
};

struct renoir_otg_info {
  struct pci_address address;
  phys_addr_t bar5;
  struct renoir_otg_mode mode;
};

struct renoir_otg_sample {
  uint64_t before_ns, after_ns;
  uint32_t frame_count, v_position, h_position;
  uint32_t raw_position, raw_status, raw_frame_count;
  bool in_blank;
  struct renoir_otg_mode mode;
};

/* BSP/IF=0 before AP startup. One attempt; no device/configuration writes or
 * firmware handoff. The audited Renoir register window is not a sized BAR.
 * Successful RO/NX/UC mappings remain private and stable until reboot. */
bool renoir_otg_prepare(const struct boot_info *boot, struct renoir_otg_info *info);

/* Sole BSP presenter, IF=1, no held locks. A narrow counter bracket is enclosed
 * by mode checks. RETRY means a non-atomic tuple crossed a line/frame boundary;
 * UNAVAILABLE permanently retires observation after a device/mode change.
 * Failure clears the sample. The caller owns advancement/period qualification,
 * rollover accounting and stall policy; frame-count phase is not assumed. */
enum renoir_otg_result renoir_otg_read(struct renoir_otg_sample *sample);
/* Fine polling between full reads: cached mode, FRAME/POSITION/STATUS only.
 * Incoherent or implausible tuples retry; identity/BAR/mode must be fully
 * revalidated once per cadence and immediately before an admitted write. */
enum renoir_otg_result renoir_otg_read_light(struct renoir_otg_sample *sample);
const char *renoir_otg_reason(void);

#endif

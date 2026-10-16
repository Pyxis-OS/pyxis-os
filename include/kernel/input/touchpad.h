#ifndef KERNEL_INPUT_TOUCHPAD_H
#define KERNEL_INPUT_TOUCHPAD_H

#include <stdbool.h>
#include <stdint.h>
#include <kernel/mouse.h>

#define TOUCHPAD_SCROLL_MM_PER_DETENT 8
#define TOUCHPAD_NATIVE_COUNTS_PER_MM 1000

enum touchpad_rejection {
  TOUCHPAD_REJECT_LIFT,
  TOUCHPAD_REJECT_PRESSURE,
  TOUCHPAD_REJECT_INVALID,
  TOUCHPAD_REJECT_COUNT,
  TOUCHPAD_REJECT_PALM,
  TOUCHPAD_REJECT_TYPING,
  TOUCHPAD_REJECT_EDGE,
  TOUCHPAD_REJECT_LOSS,
  TOUCHPAD_REJECT_PAIR,
  TOUCHPAD_REJECT_COUNT_TOTAL,
};

struct touchpad_contact_state {
  int32_t x, y, origin_x, origin_y;
  uint32_t id;
  unsigned origin_edges;
  uint8_t stable_samples;
  bool present, admitted, typing_gated, baseline;
};

enum touchpad_mode {
  TOUCHPAD_IDLE,
  TOUCHPAD_MOVE,
  TOUCHPAD_SCROLL,
};

struct touchpad_diagnostic {
  bool enabled, started, finished, have_pressure, have_coordinates, have_width;
  bool have_spacing;
  unsigned window;
  uint64_t started_at, reported_at, previous_at;
  uint64_t samples, accepted, resets, rejections[TOUCHPAD_REJECT_COUNT_TOTAL];
  uint64_t paired, unusable_pairs, ambiguous_pairs, separated_pairs, drag_samples;
  uint64_t basic_scroll_samples;
  uint64_t contacts[4], pressure[5], width[4], spacing_min, spacing_max;
  uint8_t pressure_min, pressure_max, width_min, width_max;
  int32_t x_min, x_max, y_min, y_max;
};

/* Adapter-owned storage, initialized before its source is attached and retained
 * for that source lifetime. All fields belong to the BSP with IF=0. */
struct touchpad_state {
  struct mouse_profile geometry;
  struct touchpad_contact_state contacts[2];
  bool ready, palm, loss_quarantine, scroll_near, previous_pressed, previous_paired;
  uint8_t previous_count, drag_contact;
  enum touchpad_mode mode;
  int64_t motion_fraction_x, motion_fraction_y;
  int64_t scroll_pending_x, scroll_pending_y;
  int64_t centroid_fraction_x, centroid_fraction_y;
  bool scroll_active_x, scroll_active_y;
  struct touchpad_diagnostic diagnostic;
};

struct touchpad_motion {
  int32_t dx, dy, scroll_x, scroll_y;
  bool reset;
};

/* BSP, IF=0, retained state; no physical-button ownership. Geometry is copied
 * at initialization. Scroll deltas are device counts toward right/down;
 * native movement is micrometres, projected by its adapter; legacy movement
 * remains display pixels. Buttons select drag policy but are never owned
 * here. reset discards caller scroll fractions only.
 * Native profiles require descriptor-derived micrometre coordinates/bounds,
 * both resolutions TOUCHPAD_NATIVE_COUNTS_PER_MM, and increasing Y upward.
 * Their complete fresh frames provide Tip/Confidence and valid stable IDs for
 * every Tip-active contact. Frame pointers are borrowed only for report().
 * Any Tip without Confidence quarantines the gesture until confirmed all-up;
 * native contacts never use Synaptics pressure/width thresholds or fallback. */
void touchpad_init(struct touchpad_state *state, const struct mouse_profile *profile);
/* Transport loss preserves rejection latches and requires a confirmed lift. */
void touchpad_reset(struct touchpad_state *state);
void touchpad_report(struct touchpad_state *state, const struct touchpad_sample *sample,
    unsigned buttons, uint64_t last_typing_ns, uint64_t now,
    struct touchpad_motion *motion);
void touchpad_service(struct touchpad_state *state, uint64_t now);

#endif

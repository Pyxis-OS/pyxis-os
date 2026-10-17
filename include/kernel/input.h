#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <kernel/keyboard.h>
#include <abi/input_policy.h>
#include <stdbool.h>
#include <stdint.h>

/* Source records are embedded in retained adapters. All state except published
 * availability belongs to BSP/IF=0; registration and delivery never allocate. */
enum input_pointer_kind { INPUT_POINTER_MOUSE, INPUT_POINTER_TOUCHPAD };

struct touchpad_state;

struct input_source {
  struct input_source *next;
  bool registered, keyboard_live, pointer_live;
  bool synthetic; /* Diagnostic origin; never excludes physical input. */
  bool keys[KEY_COUNT], suppressed_keys[KEY_COUNT], keyboard_unresolved;
  bool keyboard_initial_hold, pointer_initial_hold;
  enum key_code repeat_key;
  uint64_t repeat_deadline;
  uint32_t buttons, pending_buttons, suppressed_buttons;
  /* Admission-side unknown/initial holds, independent of routing's drain. */
  uint32_t focus_suppressed_buttons;
  enum input_pointer_kind pointer_kind;
  /* Optional one-to-one filter context, initialized and associated before
   * attachment; its retained adapter owns the storage for this source. */
  struct touchpad_state *touchpad;
  uint32_t scroll_numerator_x, scroll_denominator_x;
  uint32_t scroll_numerator_y, scroll_denominator_y;
  int64_t scroll_remainder_x, scroll_remainder_y;
};

void input_source_attach(struct input_source *source, bool keyboard, bool pointer);
void input_source_initial_keyboard(struct input_source *source, const bool keys[KEY_COUNT]);
void input_source_initial_pointer(struct input_source *source, uint32_t buttons);
void input_keyboard_snapshot(struct input_source *source, const bool keys[KEY_COUNT]);
void input_keyboard_unresolved(struct input_source *source);
void input_pointer_report(struct input_source *source, int32_t dx, int32_t dy,
    int32_t scroll_x, int32_t scroll_y, uint32_t buttons);
/* Raw axes are physically right/down (wheel pull is down). The coordinator
 * applies the source's direction policy once. Default scale is 120/1. Confirmed
 * HID multipliers use 120/m; touch profiles may supply their own rational gain.
 * Configure only before attachment or at a quiescent gesture boundary; it
 * clears fractional residue. No transport lifetime changes. */
void input_source_pointer_configure(struct input_source *source,
    enum input_pointer_kind kind, uint32_t numerator_x, uint32_t denominator_x,
    uint32_t numerator_y, uint32_t denominator_y);
void input_source_scroll_reset(struct input_source *source);
uint64_t input_last_typing_ns(void);
/* num_lock is the initial Num Lock state; the keyboard lights are sent to match. */
bool input_policy_install(uint32_t mouse_direction, uint32_t touchpad_direction,
    int32_t touchpad_speed, bool num_lock);
int32_t input_touchpad_speed(void);
void input_source_lost(struct input_source *source);
bool input_pointer_available(void);
uint32_t input_pointer_suppressed_buttons(void);
void input_pointer_drain(void);

#endif

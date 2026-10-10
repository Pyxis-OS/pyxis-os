#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <kernel/keyboard.h>
#include <stdbool.h>
#include <stdint.h>

/* Source records are embedded in retained adapters. All state except published
 * availability belongs to BSP/IF=0; registration and delivery never allocate. */
struct input_source {
  struct input_source *next;
  bool registered, keyboard_live, pointer_live;
  bool keys[KEY_COUNT], suppressed_keys[KEY_COUNT], keyboard_unresolved;
  enum key_code repeat_key;
  uint64_t repeat_deadline;
  uint32_t buttons, pending_buttons, suppressed_buttons;
};

void input_source_attach(struct input_source *source, bool keyboard, bool pointer);
void input_source_initial_keyboard(struct input_source *source, const bool keys[KEY_COUNT]);
void input_source_initial_pointer(struct input_source *source, uint32_t buttons);
void input_keyboard_snapshot(struct input_source *source, const bool keys[KEY_COUNT]);
void input_keyboard_unresolved(struct input_source *source);
void input_pointer_report(struct input_source *source, int32_t dx, int32_t dy,
    int32_t wheel, uint32_t buttons);
void input_source_lost(struct input_source *source);
bool input_pointer_available(void);
uint32_t input_pointer_suppressed_buttons(void);
void input_pointer_drain(void);

#endif

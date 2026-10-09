#ifndef KERNEL_OBJECT_KEYBOARD_H
#define KERNEL_OBJECT_KEYBOARD_H

#include <abi/keyboard.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct space;
struct process;
struct task_wait;
struct key_event;

#define KEYBOARD_EVENT_CAPACITY 64

struct keyboard_object {
  struct kernel_object object;
  struct space *space; /* Borrowed; spaces outlive their objects. */
  atomic_bool locked;
  struct process *owner; /* Borrowed under lock, cleared before destruction. */
  struct task_wait *reader; /* Task metadata, never a private-stack pointer. */
  struct keyboard_event events[KEYBOARD_EVENT_CAPACITY];
  size_t head, count;
  uint64_t acquisition; /* Never reused, including release/reacquisition. */
  bool selected;
  bool overlay_focused; /* Trusted popup suspends delivery, retaining capture. */
  bool terminal_layer; /* Under lock: hidden graphics overrides capture. */
  bool down[KEY_COUNT]; /* Only presses accepted since the last routing reset. */
};

/* BSP, IF=0. Space retains the initial reference. */
struct keyboard_object *keyboard_create(struct space *space, bool selected);
void keyboard_process_exit(struct process *process);
/* BSP routing, IF=0. Lock order: keyboard -> console input -> task queues. */
void keyboard_route_event(struct keyboard_object *keyboard, const struct key_event *event);
void keyboard_focus(struct keyboard_object *keyboard, bool selected);
/* BSP, IF=0. Keep capture ownership; optionally discard unread console text. */
void keyboard_set_layer(struct keyboard_object *keyboard, bool terminal_layer,
                        bool discard_input);
void keyboard_set_overlay(struct keyboard_object *keyboard, bool overlay_focused);
void keyboard_reset_input(struct keyboard_object *keyboard);
/* BSP, IF=0: clipboard admission checks focused acquisition ownership. */
bool keyboard_clipboard_owner(struct keyboard_object *keyboard, struct process *process,
    uint64_t acquisition);
/* Any CPU, preserves IF. INPUT and same-space authority are checked by caller;
 * ownership validation and event observation use the keyboard lock. */
bool keyboard_owned(struct keyboard_object *keyboard, struct process *process);
uint64_t keyboard_ready(struct keyboard_object *keyboard, struct process *process);
/* Current user task, IF=0. No allocation; READ may block. */
struct syscall_result keyboard_call(struct keyboard_object *keyboard, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif

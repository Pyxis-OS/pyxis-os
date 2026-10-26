#ifndef KERNEL_OBJECT_GAMEPAD_H
#define KERNEL_OBJECT_GAMEPAD_H

#include <abi/gamepad.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct space;
struct process;
struct task_wait;

#define GAMEPAD_EVENT_CAPACITY 64

struct gamepad_device {
  struct gamepad_info info;
  struct gamepad_state physical, accepted;
  uint32_t suppressed_buttons;
  bool live, physical_valid, accepted_valid, hat_suppressed;
};

struct gamepad_object {
  struct kernel_object object;
  struct gamepad_object *next; /* Borrowed registry; spaces retain their object. */
  struct space *space;
  atomic_bool locked;
  struct process *owner;
  struct task_wait *reader;
  bool read_active, selected, terminal_layer, overlay_focused, exhausted;
  uint64_t acquisition, sequence, catalog_generation;
  struct gamepad_device devices[GAMEPAD_DEVICE_CAPACITY];
  struct gamepad_event events[GAMEPAD_EVENT_CAPACITY];
  size_t head, count;
};

/* BSP/IF=0. The space retains the initial reference for its boot lifetime. */
struct gamepad_object *gamepad_create(struct space *space, bool selected);
void gamepad_process_exit(struct process *process);
void gamepad_focus(struct gamepad_object *gamepad, bool selected);
void gamepad_set_layer(struct gamepad_object *gamepad, bool terminal_layer);
void gamepad_set_overlay(struct gamepad_object *gamepad, bool focused);
/* Any CPU, preserves IF. */
bool gamepad_owned(struct gamepad_object *gamepad, struct process *process);
uint64_t gamepad_ready(struct gamepad_object *gamepad, struct process *process);
struct syscall_result gamepad_call(struct gamepad_object *gamepad, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif

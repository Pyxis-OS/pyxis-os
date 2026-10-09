#ifndef KERNEL_OBJECT_CLIPBOARD_H
#define KERNEL_OBJECT_CLIPBOARD_H

#include <abi/clipboard.h>
#include <abi/console.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <kernel/wait.h>

struct space;
struct process;
struct console_interrupt;
struct console_object;
struct clipboard_space;
struct clipboard_receiver;
struct clipboard_item;
struct key_event;
struct keyboard_event;

/* Borrowed descriptors are used only while their input object is retained.
 * Input lock -> clipboard lock -> scheduler locks. No heap/user access locked. */
struct clipboard_input {
  struct kernel_object *object, *output;
  struct space *space;
  atomic_bool *locked;
  uint8_t *bytes;
  size_t capacity;
  size_t *head, *count;
  bool *lost, *closed, *hung_up, *reader_active;
  struct process **reader_process;
  struct task_wait_link **first_reader;
  struct task_wait **input_wait;
  struct console_interrupt *interrupt;
};

struct clipboard_request {
  struct bsp_request request;
  struct process *process;
  struct kernel_object *object, *output, *attachment;
  struct clipboard_item *item;
  uint64_t operation, action_id, generation, mapping_identity, length, epoch, transaction_id;
  enum call_status status, refusal;
};

bool clipboard_space_init(struct space *space);
struct kernel_object *clipboard_local(struct space *space);
struct kernel_object *clipboard_shared(void);
void clipboard_init(void);
void clipboard_request_execute(struct clipboard_request *request);
void clipboard_process_exit(struct process *process);
void clipboard_space_cancel(struct space *space);
void clipboard_input_hangup(struct kernel_object *object);
void clipboard_input_notify(struct kernel_object *object);
void clipboard_stop_notify(void);
/* BSP, under the focused keyboard lock; fills only action metadata. */
void clipboard_graphics_key_event(struct space *space, struct process *owner,
    uint64_t acquisition, const struct key_event *physical, struct keyboard_event *event);
bool clipboard_key_event(struct space *space, const struct key_event *event);
bool clipboard_ordinary_allowed(struct kernel_object *object);
void clipboard_ordinary_transfer(struct kernel_object *object);
uint64_t clipboard_input_ready(struct kernel_object *object, struct process *caller);
bool clipboard_input_descriptor(struct kernel_object *object, struct clipboard_input *input);
bool terminal_clipboard_input(struct kernel_object *object, struct clipboard_input *input);
enum call_status console_paste_begin_read(struct console_object *console, bool timed, uint64_t deadline);
void console_paste_end_read(struct console_object *console);
enum call_status terminal_paste_begin_read(struct kernel_object *object, bool timed, uint64_t deadline);
void terminal_paste_end_read(struct kernel_object *object);

struct syscall_result clipboard_controller_refuse_call(uintptr_t request_address, size_t request_size);

struct syscall_result clipboard_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result clipboard_receiver_call(struct kernel_object *input, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif

#include <kernel/mm/kernel_owner.h>
#include <kernel/object/screenshot_saver.h>
#include <kernel/object/jit.h>
#include <kernel/object/clipboard.h>
#include <abi/console.h>
#include <abi/display.h>
#include <abi/keyboard.h>
#include <abi/pointer.h>
#include <abi/audio.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/file.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/gamepad.h>
#include <kernel/object/launcher.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/space.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>
#include <kernel/user/readiness_set.h>
#include <kernel/mm/heap.h>
#include <kernel/limit.h>

#include "../object/launcher_internal.h"

struct launch_prepared {
  struct process *process;
  struct task *task;
};

struct launch_preparation {
  struct launch_preparation *retire_next;
  size_t preparation_cpu;
  struct process *parent;
  struct process *reservation_parent; /* Pins the borrowed capability table. */
  struct execution_group *cleanup_group;
  struct execution_group *execution_group; /* Owned storage until publish/abort. */
  size_t count, capacity;
  struct capability_reservation reservation;
  struct capability_reserved_slot *slots;
  struct capability_grant *observers;
  struct launch_prepared *stages;
  struct task **tasks;
  size_t *destinations;
  void *storage;
};

static enum call_status capability_status(enum capability_result result,
    struct call_limit *detail)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return detail && detail->reason ? CALL_LIMIT : call_limit_reference(detail);
  case CAP_FULL: return call_limit_set(detail, CALL_LIMIT_HANDLE_ENCODING, 0,
      UINT64_C(1) << 32, CALL_LIMIT_HAS_LIMIT);
  default: return CALL_BAD_REQUEST;
  }
}

static enum call_status install_grants(struct launch_capture *capture, struct process *child,
    struct call_limit *detail)
{
  enum capability_result result = capability_validate_grants(&child->capabilities,
      capture->owned_grants, capture->grant_count);
  if (result != CAP_OK) {
    return capability_status(result, detail);
  }
  struct capability_reservation reservation = {0};
  if (capture->grant_count) {
    while ((result = capability_reserve(&child->capabilities, capture->grant_count,
        &reservation, capture->child_slots)) == CAP_FULL) {
      result = capability_grow(&child->capabilities);
      if (result != CAP_OK) {
        break;
      }
    }
    if (result != CAP_OK) {
      return result == CAP_LIMIT ? call_limit_set(detail, CALL_LIMIT_HANDLE_ENCODING,
          0, UINT64_C(1) << 32, CALL_LIMIT_HAS_LIMIT) : capability_status(result, detail);
    }
    capability_install_reserved(&reservation, capture->child_slots,
        capture->owned_grants, capture->grant_count, capture->child_handles);
  }
  for (size_t i = 0; i < capture->grant_count; ++i) {
    capture->grants[i].source = capture->child_handles[i];
  }

  struct process_environment_entry *entries = (void *)capture->startup.entries;
  for (size_t i = 0; i < capture->startup.entry_count; ++i) {
    if (entries[i].kind == ENVIRONMENT_CAP) {
      entries[i].value = capture->grants[entries[i].value].source;
    }
  }
  handle_t *directories = (void *)capture->startup.working_directories;
  for (size_t i = 0; i < capture->startup.working_directory_count; ++i) {
    directories[i] = capture->grants[directories[i]].source;
  }
  return CALL_OK;
}

static void launcher_batch_abort(struct launch_preparation *group)
{
  KASSERT(!cpu_interrupts_enabled() && group->preparation_cpu == arch_cpu_index());
  struct execution_group *previous = object_cleanup_enter(group->execution_group ?
      group->execution_group : task_cleanup_group());
  for (size_t i = group->count; i > 0; --i) {
    struct launch_prepared *stage = &group->stages[i - 1];
    capability_grant_release(&group->observers[i - 1]);
    user_task_discard_prepared(stage->task);
    KASSERT(process_destroy(stage->process) == MM_OK);
  }
  group->count = 0;
  capability_reservation_release(&group->reservation, group->slots);
  if (group->reservation_parent) {
    process_cleanup_unpin(group->reservation_parent);
    group->reservation_parent = NULL;
  }
  object_cleanup_leave(previous);
  if (group->execution_group) {
    object_release(&group->execution_group->object);
    group->execution_group = NULL;
  }
}

static void free_preparation(struct launch_preparation *group)
{
  kfree(group->destinations);
  kfree(group->storage);
  kfree(group);
}

struct launch_preparation *launcher_batch_create(size_t count, enum call_status *status,
    struct call_limit *detail)
{
  KASSERT(!cpu_interrupts_enabled() && count);
  *status = CALL_NO_MEMORY;
  size_t stage_bytes = sizeof(struct capability_reserved_slot) +
      sizeof(struct capability_grant) + sizeof(struct launch_prepared) + sizeof(struct task *);
  if (count > SIZE_MAX / stage_bytes) {
    *status = call_limit_encoding(detail);
    return NULL;
  }
  struct launch_preparation *group = kmalloc(sizeof(*group));
  if (!group) {
    return NULL;
  }
  *group = (struct launch_preparation){.preparation_cpu = arch_cpu_index(), .capacity = count};
  group->storage = kmalloc(count * stage_bytes);
  group->destinations = kmalloc(arch_cpu_count() * sizeof(*group->destinations));
  if (!group->storage || !group->destinations) {
    free_preparation(group);
    return NULL;
  }
  memset(group->storage, 0, count * stage_bytes);
  group->slots = group->storage;
  group->observers = (void *)(group->slots + count);
  group->stages = (void *)(group->observers + count);
  group->tasks = (void *)(group->stages + count);
  *status = CALL_OK;
  return group;
}

enum call_status launcher_batch_reserve(struct launch_preparation *group, size_t count,
    struct call_limit *detail)
{
  KASSERT(group && !group->count && !group->reservation.count);
  KASSERT(count && count <= group->capacity);
  struct process *parent = process_current();
  KASSERT(parent && !group->reservation_parent);
  if (!process_cleanup_pin(parent)) {
    return call_limit_reference(detail);
  }
  group->reservation_parent = parent;
  enum capability_result result = capability_request_reservation(count,
      &group->reservation, group->slots);
  return result == CAP_LIMIT ? call_limit_set(detail, CALL_LIMIT_HANDLE_ENCODING,
      0, UINT64_C(1) << 32, CALL_LIMIT_HAS_LIMIT) : capability_status(result, detail);
}

/* Kernel-added resources for a new space's first process. */
#define SPACE_DEVICE_RESOURCES 13

enum call_status launcher_preflight_space(struct launch_capture *capture,
    struct call_limit *detail)
{
  struct process_startup *startup = &capture->startup;
  size_t count = startup->entry_count;
  if (count > SIZE_MAX / sizeof(struct process_environment_entry) - SPACE_DEVICE_RESOURCES - 3) {
    return call_limit_encoding(detail);
  }
  struct process_environment_entry *entries = kmalloc((count + SPACE_DEVICE_RESOURCES + 3) *
      sizeof(*entries));
  if (!entries) {
    return CALL_NO_MEMORY;
  }
  if (count) {
    memcpy(entries, startup->entries, count * sizeof(*entries));
  }
  static const char *const mandatory[] = {
    "input", "output", "keyboard", "gamepad", "pointer", "display", "audio", "space",
    "stdin", "stdout", "stderr",
  };
  for (size_t i = 0; i < sizeof(mandatory) / sizeof(mandatory[0]); ++i) {
    entries[count++] = (struct process_environment_entry){
        mandatory[i], ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, 0, 0};
  }
  if (capture->space.jit) {
    entries[count++] = (struct process_environment_entry){"jit", ENVIRONMENT_CAP, 0, 0, 0};
  }
  if (capture->space.screenshot_saver) {
    entries[count++] = (struct process_environment_entry){"screenshot_saver", ENVIRONMENT_CAP, 0, 0, 0};
  }
  if (capture->space.terminal_control) {
    entries[count++] = (struct process_environment_entry){"terminal_pointer", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, 0, 0};
  }
  if (capture->space.clipboard_local) {
    entries[count++] = (struct process_environment_entry){"clipboard_local", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, 0, 0};
  }
  if (capture->space.clipboard_shared) {
    entries[count++] = (struct process_environment_entry){"clipboard_shared", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, 0, 0};
  }
  struct process_startup source = *startup;
  source.entries = entries;
  source.entry_count = count;
  struct process_startup_storage measured;
  enum call_status status = process_startup_measure(&source, &measured, detail);
  if (status != CALL_OK) {
    kfree(entries);
    return status;
  }
  capture->space_entries = entries;
  return CALL_OK;
}

/* BSP, IF=0. Installs the new space's devices in CHILD and points STARTUP at
 * an owned resource array that appends them, plus console streams. */
enum call_status launcher_install_space_devices(struct process *child, struct space *space,
    struct process_startup *startup, struct process_environment_entry **owned,
    struct call_limit *detail)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  struct kernel_object *console = &space->console->object;
  handle_t input, output, keyboard, gamepad, pointer, display, audio, control;
  handle_t terminal_pointer = HANDLE_INVALID, saver = HANDLE_INVALID, jit = HANDLE_INVALID;
  handle_t local_clipboard = HANDLE_INVALID, shared_clipboard = HANDLE_INVALID;
  handle_t streams[3];
  enum capability_result result = capability_install_with_limit(&child->capabilities, console,
      CONSOLE_RIGHT_READ | CONSOLE_RIGHT_INTERRUPT, 0, &input, detail);
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, console, CONSOLE_RIGHT_WRITE, 0, &output, detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, console, CONSOLE_RIGHT_READ, 0,
        &streams[0], detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, console, CONSOLE_RIGHT_WRITE, 0,
        &streams[1], detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, console, CONSOLE_RIGHT_WRITE, 0,
        &streams[2], detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, &space->keyboard->object,
        KEYBOARD_RIGHT_INPUT, 0, &keyboard, detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, &space->gamepad->object,
        GAMEPAD_RIGHT_INPUT, 0, &gamepad, detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, &space->pointer->object,
        POINTER_RIGHT_INPUT, 0, &pointer, detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, &space->display->object,
        DISPLAY_RIGHT_DRAW, 0, &display, detail);
  }
  if (result == CAP_OK) {
    result = capability_install_with_limit(&child->capabilities, &space->audio->object,
        AUDIO_RIGHT_PLAYBACK, 0, &audio, detail);
  }
  if (result == CAP_OK) {
    struct kernel_object *space_control = space_control_create(space);
    if (!space_control) {
      return CALL_NO_MEMORY;
    }
    result = capability_install_with_limit(&child->capabilities, space_control,
        SPACE_RIGHT_SET_TITLE | SPACE_RIGHT_SET_AFFINITY, 0, &control, detail);
    object_release(space_control);
  }
  if (result == CAP_OK && space->jit_enabled) {
    struct kernel_object *object = jit_create();
    if (!object) {
      return CALL_NO_MEMORY;
    }
    result = capability_install_with_limit(&child->capabilities, object, JIT_RIGHT_MANAGE, 0, &jit, detail);
    object_release(object);
  }
  if (result == CAP_OK && space->screenshot_saver_enabled) {
    struct kernel_object *object = screenshot_saver_create(space);
    if (!object) {
      return CALL_NO_MEMORY;
    }
    result = capability_install_with_limit(&child->capabilities, object, SCREENSHOT_SAVER_RIGHT_SERVE, 0, &saver, detail);
    object_release(object);
  }
  if (result == CAP_OK && space->terminal_control_enabled) {
    result = capability_install_with_limit(&child->capabilities, &space->terminal_pointer->object,
        TERMINAL_POINTER_RIGHT_CONTROL, 0, &terminal_pointer, detail);
  }
  if (result == CAP_OK && space->clipboard_local_enabled) {
    result = capability_install_with_limit(&child->capabilities, clipboard_local(space), CLIPBOARD_RIGHTS, 0,
        &local_clipboard, detail);
  }
  if (result == CAP_OK && space->clipboard_shared_enabled) {
    result = capability_install_with_limit(&child->capabilities, clipboard_shared(), CLIPBOARD_RIGHTS, 0,
        &shared_clipboard, detail);
  }
  if (result != CAP_OK) {
    return capability_status(result, detail);
  }

  size_t count = startup->entry_count;
  struct process_environment_entry *entries = *owned;
  KASSERT(entries);
  if (count) {
    memcpy(entries, startup->entries, count * sizeof(*entries));
  }
  entries[count++] = (struct process_environment_entry){"input", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, input, 0};
  entries[count++] = (struct process_environment_entry){"output", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, output, 0};
  entries[count++] = (struct process_environment_entry){"keyboard", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, keyboard, 0};
  entries[count++] = (struct process_environment_entry){"gamepad", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, gamepad, 0};
  entries[count++] = (struct process_environment_entry){"pointer", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, pointer, 0};
  entries[count++] = (struct process_environment_entry){"display", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, display, 0};
  entries[count++] = (struct process_environment_entry){"audio", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, audio, 0};
  entries[count++] = (struct process_environment_entry){"space", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, control, 0};
  if (jit != HANDLE_INVALID) {
    entries[count++] = (struct process_environment_entry){"jit", ENVIRONMENT_CAP, 0, jit, 0};
  }
  if (saver != HANDLE_INVALID) {
    entries[count++] = (struct process_environment_entry){"screenshot_saver", ENVIRONMENT_CAP, 0, saver, 0};
  }
  if (terminal_pointer != HANDLE_INVALID) {
    entries[count++] = (struct process_environment_entry){"terminal_pointer", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, terminal_pointer, 0};
  }
  if (local_clipboard != HANDLE_INVALID) {
    entries[count++] = (struct process_environment_entry){"clipboard_local", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, local_clipboard, 0};
  }
  if (shared_clipboard != HANDLE_INVALID) {
    entries[count++] = (struct process_environment_entry){"clipboard_shared", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, shared_clipboard, 0};
  }
  static const char *const stream_names[] = {"stdin", "stdout", "stderr"};
  for (size_t i = 0; i < 3; ++i) {
    entries[count++] = (struct process_environment_entry){
      stream_names[i], ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, streams[i], 0,
    };
  }
  startup->entries = entries;
  startup->entry_count = count;
  *owned = entries;
  return CALL_OK;
}

/* SPACE receives the child. A new space also gives it the space's devices. */
static enum call_status prepare_child(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, struct space *space,
    size_t preferred_cpu, struct execution_group *execution_group, bool new_space,
    struct call_limit *detail)
{
  KASSERT(!cpu_interrupts_enabled());
  KASSERT(group && group->preparation_cpu == arch_cpu_index() &&
      group->count < group->capacity);
  KASSERT(!group->parent || group->parent == parent);
  KASSERT(!group->count || group->execution_group == execution_group);
  KASSERT(group->reservation.table == &parent->capabilities);
  KASSERT(group->count < group->reservation.count);
  group->parent = parent;
  struct process *child;
  uintptr_t entry, stack_top;
  /* Only boot-archive images are read in place; the others were copied. */
  bool external = capture->image->backing != FILE_INITRD;
  size_t size = external ? capture->captured_image.size : capture->image->size;
  enum call_status status = execution_group_check(execution_group, space);
  if (status != CALL_OK) {
    if (!external) {
      file_end_operation(capture->image);
      capture->image_operation = false;
    }
    return status;
  }
  if (execution_group && !group->execution_group) {
    if (!object_retain(&execution_group->object)) {
      if (!external) {
        file_end_operation(capture->image);
        capture->image_operation = false;
      }
      return call_limit_reference(detail);
    }
    group->execution_group = execution_group;
  }
  const void *bytes = external ? image_capture_read_begin(&capture->captured_image) :
      capture->image->data;
  enum mm_result loaded = user_process_load(space, bytes, size,
      capture->initial_stack_bytes, &child, &entry, &stack_top);
  if (external) {
    image_capture_read_end(&capture->captured_image);
  }
  if (!external) {
    file_end_operation(capture->image);
    capture->image_operation = false;
  }
  if (loaded != MM_OK) {
    return loaded == MM_BUSY ? CALL_BUSY :
        loaded == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
  }
  child->launched_by = parent->list_key;
  if (capture->startup.argc) {
    process_set_label(child, capture->startup.argv[0]);
  }

  if (execution_group) {
    if (!object_retain(&execution_group->object)) {
      status = call_limit_reference(detail);
      goto fail;
    }
    child->execution_group = execution_group;
    child->capabilities.execution_group = execution_group;
  }
  status = install_grants(capture, child, detail);
  if (status != CALL_OK) {
    goto fail;
  }
  struct process_startup startup = capture->startup;
  struct process_environment_entry *entries = NULL;
  if (new_space) {
    status = launcher_request_space_devices(capture, child, &startup, &entries);
    if (status != CALL_OK) {
      if (status == CALL_LIMIT && detail) {
        *detail = capture->limit;
      }
      kfree(entries);
      goto fail;
    }
  }
  status = process_prepare_startup(child, &startup, &capture->startup_storage, detail);
  if (status != CALL_OK) {
    kfree(entries);
    goto fail;
  }

  /* Provisioning grants are only capture sources. Named grants moved into
   * ENV; cwd keeps its independent installed chain. Unnamed sources convey no
   * extra hidden child authority. */
  for (size_t i = 0; i < capture->grant_count; ++i) {
    handle_t handle = capture->child_handles[i];
    bool kept = false, named = false;
    for (size_t j = 0; j < startup.working_directory_count; ++j) {
      kept |= handle == startup.working_directories[j];
    }
    for (size_t j = 0; j < startup.entry_count; ++j) {
      const struct process_environment_entry *named_entry = &startup.entries[j];
      named |= named_entry->kind == ENVIRONMENT_CAP && named_entry->value == handle;
    }
    if (!kept && !named) {
      KASSERT(capability_close(&child->capabilities, handle) == CAP_OK);
    }
  }

  kfree(entries);

  struct task *task;
  /* Publication places the child; equal loads favour the preferred CPU. */
  enum mm_result submitted = user_task_prepare(child, entry, stack_top, preferred_cpu, &task);
  if (submitted != MM_OK) {
    status = submitted == MM_BUSY ? CALL_BUSY :
        submitted == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }

  enum capability_result retained = capability_grant_retain(&child->control->object,
      PROCESS_RIGHTS, 0, &group->observers[group->count]);
  if (retained == CAP_OK) {
    retained = forwarder_derive_grant_with_limit(&group->observers[group->count], task_forwarder_operation(), detail);
  }
  if (retained != CAP_OK) {
    capability_grant_release(&group->observers[group->count]);
    user_task_discard_prepared(task);
    status = capability_status(retained, detail);
    goto fail;
  }
  group->stages[group->count++] = (struct launch_prepared){child, task};
  return CALL_OK;

fail:
  KASSERT(process_destroy(child) == MM_OK);
  return status;
}

static enum call_status launcher_batch_prepare_owned(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, size_t parent_cpu,
    struct execution_group *execution_group, struct call_limit *detail)
{
  /* Affinity setup ends at the space's first launch request, whatever its outcome. */
  task_space_close_setup(parent->space);
  return prepare_child(group, capture, parent, parent->space, parent_cpu,
      execution_group, false, detail);
}

enum call_status launcher_batch_publish(struct launch_preparation *group, handle_t *children,
    struct readiness_process_watch *watch, struct readiness_set_registration *registration,
    struct call_limit *detail)
{
  KASSERT(!cpu_interrupts_enabled() && group->count &&
      group->preparation_cpu == arch_cpu_index());
  struct task **tasks = group->tasks;
  size_t count = group->count;
  for (size_t i = 0; i < count; ++i) {
    tasks[i] = group->stages[i].task;
  }
  enum capability_result validated = capability_validate_grants(&group->parent->capabilities,
      group->observers, count);
  enum call_status status = capability_status(validated, detail);
  if (status == CALL_OK) {
    status = watch ? execution_group_publish_observed(group->execution_group, tasks, count,
        group->destinations, arch_cpu_count(), watch, registration, detail) :
      execution_group_publish(group->execution_group, tasks, count, group->destinations,
        arch_cpu_count(), detail);
  }
  if (status != CALL_OK) {
    for (size_t i = 0; i < count; ++i) {
      children[i] = HANDLE_INVALID;
    }
    return status;
  }
  /* No failing step remains after the tasks become runnable. Observer grants
   * remain private through preparation and appear together under one table guard. */
  capability_install_reserved(&group->reservation, group->slots, group->observers,
      count, children);
  group->count = 0;
  if (group->reservation_parent) {
    process_cleanup_unpin(group->reservation_parent);
    group->reservation_parent = NULL;
  }
  if (group->execution_group) {
    object_release(&group->execution_group->object);
    group->execution_group = NULL;
  }
  return CALL_OK;
}

struct launch_retirement_queue {
  struct spinlock guard;
  struct launch_preparation *head;
};

static struct launch_retirement_queue *retired_preparations;

void launcher_ram_reapers_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled() && !retired_preparations);
  retired_preparations = kmalloc(arch_cpu_count() * sizeof(*retired_preparations));
  if (!retired_preparations) {
    panic("cannot allocate prepared-launch retirement queues");
  }
  memset(retired_preparations, 0, arch_cpu_count() * sizeof(*retired_preparations));
  for (size_t cpu = 0; cpu < arch_cpu_count(); ++cpu) {
    atomic_init(&retired_preparations[cpu].guard.locked, false);
  }
}

bool launcher_ram_retire_pending(void)
{
  KASSERT(!cpu_interrupts_enabled() && retired_preparations);
  struct launch_retirement_queue *queue = &retired_preparations[arch_cpu_index()];
  spin_lock(&queue->guard);
  bool pending = queue->head != NULL;
  spin_unlock(&queue->guard);
  return pending;
}

void launcher_batch_discard(struct launch_preparation *group)
{
  KASSERT(!cpu_interrupts_enabled() && group && retired_preparations);
  KASSERT(group->preparation_cpu == arch_cpu_index());
  if (!group->count) {
    launcher_batch_abort(group);
    free_preparation(group);
    return;
  }
  if (group->execution_group) {
    execution_group_cleanup_begin(group->execution_group);
    group->cleanup_group = group->execution_group;
  } else {
    group->cleanup_group = object_cleanup_defer();
  }
  for (size_t i = 0; i < group->count; ++i) {
    process_prepare_transfer(group->stages[i].process);
  }
  size_t cpu = group->preparation_cpu;
  struct launch_retirement_queue *queue = &retired_preparations[cpu];
  spin_lock(&queue->guard);
  group->retire_next = queue->head;
  queue->head = group;
  spin_unlock(&queue->guard);
  process_ram_cleanup_notify(cpu);
}

void launcher_reap_prepared_ram(void)
{
  KASSERT(cpu_interrupts_enabled() && retired_preparations);
  uint64_t flags = cpu_save_interrupts();
  struct launch_retirement_queue *queue = &retired_preparations[arch_cpu_index()];
  spin_lock(&queue->guard);
  struct launch_preparation *group = queue->head;
  if (group) {
    queue->head = group->retire_next;
    group->retire_next = NULL;
  }
  spin_unlock(&queue->guard);
  if (group) {
    struct execution_group *cleanup_group = group->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(cleanup_group);
    launcher_batch_abort(group);
    free_preparation(group);
    object_cleanup_leave(previous);
    if (cleanup_group) {
      execution_group_cleanup_end(cleanup_group);
    }
  }
  cpu_restore_interrupts(flags);
}

static enum call_status launcher_start_owned(struct launch_capture *capture,
    struct process *parent,
    size_t parent_cpu, struct execution_group *execution_group,
    struct capability_reservation *reservation, struct capability_reserved_slot *slots,
    handle_t *result, struct readiness_set_registration *registration,
    struct call_limit *detail)
{
  enum call_status status;
  struct launch_preparation *group = launcher_batch_create(1, &status, detail);
  if (!group) {
    return status;
  }
  group->reservation = *reservation;
  group->slots[0] = slots[0];
  *reservation = (struct capability_reservation){0};
  *result = HANDLE_INVALID;
  status = launcher_batch_prepare(group, capture, parent, parent_cpu,
      execution_group, detail);
  if (status == CALL_OK && capture->watch) {
    status = readiness_process_watch_bind(capture->watch, &group->observers[0], detail);
  }
  if (status == CALL_OK && capture->watch &&
      !readiness_process_watch_start(capture->watch, task_forwarder_operation())) {
    status = CALL_DENIED;
  }
  if (status == CALL_OK) {
    status = launcher_batch_publish(group, result, capture->watch, registration, detail);
  }
  if (status != CALL_OK) {
    launcher_batch_abort(group);
  }
  free_preparation(group);
  return status;
}

enum call_status launcher_prepare_space(struct launch_capture *capture,
    struct process *parent, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots, handle_t *result, struct call_limit *detail)
{
  KASSERT(task_vm_kernel_active() && capture->target_space);
  enum call_status status;
  struct launch_preparation *group = launcher_batch_create(1, &status, detail);
  if (!group) {
    return status;
  }
  group->reservation = *reservation;
  group->slots[0] = slots[0];
  *reservation = (struct capability_reservation){0};
  status = prepare_child(group, capture, parent,
      capture->target_space, SIZE_MAX, NULL, true, detail);
  if (status == CALL_OK) {
    status = launcher_batch_publish(group, result, NULL, NULL, detail);
  }
  if (status != CALL_OK) {
    launcher_batch_abort(group);
  }
  free_preparation(group);
  return status;
}

enum call_status launcher_batch_prepare(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, size_t parent_cpu,
    struct execution_group *execution_group, struct call_limit *detail)
{
  KASSERT(task_vm_kernel_active());
  return launcher_batch_prepare_owned(group, capture, parent, parent_cpu, execution_group, detail);
}

enum call_status launcher_start(struct launch_capture *capture, struct process *parent,
    size_t parent_cpu, struct execution_group *execution_group,
    struct capability_reservation *reservation, struct capability_reserved_slot *slots,
    handle_t *result, struct readiness_set_registration *registration,
    struct call_limit *detail)
{
  KASSERT(task_vm_kernel_active());
  return launcher_start_owned(capture, parent, parent_cpu, execution_group,
      reservation, slots, result, registration, detail);
}

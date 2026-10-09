#include <kernel/object/clipboard.h>
#include <abi/console.h>
#include <abi/display.h>
#include <abi/keyboard.h>
#include <abi/pointer.h>
#include <abi/audio.h>
#include <arch/smp.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/object/console.h>
#include <kernel/object/display.h>
#include <kernel/object/file.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/launcher.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/space.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>
#include <kernel/mm/heap.h>

#include "../object/launcher_internal.h"

struct launch_prepared {
  struct process *process;
  struct task *task;
  handle_t observer;
};

struct launch_preparation {
  struct process *parent;
  struct execution_group *execution_group;
  size_t count;
  struct launch_prepared stages[LAUNCH_BATCH_MAX];
};

static enum call_status capability_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

static enum call_status install_grants(struct launch_capture *capture,
    struct process *parent, struct process *child)
{
  for (size_t i = 0; i < capture->grant_count; ++i) {
    struct launch_grant *grant = &capture->grants[i];
    /* Capture switches from parent source handles to installed child handles;
     * the actual parent grants survive both success and failure. */
    enum capability_result result = capability_grant(&child->capabilities,
        &parent->capabilities, grant->source, grant->rights,
        grant->transport, &grant->source);
    if (result != CAP_OK) {
      return capability_status(result);
    }
  }

  struct process_binding *resources = (void *)capture->startup.resources;
  for (size_t i = 0; i < capture->startup.resource_count; ++i) {
    resources[i].handle = capture->grants[resources[i].handle].source;
  }
  struct process_binding *roots = (void *)capture->startup.roots;
  for (size_t i = 0; i < capture->startup.root_count; ++i) {
    roots[i].handle = capture->grants[roots[i].handle].source;
  }
  handle_t *directories = (void *)capture->startup.working_directories;
  for (size_t i = 0; i < capture->startup.working_directory_count; ++i) {
    directories[i] = capture->grants[directories[i]].source;
  }
  if (capture->startup.namespace) {
    capture->startup.namespace = capture->grants[capture->startup.namespace - 1].source;
  }
  for (size_t i = 0; i < STARTUP_STREAM_COUNT; ++i) {
    struct startup_stream *stream = &capture->startup.streams[i];
    if (stream->protocol != STARTUP_STREAM_NONE) {
      stream->handle = capture->grants[stream->handle].source;
    }
  }
  return CALL_OK;
}

static void launcher_batch_abort(struct launch_preparation *group)
{
  struct execution_group *previous = object_cleanup_enter(group->execution_group);
  for (size_t i = group->count; i > 0; --i) {
    struct launch_prepared *stage = &group->stages[i - 1];
    KASSERT(capability_close(&group->parent->capabilities, stage->observer) == CAP_OK);
    process_control_detach_task(stage->process->control);
    user_task_discard_prepared(stage->task);
    KASSERT(process_destroy(stage->process) == MM_OK);
  }
  group->count = 0;
  object_cleanup_leave(previous);
}

struct launch_preparation *launcher_batch_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct launch_preparation *group = kmalloc(sizeof(*group));
  if (group) {
    *group = (struct launch_preparation){0};
  }
  return group;
}

/* Kernel-added resources for a new space's first process. */
#define SPACE_DEVICE_RESOURCES 10

/* BSP, IF=0. Installs the new space's devices in CHILD and points STARTUP at
 * an owned resource array that appends them, plus console streams. */
static enum call_status install_space_devices(struct process *child, struct space *space,
    struct process_startup *startup, struct process_binding **owned)
{
  struct kernel_object *console = &space->console->object;
  handle_t input, output, keyboard, pointer, display, audio, control;
  handle_t terminal_pointer = HANDLE_INVALID;
  handle_t local_clipboard = HANDLE_INVALID, shared_clipboard = HANDLE_INVALID;
  handle_t streams[STARTUP_STREAM_COUNT];
  enum capability_result result = capability_install(&child->capabilities, console,
      CONSOLE_RIGHT_READ | CONSOLE_RIGHT_INTERRUPT, 0, &input);
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, console, CONSOLE_RIGHT_WRITE, 0, &output);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, console, CONSOLE_RIGHT_READ, 0,
        &streams[STARTUP_STDIN]);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, console, CONSOLE_RIGHT_WRITE, 0,
        &streams[STARTUP_STDOUT]);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, console, CONSOLE_RIGHT_WRITE, 0,
        &streams[STARTUP_STDERR]);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, &space->keyboard->object,
        KEYBOARD_RIGHT_INPUT, 0, &keyboard);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, &space->pointer->object,
        POINTER_RIGHT_INPUT, 0, &pointer);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, &space->display->object,
        DISPLAY_RIGHT_DRAW, 0, &display);
  }
  if (result == CAP_OK) {
    result = capability_install(&child->capabilities, &space->audio->object,
        AUDIO_RIGHT_PLAYBACK, 0, &audio);
  }
  if (result == CAP_OK) {
    struct kernel_object *space_control = space_control_create(space);
    if (!space_control) {
      return CALL_NO_MEMORY;
    }
    result = capability_install(&child->capabilities, space_control,
        SPACE_RIGHT_SET_TITLE | SPACE_RIGHT_SET_AFFINITY, 0, &control);
    object_release(space_control);
  }
  if (result == CAP_OK && space->terminal_control_enabled) {
    result = capability_install(&child->capabilities, &space->terminal_pointer->object,
        TERMINAL_POINTER_RIGHT_CONTROL, 0, &terminal_pointer);
  }
  if (result == CAP_OK && space->clipboard_local_enabled) {
    result = capability_install(&child->capabilities, clipboard_local(space), CLIPBOARD_RIGHTS, 0,
        &local_clipboard);
  }
  if (result == CAP_OK && space->clipboard_shared_enabled) {
    result = capability_install(&child->capabilities, clipboard_shared(), CLIPBOARD_RIGHTS, 0,
        &shared_clipboard);
  }
  if (result != CAP_OK) {
    return capability_status(result);
  }

  size_t count = startup->resource_count;
  struct process_binding *resources = kmalloc((count + SPACE_DEVICE_RESOURCES) *
      sizeof(*resources));
  if (!resources) {
    return CALL_NO_MEMORY;
  }
  if (count) {
    memcpy(resources, startup->resources, count * sizeof(*resources));
  }
  resources[count++] = (struct process_binding){"input", input};
  resources[count++] = (struct process_binding){"output", output};
  resources[count++] = (struct process_binding){"keyboard", keyboard};
  resources[count++] = (struct process_binding){"pointer", pointer};
  resources[count++] = (struct process_binding){"display", display};
  resources[count++] = (struct process_binding){"audio", audio};
  resources[count++] = (struct process_binding){"space", control};
  if (terminal_pointer != HANDLE_INVALID) {
    resources[count++] = (struct process_binding){"terminal_pointer", terminal_pointer};
  }
  if (local_clipboard != HANDLE_INVALID) {
    resources[count++] = (struct process_binding){"clipboard_local", local_clipboard};
  }
  if (shared_clipboard != HANDLE_INVALID) {
    resources[count++] = (struct process_binding){"clipboard_shared", shared_clipboard};
  }
  startup->resources = resources;
  startup->resource_count = count;
  for (size_t i = 0; i < STARTUP_STREAM_COUNT; ++i) {
    startup->streams[i] = (struct startup_stream){PROTOCOL_CONSOLE, streams[i]};
  }
  *owned = resources;
  return CALL_OK;
}

/* SPACE receives the child. A new space also gives it the space's devices. */
static enum call_status prepare_child(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, struct space *space,
    size_t preferred_cpu, struct execution_group *execution_group, bool new_space)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(group && group->count < LAUNCH_BATCH_MAX);
  KASSERT(!group->parent || group->parent == parent);
  KASSERT(!group->count || group->execution_group == execution_group);
  group->parent = parent;
  group->execution_group = execution_group;
  struct process *child;
  uintptr_t entry;
  /* Only boot-archive images are read in place; the others were copied. */
  bool external = capture->image->backing != FILE_INITRD;
  const void *bytes = external ? capture->external_image : capture->image->data;
  size_t size = external ? capture->external_image_size : capture->image->size;
  enum call_status status = execution_group_check(execution_group, space);
  if (status != CALL_OK) {
    if (!external) {
      file_end_operation(capture->image);
    }
    return status;
  }
  enum mm_result loaded = user_process_load(space, bytes, size, &child, &entry);
  if (!external) {
    file_end_operation(capture->image);
  }
  if (loaded != MM_OK) {
    return loaded == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
  }

  if (execution_group) {
    if (!object_retain(&execution_group->object)) {
      status = CALL_LIMIT;
      goto fail;
    }
    child->execution_group = execution_group;
    child->capabilities.execution_group = execution_group;
  }
  status = install_grants(capture, parent, child);
  if (status != CALL_OK) {
    goto fail;
  }
  struct process_binding *resources = NULL;
  if (new_space) {
    status = install_space_devices(child, space, &capture->startup, &resources);
    if (status != CALL_OK) {
      goto fail;
    }
  }
  enum mm_result prepared = process_prepare_startup(child, &capture->startup);
  kfree(resources);
  if (prepared != MM_OK) {
    status = prepared == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }

  struct task *task;
  /* Publication places the child; equal loads favour the preferred CPU. */
  enum mm_result submitted = user_task_prepare(child, entry,
      USER_INITIAL_STACK_BASE + USER_INITIAL_STACK_SIZE, preferred_cpu, &task);
  if (submitted != MM_OK) {
    status = submitted == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }

  handle_t observer;
  enum capability_result installed = capability_install(&parent->capabilities,
      &child->control->object, PROCESS_RIGHTS, 0, &observer);
  if (installed != CAP_OK) {
    user_task_discard_prepared(task);
    status = capability_status(installed);
    goto fail;
  }
  process_control_attach_task(child->control, task);
  group->stages[group->count++] = (struct launch_prepared){child, task, observer};
  return CALL_OK;

fail:
  KASSERT(process_destroy(child) == MM_OK);
  return status;
}

enum call_status launcher_batch_prepare(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, size_t parent_cpu,
    struct execution_group *execution_group)
{
  /* Affinity setup ends at the space's first launch request, whatever its outcome. */
  task_space_close_setup(parent->space);
  return prepare_child(group, capture, parent, parent->space, parent_cpu,
      execution_group, false);
}

enum call_status launcher_batch_publish(struct launch_preparation *group, handle_t *children)
{
  KASSERT(arch_cpu_index() == 0 && group->count);
  struct task *tasks[LAUNCH_BATCH_MAX];
  size_t count = group->count;
  for (size_t i = 0; i < count; ++i) {
    children[i] = group->stages[i].observer;
    tasks[i] = group->stages[i].task;
  }
  enum call_status status = execution_group_publish(group->execution_group, tasks, count);
  if (status != CALL_OK) {
    for (size_t i = 0; i < count; ++i) {
      children[i] = HANDLE_INVALID;
    }
    return status;
  }
  group->count = 0;
  return CALL_OK;
}

void launcher_batch_discard(struct launch_preparation *group)
{
  KASSERT(arch_cpu_index() == 0 && group);
  launcher_batch_abort(group);
  kfree(group);
}

enum call_status launcher_start(struct launch_capture *capture, struct process *parent,
    size_t parent_cpu, struct execution_group *execution_group, handle_t *result)
{
  struct launch_preparation group = {0};
  *result = HANDLE_INVALID;
  enum call_status status = launcher_batch_prepare(&group, capture, parent, parent_cpu,
      execution_group);
  if (status == CALL_OK) {
    status = launcher_batch_publish(&group, result);
  }
  if (status != CALL_OK) {
    launcher_batch_abort(&group);
  }
  return status;
}

enum call_status launcher_create_space(struct launch_capture *capture, struct process *parent,
    handle_t *result)
{
  KASSERT(arch_cpu_index() == 0);
  *result = HANDLE_INVALID;
  struct launch_space *request = &capture->space;
  /* Only a boot-archive image still holds its file's operation ownership. */
  bool in_memory = capture->image && capture->image->backing == FILE_INITRD;
  enum call_status status = space_name_taken(request->name) ? CALL_BAD_REQUEST : CALL_OK;
  size_t words = space_cpu_words();
  uint64_t *ceiling = NULL;
  if (status == CALL_OK) {
    ceiling = kmalloc(words * sizeof(*ceiling));
    status = ceiling ? CALL_OK : CALL_NO_MEMORY;
  }
  if (status != CALL_OK) {
    if (in_memory) {
      file_end_operation(capture->image);
    }
    return status;
  }
  memset(ceiling, 0, words * sizeof(*ceiling));
  for (size_t cpu = 0; cpu < request->cpu_count; ++cpu) {
    ceiling[cpu / 64] |= request->cpus[cpu / 64] & (UINT64_C(1) << (cpu % 64));
  }
  struct space *space = space_create(request->name, request->title, ceiling);
  space->terminal_control_enabled = request->terminal_control;
  space->clipboard_local_enabled = request->clipboard_local;
  space->clipboard_shared_enabled = request->clipboard_shared;
  if (!capture->image) {
    space_report_unstarted(space, request->reason);
    return CALL_OK;
  }

  /* The creator's CPU says nothing about the new space: no placement preference,
   * so ties fall to the lowest AP, as for any unrelated task. */
  struct launch_preparation group = {0};
  status = prepare_child(&group, capture, parent, space, SIZE_MAX, NULL, true);
  if (status == CALL_OK) {
    status = launcher_batch_publish(&group, result);
  }
  if (status != CALL_OK) {
    launcher_batch_abort(&group);
    char reason[40];
    sprintf(reason, "init launch failed (status %u)", (unsigned)status);
    space_report_unstarted(space, reason);
    return status;
  }
  klog("userspace: space %s started\n", space->name);
  return CALL_OK;
}

#include <arch/smp.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
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

enum call_status launcher_batch_prepare(struct launch_preparation *group,
    struct launch_capture *capture, struct process *parent, size_t parent_cpu,
    struct execution_group *execution_group)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(group && group->count < LAUNCH_BATCH_MAX);
  KASSERT(!group->parent || group->parent == parent);
  KASSERT(!group->count || group->execution_group == execution_group);
  group->parent = parent;
  group->execution_group = execution_group;
  struct process *child;
  uintptr_t entry;
  bool external = capture->image->backing == FILE_HOST ||
      capture->image->backing == FILE_NPFS;
  const void *bytes = external ? capture->external_image : capture->image->data;
  size_t size = external ? capture->external_image_size : capture->image->size;
  enum call_status status = execution_group_check(execution_group, parent->space);
  if (status != CALL_OK) {
    if (!external) {
      file_end_operation(capture->image);
    }
    return status;
  }
  enum mm_result loaded = user_process_load(parent->space, bytes, size, &child, &entry);
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
  enum mm_result prepared = process_prepare_startup(child, &capture->startup);
  if (prepared != MM_OK) {
    status = prepared == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }

  struct task *task;
  /* Publication places the child; equal loads favour the parent's CPU. */
  enum mm_result submitted = user_task_prepare(child, entry,
      USER_INITIAL_STACK_BASE + USER_INITIAL_STACK_SIZE, parent_cpu, &task);
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

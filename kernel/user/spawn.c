#include <arch/smp.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/process.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>
#include <kernel/mm/heap.h>

struct launch_prepared {
  struct process *process;
  struct task *task;
  handle_t observer;
};

struct launch_group {
  struct process *parent;
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
        &parent->capabilities, grant->source, grant->rights, &grant->source);
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
  for (size_t i = 0; i < STARTUP_STREAM_COUNT; ++i) {
    struct startup_stream *stream = &capture->startup.streams[i];
    if (stream->protocol != STARTUP_STREAM_NONE) {
      stream->handle = capture->grants[stream->handle].source;
    }
  }
  return CALL_OK;
}

static void launcher_group_abort(struct launch_group *group)
{
  for (size_t i = group->count; i > 0; --i) {
    struct launch_prepared *stage = &group->stages[i - 1];
    KASSERT(capability_close(&group->parent->capabilities, stage->observer) == CAP_OK);
    user_task_discard_prepared(stage->task);
    KASSERT(process_destroy(stage->process) == MM_OK);
  }
  group->count = 0;
}

struct launch_group *launcher_group_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct launch_group *group = kmalloc(sizeof(*group));
  if (group) {
    *group = (struct launch_group){0};
  }
  return group;
}

enum call_status launcher_group_prepare(struct launch_group *group,
    struct launch_capture *capture, struct process *parent, size_t cpu_index)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(group && group->count < LAUNCH_BATCH_MAX);
  KASSERT(!group->parent || group->parent == parent);
  group->parent = parent;
  struct process *child;
  uintptr_t entry;
  bool host = capture->image->backing == FILE_HOST;
  const void *bytes = host ? capture->host_image : capture->image->data;
  size_t size = host ? capture->host_image_size : capture->image->size;
  enum mm_result loaded = user_process_load(parent->space, bytes, size, &child, &entry);
  if (!host) {
    file_end_operation(capture->image);
  }
  if (loaded != MM_OK) {
    return loaded == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
  }

  enum call_status status = install_grants(capture, parent, child);
  if (status != CALL_OK) {
    goto fail;
  }
  enum mm_result prepared = process_prepare_startup(child, &capture->startup);
  if (prepared != MM_OK) {
    status = prepared == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }

  struct task *task;
  enum mm_result submitted = user_task_prepare_on(cpu_index, child, entry,
      USER_INITIAL_STACK_BASE + USER_INITIAL_STACK_SIZE, &task);
  if (submitted != MM_OK) {
    status = submitted == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }

  handle_t observer;
  enum capability_result installed = capability_install(&parent->capabilities,
      &child->control->object, PROCESS_RIGHT_WAIT, &observer);
  if (installed != CAP_OK) {
    user_task_discard_prepared(task);
    status = capability_status(installed);
    goto fail;
  }
  group->stages[group->count++] = (struct launch_prepared){child, task, observer};
  return CALL_OK;

fail:
  KASSERT(process_destroy(child) == MM_OK);
  return status;
}

void launcher_group_publish(struct launch_group *group, handle_t *children)
{
  KASSERT(arch_cpu_index() == 0 && group->count);
  struct task *tasks[LAUNCH_BATCH_MAX];
  size_t count = group->count;
  for (size_t i = 0; i < count; ++i) {
    children[i] = group->stages[i].observer;
    tasks[i] = group->stages[i].task;
  }
  user_task_publish_group(tasks, count);
  group->count = 0;
}

void launcher_group_discard(struct launch_group *group)
{
  KASSERT(arch_cpu_index() == 0 && group);
  launcher_group_abort(group);
  kfree(group);
}

enum call_status launcher_start(struct launch_capture *capture, struct process *parent,
                                size_t cpu_index, handle_t *result)
{
  struct launch_group group = {0};
  *result = HANDLE_INVALID;
  enum call_status status = launcher_group_prepare(&group, capture, parent, cpu_index);
  if (status == CALL_OK) {
    launcher_group_publish(&group, result);
  } else {
    launcher_group_abort(&group);
  }
  return status;
}

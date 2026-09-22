#include <arch/smp.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/process.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>

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
  return CALL_OK;
}

enum call_status launcher_start(struct launch_capture *capture, struct process *parent,
                                 size_t cpu_index, handle_t *result)
{
  KASSERT(arch_cpu_index() == 0);
  *result = HANDLE_INVALID;
  struct process *child;
  uintptr_t entry;
  enum mm_result loaded = user_process_load(parent->space, capture->image->data,
      capture->image->size, &child, &entry);
  file_end_operation(capture->image);
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

  handle_t observer;
  enum capability_result installed = capability_install(&parent->capabilities,
      &child->control->object, PROCESS_RIGHT_WAIT, &observer);
  if (installed != CAP_OK) {
    status = capability_status(installed);
    goto fail;
  }
  enum mm_result submitted = user_task_create_on(cpu_index, child, entry,
      USER_INITIAL_STACK_BASE + USER_INITIAL_STACK_SIZE);
  if (submitted != MM_OK) {
    KASSERT(capability_close(&parent->capabilities, observer) == CAP_OK);
    status = submitted == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
    goto fail;
  }
  /* Submission transfers child ownership. It may run/exit before this return;
   * the parent's observer already retains the independent completion object. */
  *result = observer;
  return CALL_OK;

fail:
  KASSERT(process_destroy(child) == MM_OK);
  return status;
}

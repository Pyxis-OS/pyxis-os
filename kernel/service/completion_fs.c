#include <kernel/completion.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>

enum call_status native_completion_fs_reserve(struct native_completion *operation)
{
  switch (operation->target.object->type) {
  case OBJECT_FILE:
    return file_completion_reserve(operation);
  case OBJECT_DIRECTORY:
    return directory_completion_reserve(operation);
  default:
    return CALL_WRONG_TYPE;
  }
}

void native_completion_fs_start(struct native_completion *operation)
{
  if (operation->target.object->type == OBJECT_FILE) {
    file_completion_start(operation);
  } else {
    KASSERT(operation->target.object->type == OBJECT_DIRECTORY);
    directory_completion_start(operation);
  }
}

void native_completion_fs_discard(struct native_completion *operation)
{
  if (operation->target.object->type == OBJECT_FILE) {
    file_completion_discard(operation);
  } else {
    KASSERT(operation->target.object->type == OBJECT_DIRECTORY);
    directory_completion_discard(operation);
  }
}

void native_completion_fs_cancel(struct native_completion *operation)
{
  /* The core already latched cancellation atomically. Queued work is awake;
   * active work samples it only at safe boundaries, retaining disk ownership. */
  (void)operation;
}

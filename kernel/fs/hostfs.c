#include <abi/file.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/virtio/pci.h>

struct hostfs_node {
  struct hostfs_node *next;
  struct kernel_object *object;
  struct virtio_fs_node node;
  struct virtio_fs_open opened, writer;
  uint64_t cursor_identity;
};

/* Queue links are BSP/IF=0. Only the worker uses session and node protocol
 * state. Its scratch stays off the task stack and is never lent to a caller. */
static struct virtio_fs_session *session;
static bool starting;
static enum call_status startup_failure = CALL_UNAVAILABLE;
static struct hostfs_request *first_request, *last_request;
static struct hostfs_node *first_retired, *last_retired;
static uint64_t next_identity = 1;
static struct virtio_fs_directory_batch batch;
static struct virtio_fs_dirent entry;

static enum call_status call_result(enum virtio_fs_result result)
{
  switch (result) {
  case VIRTIO_FS_OK: return CALL_OK;
  case VIRTIO_FS_NOT_FOUND: return CALL_NOT_FOUND;
  case VIRTIO_FS_DENIED: return CALL_DENIED;
  case VIRTIO_FS_WRONG_TYPE: return CALL_WRONG_TYPE;
  case VIRTIO_FS_UNSUPPORTED: return CALL_BAD_OPERATION;
  case VIRTIO_FS_INVALID: return CALL_BAD_REQUEST;
  case VIRTIO_FS_LIMIT: return CALL_LIMIT;
  case VIRTIO_FS_NO_MEMORY: return CALL_NO_MEMORY;
  case VIRTIO_FS_NO_SPACE: return CALL_NO_SPACE;
  case VIRTIO_FS_QUOTA: return CALL_QUOTA;
  case VIRTIO_FS_FILE_TOO_LARGE: return CALL_FILE_TOO_LARGE;
  case VIRTIO_FS_READ_ONLY: return CALL_READ_ONLY;
  case VIRTIO_FS_ALREADY_EXISTS: return CALL_ALREADY_EXISTS;
  case VIRTIO_FS_NOT_EMPTY: return CALL_NOT_EMPTY;
  case VIRTIO_FS_BUSY: return CALL_BUSY;
  case VIRTIO_FS_TIMED_OUT: return CALL_TIMED_OUT;
  case VIRTIO_FS_OUTCOME_UNKNOWN: return CALL_OUTCOME_UNKNOWN;
  case VIRTIO_FS_UNAVAILABLE: return CALL_UNAVAILABLE;
  default: return CALL_IO;
  }
}

static uint64_t native_kind(enum virtio_fs_kind kind)
{
  switch (kind) {
  case VIRTIO_FS_FILE: return DIRECTORY_KIND_FILE;
  case VIRTIO_FS_DIRECTORY: return DIRECTORY_KIND_DIRECTORY;
  case VIRTIO_FS_SYMLINK: return DIRECTORY_KIND_SYMLINK;
  case VIRTIO_FS_UNKNOWN: return DIRECTORY_KIND_UNKNOWN;
  default: return DIRECTORY_KIND_OTHER;
  }
}

void hostfs_prepare(void)
{
  KASSERT(arch_cpu_index() == 0 && !session && !starting);
  starting = true;
}

void hostfs_start_failed(enum virtio_fs_result result)
{
  KASSERT(arch_cpu_index() == 0 && !session && result != VIRTIO_FS_OK);
  uint64_t flags = cpu_save_interrupts();
  starting = false;
  startup_failure = call_result(result);
  while (first_request) {
    struct hostfs_request *request = first_request;
    first_request = request->next;
    request->next = NULL;
    request->status = startup_failure;
    task_wait_wake(request->wait);
  }
  last_request = NULL;
  cpu_restore_interrupts(flags);
}

void hostfs_start(struct virtio_fs_session *started)
{
  KASSERT(arch_cpu_index() == 0 && !session && started->ready);
  uint64_t flags = cpu_save_interrupts();
  session = started;
  starting = false;
  cpu_restore_interrupts(flags);
}

void hostfs_submit(struct hostfs_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  request->next = NULL;
  if (!session && !starting) {
    request->status = startup_failure;
    task_wait_wake(request->wait);
    return;
  }
  if (last_request) {
    last_request->next = request;
  } else {
    first_request = request;
  }
  last_request = request;
  virtio_fs_pci_wake();
}

void hostfs_retire(struct hostfs_node *node)
{
  KASSERT(arch_cpu_index() == 0 && session);
  node->next = NULL;
  if (last_retired) {
    last_retired->next = node;
  } else {
    first_retired = node;
  }
  last_retired = node;
  virtio_fs_pci_wake();
}

static void destroy_node(struct hostfs_node *node)
{
  if (node->opened.node) {
    virtio_fs_close(&node->opened);
  }
  if (node->writer.node) {
    virtio_fs_close(&node->writer);
  }
  if (node->node.references) {
    virtio_fs_node_put(&node->node);
  }
  /* Failure already stops the session. Local ownership still retires; these
   * allocations were never exposed as DMA storage and can be freed normally. */
  uint64_t flags = cpu_save_interrupts();
  kfree(node->object);
  kfree(node);
  cpu_restore_interrupts(flags);
}

static enum call_status create_node(struct hostfs_request *request)
{
  if (!session->ready) {
    return CALL_UNAVAILABLE;
  }
  if (next_identity == UINT64_MAX) {
    return CALL_LIMIT;
  }
  uint64_t flags = cpu_save_interrupts();
  struct hostfs_node *node = kmalloc(sizeof(*node));
  cpu_restore_interrupts(flags);
  if (!node) {
    return CALL_NO_MEMORY;
  }
  *node = (struct hostfs_node){0};

  enum virtio_fs_result result;
  if (request->operation == HOSTFS_ROOT) {
    result = virtio_fs_root(session, &node->node);
  } else {
    result = virtio_fs_lookup(&request->node->node, request->name, request->count, &node->node);
  }
  enum call_status status = call_result(result);
  if (status == CALL_OK && request->operation == HOSTFS_LOOKUP &&
      native_kind(node->node.kind) != request->kind) {
    status = CALL_WRONG_TYPE;
  }
  if (status != CALL_OK) {
    destroy_node(node);
    return status;
  }

  flags = cpu_save_interrupts();
  if (node->node.kind == VIRTIO_FS_DIRECTORY) {
    struct directory_object *directory = directory_create(DIRECTORY_HOST);
    if (directory) {
      directory->host = node;
      node->object = &directory->object;
    }
  } else {
    struct file_object *file = file_create_host(node);
    if (file) {
      node->object = &file->object;
    }
  }
  cpu_restore_interrupts(flags);
  if (!node->object) {
    destroy_node(node);
    return CALL_NO_MEMORY;
  }
  node->cursor_identity = next_identity++;
  request->object = node->object;
  return CALL_OK;
}

static enum call_status create_file(struct hostfs_request *request)
{
  uint64_t flags = cpu_save_interrupts();
  struct hostfs_node *node = kmalloc(sizeof(*node));
  if (!node) {
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  *node = (struct hostfs_node){0};
  struct file_object *file = file_create_host(node);
  if (!file) {
    kfree(node);
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  node->object = &file->object;
  enum capability_result installed = capability_install(request->table, node->object,
      request->rights, &request->handle);
  cpu_restore_interrupts(flags);
  if (installed != CAP_OK) {
    KASSERT(installed == CAP_NO_MEMORY || installed == CAP_LIMIT);
    destroy_node(node);
    return installed == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }

  /* The sole caller is parked and cannot use the installed handle until the
   * worker wakes it. All local storage/slot allocation precedes host CREATE. */
  enum virtio_fs_result result = virtio_fs_create(&request->node->node, request->name,
      request->count, &node->node, &node->writer);
  if (result != VIRTIO_FS_OK) {
    flags = cpu_save_interrupts();
    KASSERT(capability_close(request->table, request->handle) == CAP_OK);
    cpu_restore_interrupts(flags);
    request->handle = HANDLE_INVALID;
    destroy_node(node);
    return call_result(result);
  }
  object_release(node->object); /* Only the installed capability owns it now. */
  return CALL_OK;
}

static enum call_status open_node(struct hostfs_node *node)
{
  if (!session->ready) {
    return CALL_UNAVAILABLE;
  }
  if (node->opened.node) {
    return CALL_OK;
  }
  return call_result(virtio_fs_open(&node->node, VIRTIO_FS_ACCESS_READ, &node->opened));
}

static enum call_status enumerate(struct hostfs_request *request)
{
  struct hostfs_node *node = request->node;
  request->entry = (struct directory_enumerate_reply){.cursor = request->cursor};
  if (!request->cursor.generation && request->cursor.position) {
    return CALL_BAD_REQUEST;
  }
  /* Identity scopes opaque cookies to this retained open directory, never to
   * a host node ID or another lookup of the same directory. */
  if (request->cursor.generation && request->cursor.generation != node->cursor_identity) {
    request->entry.outcome = DIRECTORY_CHANGED;
    return CALL_OK;
  }
  enum call_status status = open_node(node);
  if (status != CALL_OK) {
    return status;
  }

  uint64_t cookie = request->cursor.position;
  for (;;) {
    enum virtio_fs_result result = virtio_fs_readdir(&node->opened, cookie, &batch);
    if (result != VIRTIO_FS_OK) {
      return call_result(result);
    }
    result = virtio_fs_directory_next(&batch, &entry);
    if (result == VIRTIO_FS_OK) {
      request->entry.kind = native_kind(entry.kind);
      request->entry.name_size = entry.name_length + 1;
      if (request->count < request->entry.name_size) {
        request->entry.outcome = DIRECTORY_BUFFER_TOO_SMALL;
      } else {
        request->entry.outcome = DIRECTORY_ENTRY;
        request->entry.cursor = (struct directory_cursor){node->cursor_identity, entry.next_cookie};
        memcpy(request->name, entry.name, request->entry.name_size);
      }
      return CALL_OK;
    }
    if (batch.end) {
      request->entry.outcome = DIRECTORY_END;
      request->entry.cursor = (struct directory_cursor){node->cursor_identity, cookie};
      return CALL_OK;
    }
    /* A batch containing only dot entries still advances the host cookie.
     * No attribute/entry cache: the next native call performs a fresh READDIR. */
    cookie = batch.next_cookie;
  }
}

static enum call_status perform(struct hostfs_request *request)
{
  if (!session->ready) {
    return CALL_UNAVAILABLE;
  }
  switch (request->operation) {
  case HOSTFS_ROOT:
  case HOSTFS_LOOKUP:
    return create_node(request);
  case HOSTFS_CREATE:
    return create_file(request);
  case HOSTFS_ENUMERATE:
    return enumerate(request);
  case HOSTFS_SIZE: {
    struct virtio_fs_attributes attributes;
    enum virtio_fs_result result = virtio_fs_getattr(&request->node->node, &attributes);
    if (result == VIRTIO_FS_OK) {
      request->offset = attributes.size;
    }
    return call_result(result);
  }
  case HOSTFS_WRITE:
  case HOSTFS_RESIZE: {
    struct hostfs_node *node = request->node;
    if (!node->writer.node) {
      enum virtio_fs_result result = virtio_fs_open(&node->node, VIRTIO_FS_ACCESS_WRITE,
          &node->writer);
      if (result != VIRTIO_FS_OK) {
        return call_result(result);
      }
    }
    if (request->operation == HOSTFS_RESIZE) {
      return call_result(virtio_fs_resize(&node->writer, request->offset));
    }
    return call_result(virtio_fs_write(&node->writer, request->offset,
        request->data, request->count, &request->count));
  }
  case HOSTFS_READ: {
    enum call_status status = open_node(request->node);
    if (status != CALL_OK) {
      return status;
    }
    return call_result(virtio_fs_read(&request->node->opened, request->offset,
        request->data, request->count, &request->count));
  }
  }
  return CALL_BAD_OPERATION;
}

bool hostfs_service(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  struct hostfs_node *retired = first_retired;
  if (retired) {
    first_retired = retired->next;
    if (!first_retired) {
      last_retired = NULL;
    }
  }
  struct hostfs_request *request = NULL;
  if (!retired && first_request) {
    request = first_request;
    first_request = request->next;
    if (!first_request) {
      last_request = NULL;
    }
    request->next = NULL;
  }
  cpu_restore_interrupts(flags);

  if (retired) {
    destroy_node(retired);
    return true;
  }
  if (!request) {
    return false;
  }
  request->status = perform(request);
  flags = cpu_save_interrupts();
  task_wait_wake(request->wait);
  /* Wake returns the record and any resulting object to the caller. */
  cpu_restore_interrupts(flags);
  return true;
}

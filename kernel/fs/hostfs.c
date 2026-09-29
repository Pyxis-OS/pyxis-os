#include <abi/file.h>
#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>
#include <kernel/service/profile.h>
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

static void profile_duration_merge(uint64_t *flags, struct profile_duration *duration,
    uint64_t total, uint64_t maximum)
{
  profile_add(flags, &duration->total_ns, total);
  if (maximum > duration->maximum_ns) {
    duration->maximum_ns = maximum;
  }
}

static void finish_host_profile(struct profile_host_snapshot *profile,
    struct hostfs_request *request)
{
  uint64_t resumed = arch_monotonic_ns();
  struct hostfs_profile *sample = &request->profile;
  struct profile_host_operation *stats = request->operation == HOSTFS_READ ?
      &profile->read : &profile->write;
  uint64_t *flags = &profile->flags;
  profile_add(flags, &stats->requests, 1);
  profile_add(flags, &stats->requested_bytes, sample->requested_bytes);
  if (request->status == CALL_OK) {
    profile_add(flags, &stats->completed_bytes, request->count);
    if (request->count && request->count < sample->requested_bytes) {
      profile_add(flags, &stats->short_transfers, 1);
    }
    if (!request->count && sample->requested_bytes && request->operation == HOSTFS_READ) {
      profile_add(flags, &stats->eof, 1);
    }
  } else {
    profile_add(flags, &stats->failures, 1);
  }
  profile_duration_add(flags, &stats->publication, sample->started_ns, sample->published_ns);
  profile_duration_add(flags, &stats->bsp_queue, sample->published_ns, sample->forwarded_ns);
  profile_duration_add(flags, &stats->worker_queue, sample->forwarded_ns, sample->service_started_ns);
  profile_duration_add(flags, &stats->service, sample->service_started_ns, sample->service_ended_ns);
  profile_duration_add(flags, &stats->resume, sample->service_ended_ns, resumed);
  profile_duration_add(flags, &stats->total, sample->started_ns, resumed);
  struct virtio_fs_profile *transport = &sample->transport;
  profile_add(flags, &stats->submissions, transport->submissions);
  profile_add(flags, &stats->completions, transport->completions);
  profile_add(flags, &stats->transport_failures, transport->failures);
  profile_duration_merge(flags, &stats->transport, transport->completed_ns, transport->completed_max_ns);
  profile_duration_merge(flags, &stats->transport_failed, transport->failed_ns, transport->failed_max_ns);
  if (transport->saturated) {
    *flags |= PROFILE_SATURATED;
  }
}

struct hostfs_request *hostfs_request_prepare(enum hostfs_operation operation)
{
  struct profile_host_snapshot *profile = profile_host_current();
  bool profiled = (profile->flags & PROFILE_ACTIVE) &&
      (operation == HOSTFS_READ || operation == HOSTFS_WRITE);
  uint64_t started_ns = profiled ? arch_monotonic_ns() : 0;
  struct hostfs_request *request =
      (struct hostfs_request *)bsp_request_prepare(BSP_SERVICE_HOSTFS);
  *request = (struct hostfs_request){
    .request = request->request,
    .operation = operation,
    .profile = {.active = profiled, .started_ns = started_ns},
  };
  return request;
}

void hostfs_request_submit_and_wait(struct hostfs_request *request)
{
  struct profile_host_snapshot *profile = profile_host_current();
  bool profiled = request->profile.active;
  bsp_request_submit_and_wait(&request->request);
  if (profiled) {
    finish_host_profile(profile, request);
  }
}

void hostfs_request_release(struct hostfs_request *request)
{
  KASSERT(request && !request->next && !request->node && !request->destination && !request->table);
  KASSERT(!request->captured && !request->object);
  bsp_request_release(&request->request);
}

void hostfs_request_published(struct hostfs_request *request)
{
  KASSERT(request && request->request.state == BSP_REQUEST_PREPARED);
  if (request->profile.active) {
    request->profile.requested_bytes = request->count;
    request->profile.published_ns = arch_monotonic_ns();
  }
}

/* Detach every worker reference and input loan before returning ownership. */
static void complete_request(struct hostfs_request *request)
{
  request->next = NULL;
  request->node = NULL;
  request->destination = NULL;
  request->table = NULL;
  bsp_request_complete(&request->request);
}

void hostfs_prepare(void)
{
  KASSERT(arch_cpu_index() == 0 && !session && !starting);
  starting = true;
}

static void reject_profile(struct hostfs_request *request)
{
  if (request->profile.active) {
    uint64_t now = arch_monotonic_ns();
    request->profile.service_started_ns = now;
    request->profile.service_ended_ns = now;
  }
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
    if (!first_request) {
      last_request = NULL;
    }
    request->next = NULL;
    request->status = startup_failure;
    reject_profile(request);
    complete_request(request);
  }
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
  KASSERT(arch_cpu_index() == 0 && request->request.state == BSP_REQUEST_FORWARDED);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->profile.active) {
    request->profile.forwarded_ns = arch_monotonic_ns();
  }
  request->next = NULL;
  if (!session && !starting) {
    request->status = startup_failure;
    reject_profile(request);
    complete_request(request);
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

static enum call_status create_child(struct hostfs_request *request)
{
  bool directory = request->kind == DIRECTORY_KIND_DIRECTORY;
  if (directory && next_identity == UINT64_MAX) {
    return CALL_LIMIT;
  }
  uint64_t flags = cpu_save_interrupts();
  struct hostfs_node *node = kmalloc(sizeof(*node));
  if (!node) {
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  *node = (struct hostfs_node){0};
  if (directory) {
    struct directory_object *object = directory_create(DIRECTORY_HOST);
    if (object) {
      object->host = node;
      node->object = &object->object;
      node->cursor_identity = next_identity++;
    }
  } else {
    struct file_object *file = file_create_host(node);
    if (file) {
      node->object = &file->object;
    }
  }
  if (!node->object) {
    kfree(node);
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  enum capability_result installed = capability_install(request->table, node->object,
      request->rights, 0, &request->handle);
  cpu_restore_interrupts(flags);
  if (installed != CAP_OK) {
    KASSERT(installed == CAP_NO_MEMORY || installed == CAP_LIMIT);
    destroy_node(node);
    return installed == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }

  /* The sole caller is parked and cannot use the installed handle until the
   * worker wakes it. All local storage/slot allocation precedes host creation. */
  enum virtio_fs_result result;
  if (directory) {
    result = virtio_fs_mkdir(&request->node->node, request->name, request->count, &node->node);
  } else {
    result = virtio_fs_create(&request->node->node, request->name,
        request->count, &node->node, &node->writer);
  }
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

/* Preflight observes a name, not a transaction with the later mutation.
 * Retire the temporary lookup first so cleanup cannot hide an acknowledged
 * mutation behind a subsequent FORGET failure. */
static enum virtio_fs_result lookup_kind(struct hostfs_node *parent,
    const char *name, size_t length, enum virtio_fs_kind *kind)
{
  struct virtio_fs_node child = {0};
  enum virtio_fs_result result = virtio_fs_lookup(&parent->node, name, length, &child);
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  *kind = child.kind;
  return virtio_fs_node_put(&child);
}

static enum call_status remove_child(struct hostfs_request *request)
{
  enum virtio_fs_kind kind;
  enum virtio_fs_result result = lookup_kind(request->node, request->name, request->count, &kind);
  if (result != VIRTIO_FS_OK) {
    return call_result(result);
  }
  if (request->kind != DIRECTORY_KIND_ANY && request->kind != native_kind(kind)) {
    return CALL_WRONG_TYPE;
  }
  return call_result(virtio_fs_remove(&request->node->node, request->name,
      request->count, kind == VIRTIO_FS_DIRECTORY));
}

static enum call_status rename_child(struct hostfs_request *request)
{
  enum virtio_fs_kind kind;
  enum virtio_fs_result result = lookup_kind(request->node, request->name, request->count, &kind);
  if (result != VIRTIO_FS_OK) {
    return call_result(result);
  }
  if (kind != VIRTIO_FS_FILE) {
    return CALL_WRONG_TYPE;
  }
  if (request->replace) {
    result = lookup_kind(request->destination, request->destination_name,
        request->destination_length, &kind);
    if (result != VIRTIO_FS_OK && result != VIRTIO_FS_NOT_FOUND) {
      return call_result(result);
    }
    if (result == VIRTIO_FS_OK && kind != VIRTIO_FS_FILE) {
      return CALL_WRONG_TYPE;
    }
  }
  /* NO_REPLACE is enforced by the host operation itself, including when a
   * destination appears after preflight. Never emulate it with LOOKUP. */
  return call_result(virtio_fs_rename(&request->node->node, request->name, request->count,
      &request->destination->node, request->destination_name, request->destination_length,
      request->replace));
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

static enum call_status open_writer(struct hostfs_node *node)
{
  if (node->writer.node) {
    return CALL_OK;
  }
  return call_result(virtio_fs_open(&node->node, VIRTIO_FS_ACCESS_WRITE, &node->writer));
}

static enum call_status capture_file(struct hostfs_request *request)
{
  struct hostfs_node *node = request->node;
  enum call_status status = open_node(node);
  if (status != CALL_OK) {
    return status;
  }
  struct virtio_fs_attributes attributes;
  status = call_result(virtio_fs_getattr(&node->node, &attributes));
  if (status != CALL_OK) {
    return status;
  }
  if (!attributes.size) {
    return CALL_BAD_REQUEST;
  }
  if (attributes.size > request->count) {
    return CALL_LIMIT;
  }

  size_t size = attributes.size;
  uint64_t flags = cpu_save_interrupts();
  uint8_t *bytes = kmalloc(size);
  cpu_restore_interrupts(flags);
  if (!bytes) {
    return CALL_NO_MEMORY;
  }

  /* Keep using the retained node/open across reads, even if its pathname is
   * replaced. This copy becomes stable, but is not a host-side snapshot. */
  size_t offset = 0;
  while (offset < size) {
    size_t count = size - offset;
    if (count > VIRTIO_FS_READ_MAX) {
      count = VIRTIO_FS_READ_MAX;
    }
    status = call_result(virtio_fs_read(&node->opened, offset, bytes + offset, count, &count));
    if (status != CALL_OK) {
      goto fail;
    }
    if (!count) {
      status = CALL_IO;
      goto fail;
    }
    offset += count;
  }

  status = call_result(virtio_fs_getattr(&node->node, &attributes));
  if (status != CALL_OK) {
    goto fail;
  }
  if (attributes.size != size) {
    status = CALL_IO;
    goto fail;
  }
  request->captured = bytes;
  request->count = size;
  return CALL_OK;

fail:
  flags = cpu_save_interrupts();
  kfree(bytes);
  cpu_restore_interrupts(flags);
  return status;
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
    return create_child(request);
  case HOSTFS_REMOVE:
    return remove_child(request);
  case HOSTFS_RENAME:
    return rename_child(request);
  case HOSTFS_ENUMERATE:
    return enumerate(request);
  case HOSTFS_CAPTURE:
    return capture_file(request);
  case HOSTFS_SIZE: {
    struct virtio_fs_attributes attributes;
    enum virtio_fs_result result = virtio_fs_getattr(&request->node->node, &attributes);
    if (result == VIRTIO_FS_OK) {
      request->offset = attributes.size;
    }
    return call_result(result);
  }
  case HOSTFS_SYNC: {
    struct hostfs_node *node = request->node;
    bool directory = node->node.kind == VIRTIO_FS_DIRECTORY;
    enum call_status status = directory ? open_node(node) : open_writer(node);
    if (status != CALL_OK) {
      return status;
    }
    return call_result(virtio_fs_sync(directory ? &node->opened : &node->writer));
  }
  case HOSTFS_WRITE:
  case HOSTFS_RESIZE: {
    struct hostfs_node *node = request->node;
    enum call_status status = open_writer(node);
    if (status != CALL_OK) {
      return status;
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
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED && !session->profile);
  if (request->profile.active) {
    request->profile.service_started_ns = arch_monotonic_ns();
    session->profile = &request->profile.transport;
  }
  request->status = perform(request);
  session->profile = NULL;
  flags = cpu_save_interrupts();
  if (request->profile.active) {
    request->profile.service_ended_ns = arch_monotonic_ns();
  }
  complete_request(request);
  /* Completion returns the record and any resulting object to the caller. */
  cpu_restore_interrupts(flags);
  return true;
}

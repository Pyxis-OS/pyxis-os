#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/virtio/fs.h>
#include <kernel/virtio/pci.h>
#include <kernel/virtio/queue.h>
#include "fuse.h"

/* One worker/session, no concurrent calls. Keep bounded wire scratch off the
 * kernel task stack, including when a caller owns a full directory batch. */
static struct {
  struct fuse_in_header header;
  uint8_t payload[sizeof(struct fuse_write_in) + VIRTIO_FS_WRITE_MAX];
} request;
static struct {
  struct fuse_out_header header;
  uint8_t payload[VIRTIO_FS_READ_MAX];
} response;

static struct {
  struct fuse_write_in header;
  uint8_t data[VIRTIO_FS_WRITE_MAX];
} write_request;

_Static_assert(sizeof(request) <= VIRTQUEUE_REQUEST_BYTES, "FUSE request DMA capacity");

static bool mutation_request(uint32_t opcode)
{
  return opcode == FUSE_WRITE || opcode == FUSE_SETATTR || opcode == FUSE_CREATE;
}

static enum virtio_fs_result fail_session(struct virtio_fs_session *session,
    enum virtio_fs_result result, const char *reason)
{
  session->ready = false;
  virtio_fs_pci_stop(reason);
  return result;
}

static enum virtio_fs_result host_error(int32_t error)
{
  switch (-error) {
  case FUSE_ENOENT: return VIRTIO_FS_NOT_FOUND;
  case FUSE_EPERM:
  case FUSE_EACCES: return VIRTIO_FS_DENIED;
  case FUSE_ENOTDIR:
  case FUSE_EISDIR: return VIRTIO_FS_WRONG_TYPE;
  case FUSE_ENOSYS:
  case FUSE_EOPNOTSUPP:
  case FUSE_ELOOP: return VIRTIO_FS_UNSUPPORTED;
  case FUSE_EINVAL: return VIRTIO_FS_INVALID;
  case FUSE_ENAMETOOLONG:
  case FUSE_EMFILE:
  case FUSE_EOVERFLOW: return VIRTIO_FS_LIMIT;
  case FUSE_ENOMEM: return VIRTIO_FS_NO_MEMORY;
  case FUSE_ENOSPC: return VIRTIO_FS_NO_SPACE;
  case FUSE_EDQUOT: return VIRTIO_FS_QUOTA;
  case FUSE_EFBIG: return VIRTIO_FS_FILE_TOO_LARGE;
  case FUSE_EROFS: return VIRTIO_FS_READ_ONLY;
  case FUSE_EEXIST: return VIRTIO_FS_ALREADY_EXISTS;
  case FUSE_ENOTEMPTY: return VIRTIO_FS_NOT_EMPTY;
  case FUSE_EBUSY: return VIRTIO_FS_BUSY;
  case FUSE_ETIMEDOUT: return VIRTIO_FS_TIMED_OUT;
  case FUSE_ESTALE: return VIRTIO_FS_UNAVAILABLE;
  default: return VIRTIO_FS_IO;
  }
}

static enum virtio_fs_result exchange(struct virtio_fs_session *session, uint32_t opcode,
    uint64_t node_id, const void *payload, size_t payload_bytes,
    size_t reply_capacity, size_t *reply_bytes)
{
  *reply_bytes = 0;
  if (!session->ready && opcode != FUSE_INIT) {
    return VIRTIO_FS_UNAVAILABLE;
  }
  KASSERT(payload_bytes <= sizeof(request.payload) && reply_capacity <= sizeof(response.payload));
  if (session->next_unique == UINT64_MAX) {
    return fail_session(session, VIRTIO_FS_LIMIT, "FUSE request identity exhausted");
  }
  uint64_t unique = session->next_unique++;
  request.header = (struct fuse_in_header){
    .length = sizeof(request.header) + payload_bytes,
    .opcode = opcode, .unique = unique, .node_id = node_id,
  };
  memcpy(request.payload, payload, payload_bytes);

  if (opcode == FUSE_FORGET) {
    return virtio_fs_pci_forget(&request, request.header.length);
  }
  size_t length;
  bool submitted;
  enum virtio_fs_result result = virtio_fs_pci_request(&request, request.header.length,
      &response, sizeof(response.header) + reply_capacity, &length, &submitted);
  if (result != VIRTIO_FS_OK) {
    if (submitted && mutation_request(opcode)) {
      return VIRTIO_FS_OUTCOME_UNKNOWN;
    }
    return result;
  }
  if (length < sizeof(response.header) || response.header.length != length ||
      response.header.unique != unique || response.header.error > 0 ||
      response.header.error < -FUSE_ERRNO_MAX) {
    return fail_session(session, mutation_request(opcode) ?
        VIRTIO_FS_OUTCOME_UNKNOWN : VIRTIO_FS_PROTOCOL, "invalid FUSE reply header");
  }
  if (response.header.error) {
    if (length != sizeof(response.header)) {
      return fail_session(session, mutation_request(opcode) ?
        VIRTIO_FS_OUTCOME_UNKNOWN : VIRTIO_FS_PROTOCOL, "FUSE error reply has a payload");
    }
    return host_error(response.header.error);
  }
  *reply_bytes = length - sizeof(response.header);
  return VIRTIO_FS_OK;
}

static enum virtio_fs_result fixed_reply(struct virtio_fs_session *session, uint32_t opcode,
    uint64_t node_id, const void *payload, size_t payload_bytes, void *reply, size_t expected)
{
  size_t bytes;
  enum virtio_fs_result result = exchange(session, opcode, node_id,
      payload, payload_bytes, expected, &bytes);
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  if (bytes != expected) {
    return fail_session(session, mutation_request(opcode) ?
        VIRTIO_FS_OUTCOME_UNKNOWN : VIRTIO_FS_PROTOCOL, "incorrect FUSE reply size");
  }
  if (expected) {
    memcpy(reply, response.payload, expected);
  }
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_session_init(struct virtio_fs_session *session)
{
  KASSERT(!session->next_unique && !session->ready);
  session->next_unique = 1;
  struct fuse_init_in init = {.major = FUSE_VERSION_MAJOR, .minor = FUSE_VERSION_MINOR};
  struct fuse_init_out reply;
  enum virtio_fs_result result = fixed_reply(session, FUSE_INIT, 0,
      &init, sizeof(init), &reply, sizeof(reply));
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  if (reply.major != FUSE_VERSION_MAJOR || reply.minor != FUSE_VERSION_MINOR ||
      reply.flags || reply.flags2 || reply.max_readahead) {
    return fail_session(session, VIRTIO_FS_UNSUPPORTED, "unsupported FUSE INIT version or features");
  }
  session->major = reply.major;
  session->minor = reply.minor;
  session->max_write = reply.max_write;
  session->time_granularity = reply.time_granularity;
  session->ready = true;
  return VIRTIO_FS_OK;
}

static enum virtio_fs_kind mode_kind(uint32_t mode)
{
  switch (mode & FUSE_MODE_TYPE_MASK) {
  case FUSE_MODE_FILE: return VIRTIO_FS_FILE;
  case FUSE_MODE_DIRECTORY: return VIRTIO_FS_DIRECTORY;
  case FUSE_MODE_SYMLINK: return VIRTIO_FS_SYMLINK;
  default: return VIRTIO_FS_OTHER;
  }
}

static enum virtio_fs_kind dirent_kind(uint32_t type)
{
  switch (type) {
  case FUSE_DIRENT_UNKNOWN: return VIRTIO_FS_UNKNOWN;
  case FUSE_DIRENT_FILE: return VIRTIO_FS_FILE;
  case FUSE_DIRENT_DIRECTORY: return VIRTIO_FS_DIRECTORY;
  case FUSE_DIRENT_SYMLINK: return VIRTIO_FS_SYMLINK;
  default: return VIRTIO_FS_OTHER;
  }
}

static bool dot_name(const char *name, size_t length)
{
  return (length == 1 && name[0] == '.') ||
    (length == 2 && name[0] == '.' && name[1] == '.');
}

static bool ordinary_name(const char *name, size_t length)
{
  if (!length) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    if (!name[i] || name[i] == '/') {
      return false;
    }
  }
  return true;
}

enum virtio_fs_result virtio_fs_root(struct virtio_fs_session *session, struct virtio_fs_node *node)
{
  KASSERT(!node->references);
  if (!session->ready) {
    return VIRTIO_FS_UNAVAILABLE;
  }
  *node = (struct virtio_fs_node){
    .session = session, .id = FUSE_ROOT_ID, .references = 1, .kind = VIRTIO_FS_DIRECTORY,
  };
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_node_get(struct virtio_fs_node *node)
{
  KASSERT(node->references);
  if (node->references == SIZE_MAX) {
    return VIRTIO_FS_LIMIT;
  }
  ++node->references;
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_node_put(struct virtio_fs_node *node)
{
  KASSERT(node->references);
  if (--node->references) {
    return VIRTIO_FS_OK;
  }
  enum virtio_fs_result result = VIRTIO_FS_OK;
  if (node->lookup_owned) {
    struct virtio_fs_session *session = node->session;
    KASSERT(session->lookup_refs);
    if (session->ready) {
      uint64_t count = 1;
      size_t ignored;
      result = exchange(session, FUSE_FORGET, node->id, &count, sizeof(count), 0, &ignored);
      if (result != VIRTIO_FS_OK) {
        fail_session(session, result, "cannot return FUSE lookup reference");
      }
    } else {
      result = VIRTIO_FS_UNAVAILABLE;
    }
    --session->lookup_refs;
  }
  *node = (struct virtio_fs_node){0};
  return result;
}

enum virtio_fs_result virtio_fs_lookup(struct virtio_fs_node *parent,
    const char *name, size_t length, struct virtio_fs_node *node)
{
  KASSERT(parent->references && !node->references);
  if (length > VIRTIO_FS_NAME_MAX) {
    return VIRTIO_FS_LIMIT;
  }
  if (!ordinary_name(name, length) || dot_name(name, length)) {
    return VIRTIO_FS_INVALID;
  }
  if (parent->kind != VIRTIO_FS_DIRECTORY) {
    return VIRTIO_FS_WRONG_TYPE;
  }
  struct virtio_fs_session *session = parent->session;
  if (session->lookup_refs == UINT64_MAX) {
    return VIRTIO_FS_LIMIT;
  }
  char component[VIRTIO_FS_NAME_MAX + 1];
  memcpy(component, name, length);
  component[length] = 0;
  struct fuse_entry_out entry;
  enum virtio_fs_result result = fixed_reply(session, FUSE_LOOKUP, parent->id,
      component, length + 1, &entry, sizeof(entry));
  if (result != VIRTIO_FS_OK || !entry.node_id) {
    return result == VIRTIO_FS_OK ? VIRTIO_FS_NOT_FOUND : result;
  }
  ++session->lookup_refs;
  *node = (struct virtio_fs_node){
    .session = session, .id = entry.node_id, .generation = entry.generation,
    .kind = mode_kind(entry.attr.mode), .references = 1, .lookup_owned = true,
  };
  if (node->kind != VIRTIO_FS_FILE && node->kind != VIRTIO_FS_DIRECTORY) {
    result = virtio_fs_node_put(node);
    return result == VIRTIO_FS_OK ? VIRTIO_FS_UNSUPPORTED : result;
  }
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_getattr(struct virtio_fs_node *node,
    struct virtio_fs_attributes *attributes)
{
  KASSERT(node->references);
  struct fuse_getattr_in query = {0};
  struct fuse_attr_out reply;
  enum virtio_fs_result result = fixed_reply(node->session, FUSE_GETATTR, node->id,
      &query, sizeof(query), &reply, sizeof(reply));
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  if (mode_kind(reply.attr.mode) != node->kind) {
    return fail_session(node->session, VIRTIO_FS_PROTOCOL, "FUSE node changed type while retained");
  }
  *attributes = (struct virtio_fs_attributes){.kind = node->kind, .size = reply.attr.size};
  return VIRTIO_FS_OK;
}

static enum virtio_fs_result check_open_flags(struct virtio_fs_open *opened, uint32_t flags)
{
  /* Cache hints do not oblige us to cache. Stream/nonseekable and unknown
   * semantics cannot satisfy explicit-offset I/O; release before rejecting. */
  unsigned supported = FUSE_OPEN_DIRECT_IO | FUSE_OPEN_KEEP_CACHE |
    FUSE_OPEN_CACHE_DIR | FUSE_OPEN_NOFLUSH;
  if (flags & ~supported) {
    enum virtio_fs_result result = virtio_fs_close(opened);
    return result == VIRTIO_FS_OK ? VIRTIO_FS_UNSUPPORTED : result;
  }
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_open(struct virtio_fs_node *node,
    enum virtio_fs_access access, struct virtio_fs_open *opened)
{
  KASSERT(node->references && !opened->node);
  if (node->kind != VIRTIO_FS_FILE && node->kind != VIRTIO_FS_DIRECTORY) {
    return VIRTIO_FS_UNSUPPORTED;
  }
  struct virtio_fs_session *session = node->session;
  if (session->open_handles == UINT64_MAX || node->references == SIZE_MAX) {
    return VIRTIO_FS_LIMIT;
  }
  if ((access != VIRTIO_FS_ACCESS_READ && access != VIRTIO_FS_ACCESS_WRITE) ||
      (node->kind == VIRTIO_FS_DIRECTORY && access != VIRTIO_FS_ACCESS_READ)) {
    return VIRTIO_FS_INVALID;
  }
  struct fuse_open_in query = {
    .flags = access == VIRTIO_FS_ACCESS_WRITE ? FUSE_OPEN_WRITE_ONLY : FUSE_OPEN_READ_ONLY,
  };
  struct fuse_open_out reply;
  unsigned opcode = node->kind == VIRTIO_FS_DIRECTORY ? FUSE_OPENDIR : FUSE_OPEN;
  enum virtio_fs_result result = fixed_reply(session, opcode, node->id,
      &query, sizeof(query), &reply, sizeof(reply));
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  KASSERT(virtio_fs_node_get(node) == VIRTIO_FS_OK);
  ++session->open_handles;
  *opened = (struct virtio_fs_open){.node = node, .handle = reply.handle, .flags = query.flags};

  return check_open_flags(opened, reply.flags);
}

enum virtio_fs_result virtio_fs_create(struct virtio_fs_node *parent,
    const char *name, size_t length, struct virtio_fs_node *node, struct virtio_fs_open *opened)
{
  KASSERT(parent->references && !node->references && !opened->node);
  if (length > VIRTIO_FS_NAME_MAX) {
    return VIRTIO_FS_LIMIT;
  }
  if (!ordinary_name(name, length) || dot_name(name, length)) {
    return VIRTIO_FS_INVALID;
  }
  if (parent->kind != VIRTIO_FS_DIRECTORY) {
    return VIRTIO_FS_WRONG_TYPE;
  }
  struct virtio_fs_session *session = parent->session;
  if (session->lookup_refs == UINT64_MAX || session->open_handles == UINT64_MAX) {
    return VIRTIO_FS_LIMIT;
  }

  /* Fixed prototype policy: service identity, 0644, no executable bits or
   * guest umask. O_EXCL makes creation atomic with respect to an existing name. */
  struct {
    struct fuse_create_in header;
    char name[VIRTIO_FS_NAME_MAX + 1];
  } query = {.header = {
    .flags = FUSE_OPEN_WRITE_ONLY | FUSE_OPEN_CREATE | FUSE_OPEN_EXCLUSIVE,
    .mode = FUSE_MODE_FILE | 0644u,
  }};
  memcpy(query.name, name, length);
  query.name[length] = 0;
  struct fuse_create_out reply;
  enum virtio_fs_result result = fixed_reply(session, FUSE_CREATE, parent->id,
      &query, sizeof(query.header) + length + 1, &reply, sizeof(reply));
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  if (!reply.entry.node_id || reply.entry.node_id == FUSE_ROOT_ID ||
      mode_kind(reply.entry.attr.mode) != VIRTIO_FS_FILE) {
    return fail_session(session, VIRTIO_FS_OUTCOME_UNKNOWN, "invalid FUSE created file");
  }
  ++session->lookup_refs;
  ++session->open_handles;
  *node = (struct virtio_fs_node){
    .session = session, .id = reply.entry.node_id, .generation = reply.entry.generation,
    .kind = VIRTIO_FS_FILE, .references = 2, .lookup_owned = true,
  };
  *opened = (struct virtio_fs_open){
    .node = node, .handle = reply.opened.handle, .flags = query.header.flags,
  };
  /* The two references belong to the native wrapper and the returned open.
   * A rejected open is released; the caller still retires the lookup. */
  return check_open_flags(opened, reply.opened.flags);
}

enum virtio_fs_result virtio_fs_close(struct virtio_fs_open *opened)
{
  struct virtio_fs_node *node = opened->node;
  KASSERT(node && node->references);
  struct virtio_fs_session *session = node->session;
  enum virtio_fs_result result = VIRTIO_FS_UNAVAILABLE;
  if (session->ready) {
    struct fuse_release_in query = {.handle = opened->handle, .flags = opened->flags};
    unsigned opcode = node->kind == VIRTIO_FS_DIRECTORY ? FUSE_RELEASEDIR : FUSE_RELEASE;
    result = fixed_reply(session, opcode, node->id, &query, sizeof(query), NULL, 0);
    if (result != VIRTIO_FS_OK) {
      fail_session(session, result, "cannot release FUSE open handle");
    }
  }
  KASSERT(session->open_handles);
  --session->open_handles;
  *opened = (struct virtio_fs_open){0};
  enum virtio_fs_result released = virtio_fs_node_put(node);
  return result == VIRTIO_FS_OK ? released : result;
}

enum virtio_fs_result virtio_fs_write(struct virtio_fs_open *opened, uint64_t offset,
    const void *buffer, size_t size, size_t *written)
{
  struct virtio_fs_node *node = opened->node;
  KASSERT(node && node->references && size);
  KASSERT((opened->flags & FUSE_OPEN_ACCESS_MASK) == FUSE_OPEN_WRITE_ONLY);
  *written = 0;
  if (!node->session->ready) {
    return VIRTIO_FS_UNAVAILABLE;
  }
  if (!node->session->max_write) {
    return VIRTIO_FS_UNSUPPORTED;
  }
  size_t amount = size < VIRTIO_FS_WRITE_MAX ? size : VIRTIO_FS_WRITE_MAX;
  if (amount > node->session->max_write) {
    amount = node->session->max_write;
  }
  if (offset > INT64_MAX || amount > INT64_MAX - offset) {
    return VIRTIO_FS_FILE_TOO_LARGE;
  }
  write_request.header = (struct fuse_write_in){
    .handle = opened->handle, .offset = offset, .size = amount, .flags = opened->flags,
  };
  memcpy(write_request.data, buffer, amount);
  struct fuse_write_out reply;
  enum virtio_fs_result result = fixed_reply(node->session, FUSE_WRITE, node->id,
      &write_request, sizeof(write_request.header) + amount, &reply, sizeof(reply));
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  if (!reply.size || reply.size > amount) {
    return fail_session(node->session, VIRTIO_FS_OUTCOME_UNKNOWN, "invalid FUSE write count");
  }
  *written = reply.size;
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_resize(struct virtio_fs_open *opened, uint64_t size)
{
  struct virtio_fs_node *node = opened->node;
  KASSERT(node && node->references);
  KASSERT((opened->flags & FUSE_OPEN_ACCESS_MASK) == FUSE_OPEN_WRITE_ONLY);
  if (size > INT64_MAX) {
    return VIRTIO_FS_FILE_TOO_LARGE;
  }
  struct fuse_setattr_in query = {
    .valid = FUSE_ATTR_SIZE | FUSE_ATTR_HANDLE, .handle = opened->handle, .size = size,
  };
  struct fuse_attr_out reply;
  enum virtio_fs_result result = fixed_reply(node->session, FUSE_SETATTR, node->id,
      &query, sizeof(query), &reply, sizeof(reply));
  if (result == VIRTIO_FS_OK && mode_kind(reply.attr.mode) != VIRTIO_FS_FILE) {
    return fail_session(node->session, VIRTIO_FS_OUTCOME_UNKNOWN, "invalid FUSE resize type");
  }
  /* An external writer can change size before the returned attributes are
   * captured. Acknowledged SETATTR suffices; equality would invent a transaction. */
  return result;
}

enum virtio_fs_result virtio_fs_read(struct virtio_fs_open *opened, uint64_t offset,
    void *buffer, size_t capacity, size_t *read)
{
  struct virtio_fs_node *node = opened->node;
  KASSERT(node && node->references);
  *read = 0;
  if (!node->session->ready) {
    return VIRTIO_FS_UNAVAILABLE;
  }
  if (node->kind != VIRTIO_FS_FILE) {
    return VIRTIO_FS_WRONG_TYPE;
  }
  size_t amount = capacity < VIRTIO_FS_READ_MAX ? capacity : VIRTIO_FS_READ_MAX;
  if (offset > INT64_MAX || amount > INT64_MAX - offset) {
    return VIRTIO_FS_LIMIT;
  }
  if (!amount) {
    return VIRTIO_FS_OK;
  }
  struct fuse_read_in query = {.handle = opened->handle, .offset = offset, .size = amount};
  size_t bytes;
  enum virtio_fs_result result = exchange(node->session, FUSE_READ, node->id,
      &query, sizeof(query), amount, &bytes);
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  if (bytes > amount) {
    return fail_session(node->session, VIRTIO_FS_PROTOCOL, "FUSE read exceeds requested length");
  }
  memcpy(buffer, response.payload, bytes);
  *read = bytes;
  return VIRTIO_FS_OK;
}

static enum virtio_fs_result check_directory(struct virtio_fs_session *session,
    uint64_t cookie, size_t bytes)
{
  size_t position = 0;
  while (position < bytes) {
    if (bytes - position < sizeof(struct fuse_dirent)) {
      return fail_session(session, VIRTIO_FS_PROTOCOL, "truncated FUSE directory entry");
    }
    struct fuse_dirent entry;
    memcpy(&entry, response.payload + position, sizeof(entry));
    if (entry.name_length > bytes - position - sizeof(entry)) {
      return fail_session(session, VIRTIO_FS_PROTOCOL, "FUSE directory name outside reply");
    }
    size_t extent = (sizeof(entry) + entry.name_length + FUSE_DIRENT_ALIGNMENT - 1) &
      ~(size_t)(FUSE_DIRENT_ALIGNMENT - 1);
    const char *name = (const char *)response.payload + position + sizeof(entry);
    if (extent > bytes - position || !ordinary_name(name, entry.name_length) ||
        !entry.next_cookie || entry.next_cookie == cookie) {
      return fail_session(session, VIRTIO_FS_PROTOCOL, "invalid FUSE directory entry or cookie");
    }
    if (entry.name_length > VIRTIO_FS_NAME_MAX) {
      return VIRTIO_FS_LIMIT;
    }
    cookie = entry.next_cookie;
    position += extent;
  }
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_readdir(struct virtio_fs_open *opened, uint64_t cookie,
    struct virtio_fs_directory_batch *batch)
{
  struct virtio_fs_node *node = opened->node;
  KASSERT(node && node->references);
  batch->length = batch->position = 0;
  batch->end = false;
  batch->next_cookie = cookie;
  if (node->kind != VIRTIO_FS_DIRECTORY) {
    return VIRTIO_FS_WRONG_TYPE;
  }
  struct fuse_read_in query = {
    .handle = opened->handle, .offset = cookie, .size = VIRTIO_FS_READ_MAX,
  };
  size_t bytes;
  enum virtio_fs_result result = exchange(node->session, FUSE_READDIR, node->id,
      &query, sizeof(query), VIRTIO_FS_READ_MAX, &bytes);
  if (result == VIRTIO_FS_OK) {
    result = check_directory(node->session, cookie, bytes);
  }
  if (result != VIRTIO_FS_OK) {
    return result;
  }
  memcpy(batch->bytes, response.payload, bytes);
  batch->length = bytes;
  batch->end = !bytes;
  return VIRTIO_FS_OK;
}

enum virtio_fs_result virtio_fs_directory_next(struct virtio_fs_directory_batch *batch,
    struct virtio_fs_dirent *entry)
{
  while (batch->position < batch->length) {
    struct fuse_dirent wire;
    memcpy(&wire, batch->bytes + batch->position, sizeof(wire));
    const char *name = (const char *)batch->bytes + batch->position + sizeof(wire);
    size_t extent = (sizeof(wire) + wire.name_length + FUSE_DIRENT_ALIGNMENT - 1) &
      ~(size_t)(FUSE_DIRENT_ALIGNMENT - 1);
    batch->position += extent;
    batch->next_cookie = wire.next_cookie;
    if (dot_name(name, wire.name_length)) {
      continue;
    }
    entry->kind = dirent_kind(wire.type);
    entry->next_cookie = wire.next_cookie;
    entry->name_length = wire.name_length;
    memcpy(entry->name, name, wire.name_length);
    entry->name[wire.name_length] = 0;
    return VIRTIO_FS_OK;
  }
  return VIRTIO_FS_END;
}

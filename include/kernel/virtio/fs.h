#ifndef KERNEL_VIRTIO_FS_H
#define KERNEL_VIRTIO_FS_H

#include <stddef.h>
#include <stdint.h>

#define VIRTIO_FS_NAME_MAX 255
#define VIRTIO_FS_READ_MAX 4096

enum virtio_fs_result {
  VIRTIO_FS_OK,
  VIRTIO_FS_END,
  VIRTIO_FS_NOT_FOUND,
  VIRTIO_FS_DENIED,
  VIRTIO_FS_WRONG_TYPE,
  VIRTIO_FS_UNSUPPORTED,
  VIRTIO_FS_INVALID,
  VIRTIO_FS_LIMIT,
  VIRTIO_FS_NO_MEMORY,
  VIRTIO_FS_NO_SPACE,
  VIRTIO_FS_QUOTA,
  VIRTIO_FS_FILE_TOO_LARGE,
  VIRTIO_FS_READ_ONLY,
  VIRTIO_FS_ALREADY_EXISTS,
  VIRTIO_FS_NOT_EMPTY,
  VIRTIO_FS_BUSY,
  VIRTIO_FS_IO,
  VIRTIO_FS_TIMED_OUT,
  VIRTIO_FS_UNAVAILABLE,
  VIRTIO_FS_PROTOCOL,
};

enum virtio_fs_kind {
  VIRTIO_FS_UNKNOWN,
  VIRTIO_FS_FILE,
  VIRTIO_FS_DIRECTORY,
  VIRTIO_FS_SYMLINK,
  VIRTIO_FS_OTHER,
};

struct virtio_fs_session {
  uint32_t major, minor, max_write, time_granularity;
  uint64_t next_unique, lookup_refs, open_handles;
  bool ready;
};

struct virtio_fs_attributes {
  enum virtio_fs_kind kind;
  uint64_t size;
};

/* Caller-owned stable records, initially zeroed. Each successful lookup owns
 * one host lookup reference, regardless of matching IDs in another record.
 * Local retains do not send LOOKUP. Root has no acquired lookup reference.
 * The session lives for the boot; no reconnect or session replacement. */
struct virtio_fs_node {
  struct virtio_fs_session *session;
  uint64_t id, generation;
  size_t references;
  enum virtio_fs_kind kind;
  bool lookup_owned;
};

struct virtio_fs_open {
  struct virtio_fs_node *node; /* Retained; node storage must outlive this open. */
  uint64_t handle;
};

struct virtio_fs_dirent {
  enum virtio_fs_kind kind;
  uint64_t next_cookie;
  size_t name_length;
  char name[VIRTIO_FS_NAME_MAX + 1];
};

/* A transient, checked READDIR batch, not a directory snapshot or cache. Its
 * cookies belong to the still-open directory that produced it. Consume with
 * directory_next; END there means this batch is exhausted. batch.end means
 * the server returned EOF. next_cookie includes skipped dot entries. */
struct virtio_fs_directory_batch {
  uint64_t next_cookie;
  size_t length, position;
  bool end;
  uint8_t bytes[VIRTIO_FS_READ_MAX];
};

/* All operations run on the sole BSP transport worker, IF=1, without locks.
 * No allocation, user pointers, concurrent calls or private AP stack access.
 * The native hostfs backend hands off caller-owned task records to this worker.
 * Ordinary host errors leave the session usable. Transport/protocol failures
 * stop it; outstanding records still require close/put for local retirement. */
enum virtio_fs_result virtio_fs_session_init(struct virtio_fs_session *session);
enum virtio_fs_result virtio_fs_root(struct virtio_fs_session *session, struct virtio_fs_node *node);
enum virtio_fs_result virtio_fs_lookup(struct virtio_fs_node *parent,
    const char *name, size_t length, struct virtio_fs_node *node);
enum virtio_fs_result virtio_fs_getattr(struct virtio_fs_node *node,
    struct virtio_fs_attributes *attributes);
enum virtio_fs_result virtio_fs_node_get(struct virtio_fs_node *node);
/* Final put sends FORGET after all open handles have released their retains.
 * Always retires the final local record, including on a failed session. */
enum virtio_fs_result virtio_fs_node_put(struct virtio_fs_node *node);
enum virtio_fs_result virtio_fs_open(struct virtio_fs_node *node, struct virtio_fs_open *opened);
/* Close always consumes the open, releasing its node retain even on failure.
 * Failed RELEASE/FORGET stops the session rather than losing host ownership. */
enum virtio_fs_result virtio_fs_close(struct virtio_fs_open *opened);
/* Explicit byte offsets, short reads/EOF preserved, at most READ_MAX bytes. */
enum virtio_fs_result virtio_fs_read(struct virtio_fs_open *opened, uint64_t offset,
    void *buffer, size_t capacity, size_t *read);
/* Opaque cookie zero starts/restarts. No arithmetic or ordering of cookies.
 * Host mutation may affect results; no snapshot or reliable change detection. */
enum virtio_fs_result virtio_fs_readdir(struct virtio_fs_open *opened, uint64_t cookie,
    struct virtio_fs_directory_batch *batch);
enum virtio_fs_result virtio_fs_directory_next(struct virtio_fs_directory_batch *batch,
    struct virtio_fs_dirent *entry);

#endif

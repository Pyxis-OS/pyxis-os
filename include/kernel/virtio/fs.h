#ifndef KERNEL_VIRTIO_FS_H
#define KERNEL_VIRTIO_FS_H

#include <stddef.h>
#include <stdint.h>

#define VIRTIO_FS_NAME_MAX 255
#define VIRTIO_FS_READ_MAX 4096
#define VIRTIO_FS_WRITE_MAX 4096

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
  VIRTIO_FS_OUTCOME_UNKNOWN,
  VIRTIO_FS_LINK_NOT_FOLLOWED,
  VIRTIO_FS_NAME_TOO_LONG,
};

enum virtio_fs_kind {
  VIRTIO_FS_UNKNOWN,
  VIRTIO_FS_FILE,
  VIRTIO_FS_DIRECTORY,
  VIRTIO_FS_SYMLINK,
  VIRTIO_FS_OTHER,
};

/* Optional worker-owned aggregate for one native operation, including lazy
 * OPEN. No wire identities escape; zero unless a chain was published. */
struct virtio_fs_profile {
  uint64_t submissions, completions, failures;
  uint64_t completed_ns, completed_max_ns, failed_ns, failed_max_ns;
  bool saturated;
};

struct virtio_fs_session {
  uint32_t major, minor, max_write, time_granularity;
  uint64_t next_unique, lookup_refs, open_handles;
  bool ready;
  struct virtio_fs_profile *profile; /* Sole worker scopes this around native I/O. */
};

struct virtio_fs_attributes {
  enum virtio_fs_kind kind;
  uint64_t size;
  int64_t modified_seconds;
  uint32_t modified_nanoseconds;
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

enum virtio_fs_access {
  VIRTIO_FS_ACCESS_READ,
  VIRTIO_FS_ACCESS_WRITE,
};

struct virtio_fs_open {
  struct virtio_fs_node *node; /* Retained; node storage must outlive this open. */
  uint64_t handle;
  uint32_t flags; /* Open flags are retained for WRITE and RELEASE. */
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
enum virtio_fs_result virtio_fs_open(struct virtio_fs_node *node,
    enum virtio_fs_access access, struct virtio_fs_open *opened);
/* Exclusive regular-file creation, fixed 0644 mode under the service identity.
 * Success owns both a lookup reference and a write-only open. Failure can leave
 * a created host name; never try to undo it by removing a potentially replaced
 * entry. Even failure can leave node ownership for the caller to put. */
enum virtio_fs_result virtio_fs_create(struct virtio_fs_node *parent,
    const char *name, size_t length, struct virtio_fs_node *node, struct virtio_fs_open *opened);
/* MKDIR uses fixed 0755 mode under the service identity. Success owns one
 * lookup reference and no open handle. A failed mutation may have taken effect. */
enum virtio_fs_result virtio_fs_mkdir(struct virtio_fs_node *parent,
    const char *name, size_t length, struct virtio_fs_node *node);
/* Names are checked by the host at use time. The caller may preflight types,
 * but another host actor can replace a name before REMOVE or RENAME2 executes.
 * RENAME2 uses atomic replace or RENAME_NOREPLACE; it has no older-op fallback. */
enum virtio_fs_result virtio_fs_remove(struct virtio_fs_node *parent,
    const char *name, size_t length, bool directory);
enum virtio_fs_result virtio_fs_rename(struct virtio_fs_node *source,
    const char *source_name, size_t source_length, struct virtio_fs_node *destination,
    const char *destination_name, size_t destination_length, bool replace);
/* Close always consumes the open, releasing its node retain even on failure.
 * Failed RELEASE/FORGET stops the session rather than losing host ownership. */
enum virtio_fs_result virtio_fs_close(struct virtio_fs_open *opened);
/* Full synchronization of the opened file/directory, not a recursive/global
 * flush. A published request without a trustworthy completion returns UNKNOWN.
 * Keep the open and node alive on both success and failure. */
enum virtio_fs_result virtio_fs_sync(struct virtio_fs_open *opened);
/* Explicit byte offsets, short reads/EOF preserved, at most READ_MAX bytes. */
enum virtio_fs_result virtio_fs_read(struct virtio_fs_open *opened, uint64_t offset,
    void *buffer, size_t capacity, size_t *read);
/* One bounded write; size must be nonzero. No retry on any failure. Confirmed
 * progress may be short; UNKNOWN means published but no trustworthy completion. */
enum virtio_fs_result virtio_fs_write(struct virtio_fs_open *opened, uint64_t offset,
    const void *buffer, size_t size, size_t *written);
enum virtio_fs_result virtio_fs_resize(struct virtio_fs_open *opened, uint64_t size);
/* Opaque cookie zero starts/restarts. No arithmetic or ordering of cookies.
 * Host mutation may affect results; no snapshot or reliable change detection. */
enum virtio_fs_result virtio_fs_readdir(struct virtio_fs_open *opened, uint64_t cookie,
    struct virtio_fs_directory_batch *batch);
enum virtio_fs_result virtio_fs_directory_next(struct virtio_fs_directory_batch *batch,
    struct virtio_fs_dirent *entry);

#endif

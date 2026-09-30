#ifndef ABI_DIRECTORY_H
#define ABI_DIRECTORY_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

#define DIRECTORY_RIGHT_LOOKUP (UINT64_C(1) << 0)
#define DIRECTORY_RIGHT_ENUMERATE (UINT64_C(1) << 1)
#define DIRECTORY_RIGHT_READ_FILES (UINT64_C(1) << 2)
#define DIRECTORY_RIGHT_CREATE (UINT64_C(1) << 3)
#define DIRECTORY_RIGHT_WRITE_FILES (UINT64_C(1) << 4)
#define DIRECTORY_RIGHT_REMOVE (UINT64_C(1) << 5)
#define DIRECTORY_RIGHT_FILESYSTEM_INFO (UINT64_C(1) << 6)
#define DIRECTORY_CONTENT_RIGHTS (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | \
                          DIRECTORY_RIGHT_READ_FILES | DIRECTORY_RIGHT_CREATE | \
                          DIRECTORY_RIGHT_WRITE_FILES | DIRECTORY_RIGHT_REMOVE)
#define DIRECTORY_RIGHTS (DIRECTORY_CONTENT_RIGHTS | DIRECTORY_RIGHT_FILESYSTEM_INFO)

#define DIRECTORY_LOOKUP UINT64_C(1)
#define DIRECTORY_ENUMERATE UINT64_C(2)
#define DIRECTORY_CREATE UINT64_C(3)
#define DIRECTORY_REMOVE UINT64_C(4)
#define DIRECTORY_RENAME UINT64_C(5)
#define DIRECTORY_SYNC UINT64_C(6)
#define DIRECTORY_FILESYSTEM_INFO UINT64_C(7)

#define DIRECTORY_RENAME_NO_REPLACE UINT64_C(0)
#define DIRECTORY_RENAME_REPLACE UINT64_C(1)

/* ANY is accepted only by REMOVE; lookup/create still require an exact kind. */
#define DIRECTORY_KIND_ANY UINT64_C(0)
#define DIRECTORY_KIND_FILE UINT64_C(1)
#define DIRECTORY_KIND_DIRECTORY UINT64_C(2)
/* Enumeration only: lookup/create still request FILE or DIRECTORY. */
#define DIRECTORY_KIND_UNKNOWN UINT64_C(3)
#define DIRECTORY_KIND_SYMLINK UINT64_C(4)
#define DIRECTORY_KIND_OTHER UINT64_C(5)

/* Opaque, scoped to the directory being enumerated. Start with both fields
 * zero. No shared enumeration position exists in a handle or object. */
struct directory_cursor {
  uint64_t generation;
  uint64_t position;
};

/* LOOKUP and exclusive CREATE request the same name, kind and child rights. */
struct directory_child_request {
  uint64_t name;
  uint64_t name_length; /* Bytes excluding NUL; one ordinary component. */
  uint64_t kind;
  uint64_t rights;
};

struct directory_enumerate_request {
  struct directory_cursor cursor;
  uint64_t name;
  uint64_t capacity;
};

/* Remove one name with the selected kind (or ANY). Directories must be empty.
 * Host type checks precede a name-based operation; external replacement may
 * change the affected object/type. Existing handles keep their objects.
 * No child rights or reply are needed. Failure need not mean no host change. */
struct directory_remove_request {
  uint64_t name;
  uint64_t name_length;
  uint64_t kind;
};

/* Call the source parent with REMOVE. Destination is a handle in the caller's
 * table and needs CREATE, plus REMOVE when replacing another file. Host REPLACE
 * always requires destination REMOVE, even if the name is absent. Regular files
 * only at type check; host names/types can change before the atomic operation.
 * Host NO_REPLACE includes the same name (ALREADY_EXISTS); RAM permits a no-op.
 * Success returns no reply. Failure need not mean no host change. Names are
 * counted single components. */
struct directory_rename_request {
  uint64_t source_name;
  uint64_t source_length;
  handle_t destination;
  uint64_t destination_name;
  uint64_t destination_length;
  uint64_t policy;
};

union directory_payload {
  struct directory_child_request lookup;
  struct directory_child_request create;
  struct directory_enumerate_request enumerate;
  struct directory_remove_request remove;
  struct directory_rename_request rename;
};

/* SYNC requires either CREATE or REMOVE. Send the complete zeroed message;
 * it ignores the body and returns no reply. Synchronizes this directory's
 * entries, not its children or the whole filesystem. RAM succeeds as a no-op;
 * host durability depends on the service/storage. OUTCOME_UNKNOWN means the
 * submitted synchronization has no trustworthy completion. */
struct directory_message {
  struct message_header header;
  union directory_payload body;
};

struct directory_child_reply {
  handle_t handle; /* New owned reference in the caller's table. */
};

/* These are successful enumeration outcomes, carried with CALL_OK. Ordinary
 * syscall errors still have no reply. Enumeration is a live view, not a
 * snapshot. DIRECTORY_CHANGED reports detected invalidation and requires an
 * explicit restart with the zero cursor; external changes may go undetected. */
#define DIRECTORY_ENTRY UINT64_C(1)
#define DIRECTORY_END UINT64_C(2)
#define DIRECTORY_BUFFER_TOO_SMALL UINT64_C(3)
#define DIRECTORY_CHANGED UINT64_C(4)

struct directory_enumerate_reply {
  uint64_t outcome;
  struct directory_cursor cursor;
  uint64_t kind;
  uint64_t name_size; /* Includes NUL. Zero for END/CHANGED. */
};

#define FILESYSTEM_TYPE_PYXIS UINT64_C(1)
#define FILESYSTEM_FLAG_READ_ONLY (UINT64_C(1) << 0)
#define FILESYSTEM_FLAG_GPT_DEGRADED (UINT64_C(1) << 1)
#define FILESYSTEM_FLAG_DEGRADED (UINT64_C(1) << 2)
#define FILESYSTEM_VOLUME_NAME_MAX 255u

/* FILESYSTEM_INFO uses a complete zeroed directory_message (body ignored).
 * Requires only FILESYSTEM_INFO on a native directory; other backends return
 * BAD_OPERATION. No traversal, new authority or whole-image check occurs.
 * All fields below are available on success. IDs are opaque bytes in filesystem
 * order; name is NUL-terminated with zero padding. No namespace binding is given.
 * Allocatable bytes are (pool blocks - 2) * 4096, excluding superblock slots but
 * INCLUDING shared metadata/reserves. This is shared pool capacity, never a
 * volume's writable allowance. Identity, generation and capacity describe the
 * retained selected state; degraded flags distinguish GPT and filesystem opening.
 * Used/free bytes, charged bytes, guarantees, quotas and percentages are NOT
 * available: this record has no such fields. Their absence never means zero.
 * Opening validates geometry/root envelopes, not global allocation accounting. */
struct directory_filesystem_info {
  uint64_t type;
  uint64_t flags;
  uint8_t pool_id[16];
  uint8_t volume_id[16];
  uint64_t generation;
  uint64_t pool_allocatable_bytes;
  char volume_name[FILESYSTEM_VOLUME_NAME_MAX + 1];
};

_Static_assert(sizeof(struct directory_filesystem_info) == 320, "filesystem info layout");

/* ENTRY copies a whole NUL-terminated name and advances the cursor. A short
 * buffer reports the required name_size without touching the name or advancing.
 * END is repeatable while the directory is unchanged; no name or handle is
 * returned. Order is unspecified; cursor fields are not numeric entry indices.
 * Name and reply destinations must not overlap. Enumeration grants no handles. */
_Static_assert(sizeof(struct directory_cursor) == 16, "directory cursor layout");
_Static_assert(sizeof(union directory_payload) == 48, "directory payload layout");
_Static_assert(offsetof(struct directory_message, body) == 16, "directory payload offset");
_Static_assert(sizeof(struct directory_message) == 64, "directory message layout");
_Static_assert(sizeof(struct directory_child_reply) == 8, "directory child reply layout");
_Static_assert(sizeof(struct directory_enumerate_reply) == 40, "directory enumeration reply layout");

#endif

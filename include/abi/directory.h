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
#define DIRECTORY_RIGHTS (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | \
                          DIRECTORY_RIGHT_READ_FILES | DIRECTORY_RIGHT_CREATE | \
                          DIRECTORY_RIGHT_WRITE_FILES | DIRECTORY_RIGHT_REMOVE)

#define DIRECTORY_LOOKUP UINT64_C(1)
#define DIRECTORY_ENUMERATE UINT64_C(2)
#define DIRECTORY_CREATE UINT64_C(3)
#define DIRECTORY_REMOVE UINT64_C(4)

/* ANY is accepted only by REMOVE; lookup/create still require an exact kind. */
#define DIRECTORY_KIND_ANY UINT64_C(0)
#define DIRECTORY_KIND_FILE UINT64_C(1)
#define DIRECTORY_KIND_DIRECTORY UINT64_C(2)

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

/* Remove one name, requiring the selected kind (or ANY). Directories must be
 * empty. Existing child handles survive; no child rights or reply are needed. */
struct directory_remove_request {
  uint64_t name;
  uint64_t name_length;
  uint64_t kind;
};

union directory_payload {
  struct directory_child_request lookup;
  struct directory_child_request create;
  struct directory_enumerate_request enumerate;
  struct directory_remove_request remove;
};

struct directory_message {
  struct message_header header;
  union directory_payload body;
};

struct directory_child_reply {
  handle_t handle; /* New owned reference in the caller's table. */
};

/* These are successful enumeration outcomes, carried with CALL_OK. Ordinary
 * syscall errors still have no reply. Mutation invalidates prior cursors;
 * DIRECTORY_CHANGED requires an explicit restart with the zero cursor. */
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

/* ENTRY copies a whole NUL-terminated name and advances the cursor. A short
 * buffer reports the required name_size without touching the name or advancing.
 * END is repeatable; no name or handle is returned. Order is unspecified.
 * Name and reply destinations must not overlap. Enumeration grants no handles. */
_Static_assert(sizeof(struct directory_cursor) == 16, "directory cursor layout");
_Static_assert(sizeof(union directory_payload) == 32, "directory payload layout");
_Static_assert(offsetof(struct directory_message, body) == 16, "directory payload offset");
_Static_assert(sizeof(struct directory_message) == 48, "directory message layout");
_Static_assert(sizeof(struct directory_child_reply) == 8, "directory child reply layout");
_Static_assert(sizeof(struct directory_enumerate_reply) == 40, "directory enumeration reply layout");

#endif

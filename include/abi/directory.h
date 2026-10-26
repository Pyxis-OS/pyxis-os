#ifndef ABI_DIRECTORY_H
#define ABI_DIRECTORY_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/file_info.h>
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
#define DIRECTORY_INFO UINT64_C(8)
#define DIRECTORY_LOOKUP_TYPED UINT64_C(9)
#define DIRECTORY_LINK UINT64_C(10)
#define DIRECTORY_REPOSITORY_VIEW UINT64_C(16)
#define DIRECTORY_REPOSITORY_VIEW_RIGHTS \
  (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_READ_FILES | DIRECTORY_RIGHT_CREATE | \
   DIRECTORY_RIGHT_WRITE_FILES | DIRECTORY_RIGHT_REMOVE)
#define DIRECTORY_LINK_SOURCE_RIGHTS \
  (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_READ_FILES | DIRECTORY_RIGHT_WRITE_FILES)

#define DIRECTORY_RENAME_NO_REPLACE UINT64_C(0)
#define DIRECTORY_RENAME_REPLACE UINT64_C(1)

/* ANY is accepted by REMOVE/RENAME; ordinary LOOKUP/CREATE require an exact kind. */
#define DIRECTORY_KIND_ANY UINT64_C(0)
#define DIRECTORY_KIND_FILE UINT64_C(1)
#define DIRECTORY_KIND_DIRECTORY UINT64_C(2)
/* Enumeration only: LOOKUP/CREATE still request FILE or DIRECTORY. */
#define DIRECTORY_KIND_UNKNOWN UINT64_C(3)
#define DIRECTORY_KIND_SYMLINK UINT64_C(4)
#define DIRECTORY_KIND_OTHER UINT64_C(5)
/* REMOVE additionally accepts exact SYMLINK on HOST only, unlinking the name
 * without following it. LOOKUP/CREATE still request FILE or DIRECTORY. */

/* Opaque, scoped to the directory being enumerated. Start with both fields
 * zero. No shared enumeration position exists in a handle or object. */
struct directory_cursor {
  uint64_t generation;
  uint64_t position;
};

/* LOOKUP and exclusive CREATE request the same name, kind and child rights.
 * Native and host directories refuse a name over 255 bytes with NAME_TOO_LONG;
 * RAM directories have no name limit. Lookup never follows a host symbolic
 * link: LINK_NOT_FOLLOWED. Other host special files are BAD_OPERATION. */
struct directory_child_request {
  uint64_t name;
  uint64_t name_length; /* Bytes excluding NUL; one ordinary component. */
  uint64_t kind;
  uint64_t rights;
};

/* Resolve once and return the actual FILE or DIRECTORY. Both masks must use
 * their protocol's known bits; only the selected kind's mask is checked against
 * parent authority. Symlinks and special files retain ordinary lookup refusals. */
struct directory_typed_lookup_request {
  uint64_t name;
  uint64_t name_length;
  uint64_t file_rights;
  uint64_t directory_rights;
};

struct directory_typed_lookup_reply {
  handle_t handle;
  uint64_t protocol; /* Kernel-attested protocol of the returned owned handle. */
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

/* Call the source parent with REMOVE. Destination needs CREATE, plus REMOVE
 * for REPLACE. RAM and writable npfs move FILE or DIRECTORY within one instance;
 * directory replacement requires an empty same-kind victim. kind is ANY or an
 * atomic source-kind requirement. Self/descendant directory moves are BAD_REQUEST.
 * Held objects/rights survive; existing cwd chains are not re-resolved.
 * Cross-store/mount and host/FAT directory moves are BAD_OPERATION; read-only
 * stores are READ_ONLY. No copy/delete fallback. Host files retain external
 * name/type races and same-name NO_REPLACE refusal. Success returns no reply;
 * IO/OUTCOME_UNKNOWN do not guarantee no change. Names are single components. */
struct directory_rename_request {
  uint64_t source_name;
  uint64_t source_length;
  handle_t destination;
  uint64_t destination_name;
  uint64_t destination_length;
  uint64_t policy;
  uint64_t kind;
};

/* Call the named source's parent with all LINK_SOURCE_RIGHTS. Destination needs
 * CREATE. Only regular files in one writable npfs volume are supported; other
 * backends refuse BAD_OPERATION. Destination creation is exclusive. Source
 * remains named, and both names identify the same file. No orphan-handle form.
 * Names are single components; success returns no reply and stages one atomic
 * namespace/count edit, without implying sync. First use durably enables the
 * pool feature before staging. Later failure may leave that feature enabled.
 * Held authority survives CLOSE; CUT before worker admission denies the call,
 * while an already-started operation completes under ordinary lineage rules. */
struct directory_link_request {
  uint64_t source_name;
  uint64_t source_length;
  handle_t destination;
  uint64_t destination_name;
  uint64_t destination_length;
};

union directory_payload {
  struct directory_child_request lookup;
  struct directory_typed_lookup_request lookup_typed;
  struct directory_child_request create;
  struct directory_enumerate_request enumerate;
  struct directory_remove_request remove;
  struct directory_rename_request rename;
  struct directory_link_request link;
};

/* SYNC requires either CREATE or REMOVE. Send the complete zeroed message;
 * it ignores the body and returns no reply. Synchronizes this directory's
 * entries, not its children or the whole filesystem. RAM succeeds as a no-op;
 * host durability depends on the service/storage. Native sync commits all
 * dirty data and metadata in the current pool, including other files.
 * OUTCOME_UNKNOWN means the
 * submitted synchronization has no trustworthy completion. */
struct directory_message {
  struct message_header header;
  union directory_payload body;
};

struct directory_child_reply {
  handle_t handle; /* New owned reference in the caller's table. */
};

/* REPOSITORY_VIEW takes a zeroed body and returns directory_child_reply. All
 * REPOSITORY_VIEW_RIGHTS are required; the new independent native npfs view
 * has exactly those rights. Only FILE packages.lua and packages.lua.new are
 * accessible, including completion CREATE/REMOVE. ANY removal/rename means
 * FILE. No enumeration, directory children or links. Rename requires two views
 * derived from the same retained directory, with both names in the view. INFO
 * and SYNC retain the underlying directory's identity and sync semantics.
 * Copying, attenuating or transferring the handle preserves the confinement. */

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

#define FILESYSTEM_TYPE_NPFS UINT64_C(1)
#define FILESYSTEM_TYPE_RAM UINT64_C(2)
#define FILESYSTEM_FLAG_READ_ONLY (UINT64_C(1) << 0)
#define FILESYSTEM_FLAG_GPT_DEGRADED (UINT64_C(1) << 1)
#define FILESYSTEM_FLAG_DEGRADED (UINT64_C(1) << 2)
#define FILESYSTEM_VOLUME_NAME_MAX 255u

/* FILESYSTEM_INFO uses a complete zeroed directory_message (body ignored).
 * Requires only FILESYSTEM_INFO on an npfs or RAM directory; other backends
 * return BAD_OPERATION. RAM returns its type and zero flags; all remaining
 * fields are inapplicable and zero, not capacity or identity measurements. No traversal, new authority or whole-image check occurs.
 * On NPFS, all fields below are available on success. IDs are opaque bytes in filesystem
 * order; name is NUL-terminated with zero padding. No namespace binding is given.
 * Allocatable bytes are (pool blocks - 2) * 4096, excluding the two header blocks
 * but INCLUDING shared metadata/reserves. This is shared pool capacity, never a
 * volume's writable allowance. Identity, generation and capacity describe the
 * mounted pool; generation is its selected journal sequence. Degraded flags
 * distinguish GPT and filesystem opening.
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
_Static_assert(sizeof(union directory_payload) == 56, "directory payload layout");
_Static_assert(offsetof(struct directory_message, body) == 16, "directory payload offset");
_Static_assert(sizeof(struct directory_message) == 72, "directory message layout");
_Static_assert(sizeof(struct directory_link_request) == 40, "directory link layout");
_Static_assert(sizeof(struct directory_child_reply) == 8, "directory child reply layout");
_Static_assert(sizeof(struct directory_typed_lookup_request) == 32, "typed directory lookup request layout");
_Static_assert(sizeof(struct directory_typed_lookup_reply) == 16, "typed directory lookup reply layout");
_Static_assert(sizeof(struct directory_enumerate_reply) == 40, "directory enumeration reply layout");

#endif

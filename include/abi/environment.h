#ifndef ABI_ENVIRONMENT_H
#define ABI_ENVIRONMENT_H

#include <abi/handle.h>
#include <abi/message.h>

#define ENVIRONMENT_NAME_MAX 63
#define ENVIRONMENT_RIGHT_READ (UINT64_C(1) << 0)
#define ENVIRONMENT_RIGHT_WRITE (UINT64_C(1) << 1)
#define ENVIRONMENT_RIGHTS (ENVIRONMENT_RIGHT_READ | ENVIRONMENT_RIGHT_WRITE)
#define ENVIRONMENT_LOOKUP UINT64_C(1)
#define ENVIRONMENT_SET UINT64_C(2)
#define ENVIRONMENT_UNSET UINT64_C(3)
#define ENVIRONMENT_INFO UINT64_C(4)
#define ENVIRONMENT_SNAPSHOT UINT64_C(5)
#define ENVIRONMENT_LOOKUP_FILE UINT64_C(6)
#define ENVIRONMENT_ENUMERATE UINT64_C(2)
#define ENVIRONMENT_CAP UINT64_C(1)
#define ENVIRONMENT_TEXT UINT64_C(2)
#define ENVIRONMENT_EXPORTED UINT64_C(1)
#define ENVIRONMENT_SET_NO_REPLACE UINT64_C(1)
#define ENVIRONMENT_SET_REQUIRE_ABSENT UINT64_C(2)
#define ENVIRONMENT_SNAPSHOT_INCLUDE_PRIVATE UINT64_C(1)
#define ENVIRONMENT_SNAPSHOT_TEXT_ONLY UINT64_C(2)

/* Names are exact ASCII letters, digits, '_', '+', '-' and '.', nonempty,
 * NUL terminated with zero padding. Text length excludes the final NUL; text
 * input contains no NUL, and lookup needs length + 1 bytes. Returned CAP handles
 * are owned local grants. Entry identity never repeats within a table. */
struct environment_lookup_message {
  struct message_header header;
  char name[ENVIRONMENT_NAME_MAX + 1];
  uint64_t text_address;
  uint64_t text_capacity;
};

/* LOOKUP_FILE uses the same request with zero text_address/text_capacity. CAP
 * returns its unchanged owned grant. TEXT returns an owned READ-only immutable
 * native FILE containing exactly text_length bytes, excluding the final NUL.
 * The reply keeps the captured cell's kind, identity and generation; TEXT has
 * FILE_RIGHT_READ and zero transport. No directory/provider authority is added.
 * Allocation/admission failure publishes neither a handle nor a partial reply. */

struct environment_enumerate_message {
  struct message_header header;
  uint64_t index;
  uint64_t text_address;
  uint64_t text_capacity;
};

struct environment_entry_reply {
  char name[ENVIRONMENT_NAME_MAX + 1];
  uint64_t kind;
  uint64_t flags;
  uint64_t identity;
  uint64_t generation;
  handle_t handle;
  uint64_t rights;
  uint64_t transport;
  uint64_t text_length;
};

/* One SET atomically applies the complete array. NO_REPLACE keeps an existing
 * entry unchanged. REQUIRE_ABSENT fails the complete mutation if any supplied
 * name exists. Flags are mutually exclusive. No-replace alone succeeds when
 * every supplied entry is already present.
 * CAP masks request exact subsets of the supplied handle's authority. */
struct environment_set_entry {
  char name[ENVIRONMENT_NAME_MAX + 1];
  uint64_t kind;
  uint64_t flags;
  uint64_t set_flags;
  handle_t handle;
  uint64_t rights;
  uint64_t transport;
  uint64_t text_address;
  uint64_t text_length;
};

struct environment_set_message {
  struct message_header header;
  uint64_t entries_address;
  uint64_t count;
};

/* identity == 0 removes any entry under the name. A nonzero identity removes
 * only that publication; absent names and mismatches are successful no-ops. */
struct environment_unset_entry {
  char name[ENVIRONMENT_NAME_MAX + 1];
  uint64_t identity;
};

struct environment_unset_message {
  struct message_header header;
  uint64_t entries_address;
  uint64_t count;
};

struct environment_info_reply {
  uint64_t generation;
  uint64_t count;
  uint64_t bytes;
};

struct environment_snapshot_message {
  struct message_header header;
  uint64_t flags;
};

/* Default capture is exported entries only. INCLUDE_PRIVATE selects the
 * caller's own private entries too; TEXT_ONLY excludes all logical grants.
 * Captures are immutable, readable owned handles and never mutate a parent.
 * Snapshot LOOKUP, LOOKUP_FILE and INFO also accept PROTOCOL_ENVIRONMENT,
 * allowing shared helpers without querying handle metadata first. ENUMERATE uses the
 * snapshot protocol; live environment SET/UNSET never reach a snapshot.
 * Slot wrappers retain identity; a snapshot does not freeze their targets. */
struct environment_snapshot_reply {
  handle_t handle;
  struct environment_info_reply info;
};

/* LOOKUP/ENUMERATE return the fixed entry reply also on BUFFER_TOO_SMALL.
 * No text byte or handle is published then; required capacity is text_length+1.
 * INFO and SNAPSHOT generation identify the captured table publication. */
_Static_assert(sizeof(struct environment_entry_reply) == 128, "environment entry reply layout");
_Static_assert(sizeof(struct environment_set_entry) == 128, "environment set entry layout");
_Static_assert(sizeof(struct environment_unset_entry) == 72, "environment unset entry layout");
_Static_assert(sizeof(struct environment_lookup_message) == 96, "environment lookup layout");
_Static_assert(sizeof(struct environment_info_reply) == 24, "environment info layout");

#endif

#ifndef ABI_PACKAGE_COMMANDS_H
#define ABI_PACKAGE_COMMANDS_H

#include <abi/handle.h>
#include <stdint.h>

#define DIRECTORY_PACKAGE_COMMANDS UINT64_C(11)
#define DIRECTORY_COMMAND_CAPTURE UINT64_C(12)
#define DIRECTORY_COMMAND_INFO UINT64_C(13)
#define DIRECTORY_COMMAND_LOOKUP UINT64_C(14)
#define DIRECTORY_COMMAND_BIND UINT64_C(15)

#define PACKAGE_COMMANDS_LIMIT 256u
#define PACKAGE_COMMAND_NAME_MAX 255u
#define PACKAGE_COMMAND_ID_MAX 64u
#define PACKAGE_COMMAND_VERSION_MAX 64u
#define PACKAGE_COMMAND_PATH_MAX 250u
#define PACKAGE_COMMAND_ABI_MAX 64u
/* Eight bytes: PXCMDS and two zeros. */
#define PACKAGE_COMMAND_INDEX_MAGIC "PXCMDS\0"
#define PACKAGE_COMMAND_VIEW_LIVE UINT64_C(1)

/* sets/N.commands: little endian, this header followed by count fixed records,
 * without trailing bytes. The magic is PXCMDS padded with zeros, the set equals
 * N and the record size is 968. Any other header is a damaged index. */
struct package_command_index_header {
  char magic[8];
  uint64_t set;
  uint32_t count;
  uint32_t record_size;
  uint64_t reserved;
};

/* Strings terminate with NUL and all remaining bytes are zero. Records are
 * sorted by unsigned name bytes with no duplicate. kind is DIRECTORY_KIND_FILE
 * for a native .pxe, DIRECTORY_KIND_DIRECTORY for a complete .pxb directory.
 * path is confined to store/ID/VERSION/tree; command is empty for a plain file
 * and equals name, a declared bundle command, for a bundle. abi is the ABI the
 * package was built for ([0-9a-z][0-9a-z.-]*); only program and toolchain
 * packages export commands, and they always record one. The kernel carries
 * abi and never compares it; the launcher does. All reserved
 * bytes are zero. */
struct package_command_record {
  uint64_t kind;
  char name[256];
  char id[65];
  char version[65];
  char path[251];
  char command[256];
  char abi[65];
  uint8_t reserved[2];
};

struct package_command_info {
  uint8_t pool_id[16];
  uint8_t volume_id[16];
  uint64_t origin; /* Kernel volume domain, held-lifetime only; not a disk UUID. */
  uint64_t set; /* Zero means the captured empty set; live views report zero. */
  uint64_t flags;
  uint64_t count; /* Fixed views only. */
  uint64_t limit;
};

/* These operations use a complete directory_message. PACKAGE_COMMANDS creates
 * a live read-only view from a held native volume root with LOOKUP|READ_FILES;
 * COMMAND_CAPTURE returns a fixed view. Both return directory_child_reply.
 * COMMAND_INFO returns package_command_info. Their otherwise-unused body is zero.
 * CAPTURE may reuse an existing fixed view from this volume, including set zero.
 * The resulting grant preserves the lineage of both handles. */
struct package_command_capture_request {
  handle_t previous; /* Zero captures the current set. */
  uint64_t reserved[6];
};

/* COMMAND_BIND is called on an independently held ordinary native volume root
 * with LOOKUP|READ_FILES. It verifies a fixed capture belongs to that actual
 * volume and returns directory_child_reply holding the receiver's pinned root,
 * with receiver rights intersected with LOOKUP|ENUMERATE|READ_FILES. The capture
 * contributes revocation lineage, never root authority. A command view cannot
 * be the receiver. Both handles' CUT lineage remains on the returned grant. */
struct package_command_bind_request {
  handle_t capture; /* Required fixed command view from this volume. */
  uint64_t reserved[6];
};

/* COMMAND_LOOKUP returns one owned target and its captured provenance. It
 * requires a fixed view; no target is returned on failure. Rights have the
 * ordinary typed DIRECTORY_LOOKUP rules and may contain only read-only bits.
 * Ordinary LOOKUP/LOOKUP_TYPED and ENUMERATE also expose the indexed names. */
struct package_command_lookup_request {
  uint64_t name;
  uint64_t name_length;
  uint64_t file_rights;
  uint64_t directory_rights;
  uint64_t reserved[3];
};

struct package_command_lookup_reply {
  handle_t handle;
  uint64_t protocol;
  struct package_command_info info;
  struct package_command_record target;
};

_Static_assert(sizeof(struct package_command_index_header) == 32, "command index header layout");
_Static_assert(sizeof(PACKAGE_COMMAND_INDEX_MAGIC) == sizeof(((struct package_command_index_header *)0)->magic),
    "command index magic fills its field");
_Static_assert(sizeof(struct package_command_record) == 968, "command index record layout");
_Static_assert(sizeof(struct package_command_info) == 72, "command view info layout");
_Static_assert(sizeof(struct package_command_capture_request) == 56, "command capture request layout");
_Static_assert(sizeof(struct package_command_bind_request) == 56, "command bind request layout");
_Static_assert(sizeof(struct package_command_lookup_request) == 56, "command lookup request layout");
_Static_assert(sizeof(struct package_command_lookup_reply) == 1056, "command lookup reply layout");

#endif

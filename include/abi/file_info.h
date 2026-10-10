#ifndef ABI_FILE_INFO_H
#define ABI_FILE_INFO_H

#include <stdint.h>
#include <stddef.h>

#define FILE_INFO_SIZE_VALID (UINT64_C(1) << 0)
#define FILE_INFO_DOMAIN_VALID (UINT64_C(1) << 1)
#define FILE_INFO_OBJECT_VALID (UINT64_C(1) << 2)
#define FILE_INFO_MTIME_VALID (UINT64_C(1) << 3)
#define FILE_INFO_VALID_BITS (FILE_INFO_SIZE_VALID | FILE_INFO_DOMAIN_VALID | \
                             FILE_INFO_OBJECT_VALID | FILE_INFO_MTIME_VALID)

/* Reserved for provider-owned private bytes, never a proxy for native backing.
 * These snapshots have a known disjoint domain but need not report object IDs. */
#define FILE_DOMAIN_PRIVATE_BYTES UINT64_C(1)

/* INFO has no FILE payload; DIRECTORY uses its complete zeroed message.
 * FILE requires READ or WRITE, DIRECTORY no additional content rights.
 * All reported fields describe the held object, not a pathname. Unknown fields
 * have no usable value: zero is not an absence marker. OBJECT implies DOMAIN.
 * Different valid domains prove distinctness without object IDs. Within one
 * domain, both object IDs are needed. Hold a reference to the original object
 * throughout comparison; IDs have no after-final-release or cross-boot promise.
 * Signed Unix seconds and 0..999999999 nanoseconds are real sampled mtime, not
 * a change counter. INFO reserves nothing and freezes neither bytes nor names. */
struct file_info_reply {
  uint64_t valid;
  uint64_t size;
  uint64_t domain;
  uint64_t object;
  int64_t modified_seconds;
  uint64_t modified_nanoseconds;
};

_Static_assert(sizeof(struct file_info_reply) == 48, "file info reply layout");
_Static_assert(offsetof(struct file_info_reply, modified_seconds) == 32, "file info time offset");

#endif

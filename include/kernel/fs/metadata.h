#ifndef KERNEL_FS_METADATA_H
#define KERNEL_FS_METADATA_H

#include <abi/file_info.h>
#include <stdbool.h>

/* RAM/archive state only. Size remains owned by the file, never duplicated. */
struct fs_metadata {
  uint64_t domain, object;
  int64_t modified_ns;
  bool modified_valid;
};

/* BSP, IF=0. Never wrap or reuse; failure publishes no identity. The reserved
 * private-byte domain is excluded. Backends own their domains and object IDs. */
bool fs_metadata_allocate_id(uint64_t *id);
/* BSP, IF=0, before publishing a RAM/archive object. Their common domains
 * span root grants, including RAM rename across directory roots. */
bool fs_metadata_create(struct fs_metadata *metadata, bool archive);
/* Under the owning RAM file operation or directory lock. Any CPU, IF=0. */
void fs_metadata_touch(struct fs_metadata *metadata);
/* Pure conversions. Caller owns a stable snapshot; fields accumulate in reply. */
void fs_metadata_mtime(int64_t nanoseconds, struct file_info_reply *reply);
void fs_metadata_info(const struct fs_metadata *metadata, struct file_info_reply *reply);

#endif

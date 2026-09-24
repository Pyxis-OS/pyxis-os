#ifndef KERNEL_VIRTIO_FS_H
#define KERNEL_VIRTIO_FS_H

#include <stdint.h>

struct virtio_fs_session {
  uint32_t major, minor, max_write, time_granularity;
  bool ready;
};

/* Sole BSP transport worker, IF=1. Starts one FUSE session using the ordinary
 * request queue. Returns a diagnostic on failure, NULL on success. */
const char *virtio_fs_session_init(struct virtio_fs_session *session);

#endif

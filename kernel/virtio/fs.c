#include <kernel/log.h>
#include <kernel/virtio/fs.h>
#include <kernel/virtio/pci.h>
#include <stddef.h>

#define FUSE_VERSION_MAJOR 7
#define FUSE_VERSION_MINOR 38
#define FUSE_INIT 26
#define FUSE_INIT_UNIQUE UINT64_C(1)

/* FUSE 7.38, little-endian on this transport. These are wire structures, not
 * the native filesystem ABI. No optional INIT flags or header extensions. */
struct fuse_in_header {
  uint32_t length, opcode;
  uint64_t unique, node_id;
  uint32_t uid, gid, pid;
  uint16_t extension_length, padding;
};

struct fuse_out_header {
  uint32_t length;
  int32_t error;
  uint64_t unique;
};

struct fuse_init_in {
  uint32_t major, minor, max_readahead, flags;
};

struct fuse_init_out {
  uint32_t major, minor, max_readahead, flags;
  uint16_t max_background, congestion_threshold;
  uint32_t max_write, time_granularity;
  uint16_t max_pages, map_alignment;
  uint32_t flags2, unused[7];
};

_Static_assert(sizeof(struct fuse_in_header) == 40, "FUSE request header");
_Static_assert(sizeof(struct fuse_out_header) == 16, "FUSE reply header");
_Static_assert(sizeof(struct fuse_init_in) == 16, "FUSE INIT without INIT_EXT");
_Static_assert(sizeof(struct fuse_init_out) == 64, "FUSE 7.38 INIT reply");

const char *virtio_fs_session_init(struct virtio_fs_session *session)
{
  struct {
    struct fuse_in_header header;
    struct fuse_init_in init;
  } request = {
    .header = {.length = sizeof(request), .opcode = FUSE_INIT, .unique = FUSE_INIT_UNIQUE},
    .init = {.major = FUSE_VERSION_MAJOR, .minor = FUSE_VERSION_MINOR},
  };
  struct {
    struct fuse_out_header header;
    struct fuse_init_out init;
  } reply;
  size_t length;
  const char *failure = virtio_fs_pci_request(&request, sizeof(request),
                                             &reply, sizeof(reply), &length);
  if (failure) {
    return failure;
  }
  if (length < sizeof(reply.header) || reply.header.length != length ||
      reply.header.unique != FUSE_INIT_UNIQUE) {
    return "invalid FUSE INIT reply header";
  }
  if (reply.header.error) {
    klog("virtio-fs: FUSE INIT error %d\n", reply.header.error);
    return "FUSE INIT rejected";
  }
  if (length != sizeof(reply) || reply.init.major != FUSE_VERSION_MAJOR ||
      reply.init.minor != FUSE_VERSION_MINOR) {
    return "unsupported FUSE INIT version or reply size (requires 7.38)";
  }
  if (reply.init.flags || reply.init.flags2 || reply.init.max_readahead) {
    return "unrequested FUSE INIT features or read-ahead";
  }
  *session = (struct virtio_fs_session){
    .major = reply.init.major,
    .minor = reply.init.minor,
    .max_write = reply.init.max_write,
    .time_granularity = reply.init.time_granularity,
    .ready = true,
  };
  return NULL;
}

#ifndef KERNEL_VIRTIO_FUSE_WIRE_H
#define KERNEL_VIRTIO_FUSE_WIRE_H
#include <stdint.h>

#define FUSE_VERSION_MAJOR 7
#define FUSE_VERSION_MINOR 38

#define FUSE_ROOT_ID UINT64_C(1)

#define FUSE_LOOKUP 1
#define FUSE_FORGET 2
#define FUSE_GETATTR 3
#define FUSE_SETATTR 4
#define FUSE_MKDIR 9
#define FUSE_UNLINK 10
#define FUSE_RMDIR 11
#define FUSE_OPEN 14
#define FUSE_READ 15
#define FUSE_WRITE 16
#define FUSE_RELEASE 18
#define FUSE_FSYNC 20
#define FUSE_INIT 26
#define FUSE_OPENDIR 27
#define FUSE_READDIR 28
#define FUSE_RELEASEDIR 29
#define FUSE_FSYNCDIR 30
#define FUSE_CREATE 35
#define FUSE_RENAME2 45

#define FUSE_RENAME_NOREPLACE 1u

#define FUSE_OPEN_READ_ONLY 0
#define FUSE_OPEN_WRITE_ONLY 1
#define FUSE_OPEN_ACCESS_MASK 3
#define FUSE_OPEN_CREATE 0100u
#define FUSE_OPEN_EXCLUSIVE 0200u

#define FUSE_ATTR_SIZE (1u << 3)
#define FUSE_ATTR_HANDLE (1u << 6)
#define FUSE_OPEN_DIRECT_IO 1u
#define FUSE_OPEN_KEEP_CACHE 2u
#define FUSE_OPEN_CACHE_DIR 8u
#define FUSE_OPEN_NOFLUSH 32u

#define FUSE_MODE_TYPE_MASK 0170000u
#define FUSE_MODE_FILE 0100000u
#define FUSE_MODE_DIRECTORY 0040000u
#define FUSE_MODE_SYMLINK 0120000u

#define FUSE_DIRENT_UNKNOWN 0
#define FUSE_DIRENT_DIRECTORY 4
#define FUSE_DIRENT_FILE 8
#define FUSE_DIRENT_SYMLINK 10
#define FUSE_DIRENT_ALIGNMENT 8u

#define FUSE_ERRNO_MAX 4095
#define FUSE_EPERM 1
#define FUSE_ENOENT 2
#define FUSE_ENOMEM 12
#define FUSE_EACCES 13
#define FUSE_EBUSY 16
#define FUSE_EEXIST 17
#define FUSE_EXDEV 18
#define FUSE_ENOTDIR 20
#define FUSE_EISDIR 21
#define FUSE_EINVAL 22
#define FUSE_EMFILE 24
#define FUSE_EFBIG 27
#define FUSE_ENOSPC 28
#define FUSE_EROFS 30
#define FUSE_ENAMETOOLONG 36
#define FUSE_ENOSYS 38
#define FUSE_ENOTEMPTY 39
#define FUSE_ELOOP 40
#define FUSE_EOVERFLOW 75
#define FUSE_EOPNOTSUPP 95
#define FUSE_ETIMEDOUT 110
#define FUSE_ESTALE 116
#define FUSE_EDQUOT 122

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

struct fuse_attr {
  uint64_t inode, size, blocks, atime, mtime, ctime;
  uint32_t atime_nsec, mtime_nsec, ctime_nsec, mode;
  uint32_t links, uid, gid, rdev, block_size, flags;
};

struct fuse_entry_out {
  uint64_t node_id, generation, entry_valid, attr_valid;
  uint32_t entry_valid_nsec, attr_valid_nsec;
  struct fuse_attr attr;
};

struct fuse_getattr_in {
  uint32_t flags, padding;
  uint64_t handle;
};

struct fuse_attr_out {
  uint64_t valid;
  uint32_t valid_nsec, padding;
  struct fuse_attr attr;
};

struct fuse_open_in {
  uint32_t flags, open_flags;
};

struct fuse_open_out {
  uint64_t handle;
  uint32_t flags, padding;
};

struct fuse_release_in {
  uint64_t handle;
  uint32_t flags, release_flags;
  uint64_t lock_owner;
};

struct fuse_fsync_in {
  uint64_t handle;
  uint32_t flags, padding;
};

_Static_assert(sizeof(struct fuse_fsync_in) == 16, "FUSE fsync request");

struct fuse_read_in {
  uint64_t handle, offset;
  uint32_t size, read_flags;
  uint64_t lock_owner;
  uint32_t flags, padding;
};

struct fuse_create_in {
  uint32_t flags, mode, umask, open_flags;
};

struct fuse_mkdir_in {
  uint32_t mode, umask;
};

struct fuse_rename2_in {
  uint64_t newdir;
  uint32_t flags, padding;
};

struct fuse_create_out {
  struct fuse_entry_out entry;
  struct fuse_open_out opened;
};

_Static_assert(sizeof(struct fuse_create_in) == 16, "FUSE create request prefix");
_Static_assert(sizeof(struct fuse_create_out) == 144, "FUSE create reply");
_Static_assert(sizeof(struct fuse_mkdir_in) == 8, "FUSE mkdir request prefix");
_Static_assert(sizeof(struct fuse_rename2_in) == 16, "FUSE rename2 request prefix");

struct fuse_write_in {
  uint64_t handle, offset;
  uint32_t size, write_flags;
  uint64_t lock_owner;
  uint32_t flags, padding;
};

struct fuse_write_out {
  uint32_t size, padding;
};

struct fuse_setattr_in {
  uint32_t valid, padding;
  uint64_t handle, size, lock_owner, atime, mtime, ctime;
  uint32_t atime_nsec, mtime_nsec, ctime_nsec, mode, unused4, uid, gid, unused5;
};

_Static_assert(sizeof(struct fuse_write_in) == 40, "FUSE write request prefix");
_Static_assert(sizeof(struct fuse_write_out) == 8, "FUSE write reply");
_Static_assert(sizeof(struct fuse_setattr_in) == 88, "FUSE setattr request");

struct fuse_dirent {
  uint64_t inode, next_cookie;
  uint32_t name_length, type;
};

_Static_assert(sizeof(struct fuse_attr) == 88, "FUSE attributes");
_Static_assert(sizeof(struct fuse_entry_out) == 128, "FUSE lookup reply");
_Static_assert(sizeof(struct fuse_attr_out) == 104, "FUSE getattr reply");
_Static_assert(sizeof(struct fuse_open_in) == 8 && sizeof(struct fuse_open_out) == 16,
               "FUSE open messages");
_Static_assert(sizeof(struct fuse_release_in) == 24, "FUSE release request");
_Static_assert(sizeof(struct fuse_read_in) == 40, "FUSE read request");
_Static_assert(sizeof(struct fuse_dirent) == 24, "FUSE directory entry prefix");
#endif

#ifndef KERNEL_FS_NPFS_ADAPTER_H
#define KERNEL_FS_NPFS_ADAPTER_H

#include <kernel/fs/npfs.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include "npfs_store.h"

struct package_command_view;
struct package_sdk_view;

/* Sole npfs worker owns these fields and their backing references. */
struct npfs_node {
  struct npfs_node *next;
  struct npfs_store_inode *inode;
  struct execution_group *cleanup_group;
  struct package_command_view *commands;
  struct package_sdk_view *sdk;
  /* A package view directory's own object in the package-view domain. */
  uint64_t view_object;
  uint64_t rights, kind;
  union {
    struct directory_object directory;
    struct file_object file;
  } wrapper;
};

/* Consume an owned inode on both success and failure. */
enum call_status npfs_wrap_inode(struct npfs_store_inode *inode, uint64_t rights,
    struct kernel_object **out);
enum call_status npfs_allocate_node(uint64_t kind, uint64_t rights, struct npfs_node **out);
enum call_status npfs_commands_perform(struct npfs_store_context *context, struct npfs_job *job);
void npfs_commands_release(struct package_command_view *view);
enum call_status npfs_sdk_perform(struct npfs_store_context *context, struct npfs_job *job);
void npfs_sdk_release(struct package_sdk_view *view);

#endif

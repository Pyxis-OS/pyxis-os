#ifndef KERNEL_FS_NPFS_PACKAGES_H
#define KERNEL_FS_NPFS_PACKAGES_H

#include "npfs_adapter.h"

/* Store access and name rules shared by the package command, roots and sdk
 * views of a packages volume; ROOT is that volume's root. */

#define PACKAGE_VIEW_RIGHTS \
  (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES)

bool npfs_package_zero(const void *bytes, size_t size);
/* NUL within SIZE, nonempty unless EMPTY, and zero after the NUL. */
bool npfs_package_string(const char *bytes, size_t size, bool empty);
/* A package ID, or with VERSION a package version. */
bool npfs_package_name(const char *name, bool version);
/* One path component: not empty, . or .., and without NUL, / or \. */
bool npfs_package_component(const char *name, size_t size);
bool npfs_package_utf8(const char *text);

/* PARENT's child of KIND; another kind is IO. */
enum call_status npfs_package_child(struct npfs_store_context *context,
    struct npfs_store_inode *parent, const char *name, size_t length, uint64_t kind,
    struct npfs_store_inode **out);
/* Exactly SIZE bytes at OFFSET; a short read is IO. */
enum call_status npfs_package_read(struct npfs_store_context *context,
    struct npfs_store_inode *inode, uint64_t offset, void *bytes, size_t size);
/* The set current names, or zero without one. A malformed current is IO. */
enum call_status npfs_package_current(struct npfs_store_context *context,
    struct npfs_store_inode *root, uint64_t *set);
/* sets/N followed by SUFFIX; a missing one is IO, a damaged set. */
enum call_status npfs_package_set_file(struct npfs_store_context *context,
    struct npfs_store_inode *root, uint64_t set, const char *suffix,
    struct npfs_store_inode **out);
/* store/ID/VERSION/tree, or PATH within it, as KIND with RIGHTS. A missing
 * entry is IO, since an installed set names it. */
enum call_status npfs_package_open(struct npfs_store_context *context,
    struct npfs_store_inode *root, const char *id, const char *version, const char *path,
    uint64_t kind, uint64_t rights, struct kernel_object **out);

/* The set of a fixed command capture; false for any other view. */
bool npfs_commands_fixed_set(const struct package_command_view *view, uint64_t *set);

#endif

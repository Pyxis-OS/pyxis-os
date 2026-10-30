#include <abi/package_sdk.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include "npfs_packages.h"

/* A validated sets/N.sdk held in memory and shared by the views of set N. */
struct package_sdk_index {
  size_t references, size;
  uint64_t set;
  /* Unique among the indexes read this boot; it names their directories. */
  uint32_t serial;
  uint32_t package_count, file_count;
  const struct package_sdk_package *packages;
  const struct package_sdk_file *files;
  const char *strings;
  uint8_t bytes[];
};

/* An indexed directory's object ID: the index serial, then the first file at
 * or under the directory and the length of its path, which together name one
 * directory of that index. */
static_assert(PACKAGE_SDK_PATH_MAX < (1u << 8), "sdk path length fits its field");
#define SDK_FILES_MAX (1u << 24) /* the file position field of an object ID */
#define SDK_SERIAL_MAX ((UINT32_C(1) << 31) - 1)
static uint32_t next_serial = 1;

/* One directory of sdk://. The live root holds no index: each operation reads
 * current and uses the cache below. A fixed directory holds its set's index,
 * NULL for the empty set zero, and its path within the view, empty at the
 * root. */
struct package_sdk_view {
  struct package_sdk_index *index;
  uint64_t set;
  bool live;
  size_t length;
  char prefix[];
};

/* The index of the current set last read, for the packages volume named by
 * these identities. Sets are immutable, so it stays valid until current names
 * another set; only the npfs worker touches it. */
static struct package_sdk_index *cached;
static uint8_t cached_pool[16], cached_volume[16];

static void index_release(struct package_sdk_index *index)
{
  if (!index) return;
  KASSERT(index->references);
  if (!--index->references) npfs_package_free(index, sizeof(*index) + index->size);
}

static enum call_status index_retain(struct package_sdk_index *index)
{
  if (!index) return CALL_OK;
  if (index->references == SIZE_MAX) return CALL_LIMIT;
  ++index->references;
  return CALL_OK;
}

void npfs_sdk_release(struct package_sdk_view *view)
{
  if (!view) return;
  npfs_require_worker();
  index_release(view->index);
  npfs_package_free(view, sizeof(*view) + view->length + 1);
}

static const char *path_of(const struct package_sdk_index *index, size_t file)
{
  return index->strings + index->files[file].path;
}

static int compare(const struct package_sdk_index *index, size_t file, const char *key,
    size_t length)
{
  size_t path_length = index->files[file].path_length;
  int order = memcmp(path_of(index, file), key, path_length < length ? path_length : length);
  if (order) return order;
  return path_length < length ? -1 : path_length > length ? 1 : 0;
}

/* The first file whose path is not below KEY. */
static size_t lower_bound(const struct package_sdk_index *index, const char *key, size_t length)
{
  size_t low = 0, high = index ? index->file_count : 0;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (compare(index, middle, key, length) < 0) low = middle + 1;
    else high = middle;
  }
  return low;
}

static bool starts_with(const struct package_sdk_index *index, size_t file, const char *prefix,
    size_t length)
{
  return file < index->file_count && index->files[file].path_length >= length &&
      !memcmp(path_of(index, file), prefix, length);
}

/* include/..., lib/NAME or lib/pkgconfig/NAME (docs/wip/sdk-view.md, decision 45). */
static bool view_path(const char *path, size_t length)
{
  size_t components = 0, start = 0;
  bool include = false, lib = false, pkgconfig = false;
  for (size_t i = 0; i <= length; ++i) {
    if (i != length && path[i] != '/') continue;
    if (!npfs_package_component(path + start, i - start)) return false;
    if (!components) {
      include = i - start == 7 && !memcmp(path, "include", 7);
      lib = i - start == 3 && !memcmp(path, "lib", 3);
    } else if (components == 1) {
      pkgconfig = lib && i != length && i - start == 9 && !memcmp(path + start, "pkgconfig", 9);
    }
    ++components;
    start = i + 1;
  }
  return npfs_package_utf8(path) && ((include && components >= 2) ||
      (lib && (components == 2 || (pkgconfig && components == 3))));
}

static bool index_valid(struct package_sdk_index *index, uint64_t set)
{
  struct package_sdk_index_header header;
  memcpy(&header, index->bytes, sizeof(header));
  uint64_t expected = sizeof(header) + (uint64_t)header.packages * sizeof(struct package_sdk_package) +
      (uint64_t)header.files * sizeof(struct package_sdk_file) + header.strings;
  if (memcmp(header.magic, PACKAGE_SDK_INDEX_MAGIC, sizeof(header.magic)) || header.set != set ||
      header.reserved || header.files > SDK_FILES_MAX || expected != index->size) return false;
  index->set = set;
  index->package_count = header.packages;
  index->file_count = header.files;
  index->packages = (const void *)(index->bytes + sizeof(header));
  index->files = (const void *)(index->packages + header.packages);
  index->strings = (const char *)(index->files + header.files);
  for (size_t i = 0; i < index->package_count; ++i) {
    const struct package_sdk_package *package = &index->packages[i];
    if (!npfs_package_string(package->id, sizeof(package->id), false) ||
        !npfs_package_string(package->version, sizeof(package->version), false) ||
        !npfs_package_zero(package->reserved, sizeof(package->reserved)) ||
        !npfs_package_name(package->id, false) || !npfs_package_name(package->version, true) ||
        (i && memcmp(index->packages[i - 1].id, package->id, sizeof(package->id)) >= 0)) return false;
  }
  uint64_t offset = 0;
  for (size_t i = 0; i < index->file_count; ++i) {
    const struct package_sdk_file *file = &index->files[i];
    if (file->path != offset || file->reserved || file->package >= index->package_count ||
        !file->path_length || file->path_length > PACKAGE_SDK_PATH_MAX ||
        offset + file->path_length >= header.strings) return false;
    const char *path = path_of(index, i);
    if (strnlen(path, file->path_length + 1) != file->path_length ||
        !view_path(path, file->path_length) ||
        (i && compare(index, i - 1, path, file->path_length) >= 0)) return false;
    offset += file->path_length + 1;
  }
  if (offset != header.strings) return false;
  /* A directory of the view is never also one of its files. */
  for (size_t i = 0; i < index->file_count; ++i) {
    const char *path = path_of(index, i);
    for (size_t at = 0; at < index->files[i].path_length; ++at) {
      if (path[at] != '/') continue;
      size_t found = lower_bound(index, path, at);
      if (found < index->file_count && !compare(index, found, path, at)) return false;
    }
  }
  return true;
}

static enum call_status index_read(struct npfs_store_context *context,
    struct npfs_store_inode *root, uint64_t set, struct package_sdk_index **out)
{
  *out = NULL;
  struct npfs_store_inode *file;
  enum call_status status = npfs_package_set_file(context, root, set, ".sdk", &file);
  if (status != CALL_OK) return status;
  uint64_t size = npfs_store_size(file);
  /* Only the package-view budget limits the index, at the allocation below. */
  if (size < sizeof(struct package_sdk_index_header)) status = CALL_IO;
  else if (size > SIZE_MAX - sizeof(struct package_sdk_index)) status = CALL_LIMIT;
  struct package_sdk_index *index = NULL;
  if (status == CALL_OK) {
    status = npfs_package_allocate(sizeof(*index) + (size_t)size, (void **)&index);
  }
  if (status == CALL_OK && next_serial > SDK_SERIAL_MAX) {
    npfs_package_free(index, sizeof(*index) + (size_t)size);
    status = CALL_LIMIT;
  }
  if (status == CALL_OK) {
    index->references = 1;
    index->size = (size_t)size;
    index->serial = next_serial++;
    status = npfs_package_read(context, file, 0, index->bytes, index->size);
    if (status == CALL_OK && !index_valid(index, set)) status = CALL_IO;
    if (status == CALL_OK) *out = index;
    else index_release(index);
  }
  npfs_store_release(file);
  return status;
}

/* A reference to SET's index on ROOT's volume, NULL for set zero. Only the
 * current set, CURRENT, replaces the cache, so a capture of an older set does
 * not evict it. */
static enum call_status index_for_set(struct npfs_store_context *context,
    struct npfs_store_inode *root, uint64_t set, bool current, struct package_sdk_index **out)
{
  *out = NULL;
  if (!set) return CALL_OK;
  struct directory_filesystem_info volume;
  npfs_store_info(root, &volume);
  if (cached && cached->set == set && !memcmp(cached_pool, volume.pool_id, sizeof(cached_pool)) &&
      !memcmp(cached_volume, volume.volume_id, sizeof(cached_volume))) {
    enum call_status status = index_retain(cached);
    if (status == CALL_OK) *out = cached;
    return status;
  }
  /* Release the stale cache first when nothing else holds it, so a set change
   * never needs two unshared indexes at once. */
  if (current && cached && cached->references == 1) {
    index_release(cached);
    cached = NULL;
  }
  struct package_sdk_index *index;
  enum call_status status = index_read(context, root, set, &index);
  if (status != CALL_OK || !current) {
    *out = status == CALL_OK ? index : NULL;
    return status;
  }
  status = index_retain(index);
  if (status != CALL_OK) {
    index_release(index);
    return status;
  }
  index_release(cached);
  cached = index;
  memcpy(cached_pool, volume.pool_id, sizeof(cached_pool));
  memcpy(cached_volume, volume.volume_id, sizeof(cached_volume));
  *out = index;
  return CALL_OK;
}

/* A directory node for PREFIX of INDEX, whose reference it takes. FIRST is the
 * position of the first file at or under PREFIX. */
static enum call_status wrap_view(struct npfs_job *job, struct package_sdk_index *index,
    uint64_t set, bool live, const char *prefix, size_t length, size_t first, uint64_t rights)
{
  struct package_sdk_view *view;
  enum call_status status = npfs_package_allocate(sizeof(*view) + length + 1, (void **)&view);
  if (status != CALL_OK) {
    index_release(index);
    return status;
  }
  *view = (struct package_sdk_view){.index = index, .set = set, .live = live, .length = length};
  memcpy(view->prefix, prefix, length);
  view->prefix[length] = 0;
  uint64_t object = index ? NPFS_PACKAGE_VIEW_INDEXED | (uint64_t)index->serial << 32 |
      (uint64_t)first << 8 | length : 0;
  struct npfs_node *node;
  status = npfs_package_view_node(job, rights, object, &node);
  if (status != CALL_OK) {
    npfs_sdk_release(view);
    return status;
  }
  node->sdk = view;
  job->object = &node->wrapper.directory.object;
  job->child_rights = rights;
  return CALL_OK;
}

static enum call_status lookup(struct npfs_store_context *context, struct npfs_job *job,
    const struct package_sdk_view *view, struct package_sdk_index *index, uint64_t set)
{
  bool typed = job->kind == DIRECTORY_KIND_ANY;
  if (!npfs_package_component(job->name, job->count) ||
      (typed && ((job->child_rights & ~FILE_RIGHT_READ) ||
      (job->directory_child_rights & ~PACKAGE_VIEW_RIGHTS)))) return CALL_BAD_REQUEST;
  if (!typed && job->kind != DIRECTORY_KIND_FILE && job->kind != DIRECTORY_KIND_DIRECTORY) {
    return CALL_BAD_REQUEST;
  }
  /* The candidate path, with room for the separator that selects a directory. */
  char path[PACKAGE_SDK_PATH_MAX + 2];
  size_t length = view->length + (view->length ? 1 : 0) + job->count;
  if (!index || length > PACKAGE_SDK_PATH_MAX) return CALL_NOT_FOUND;
  memcpy(path, view->prefix, view->length);
  if (view->length) path[view->length] = '/';
  memcpy(path + length - job->count, job->name, job->count);
  size_t file = lower_bound(index, path, length);
  uint64_t kind = DIRECTORY_KIND_FILE;
  if (file == index->file_count || compare(index, file, path, length)) {
    path[length] = '/';
    file = lower_bound(index, path, length + 1);
    if (!starts_with(index, file, path, length + 1)) {
      return CALL_NOT_FOUND;
    }
    kind = DIRECTORY_KIND_DIRECTORY;
  }
  if (!typed && job->kind != kind) return CALL_WRONG_TYPE;
  bool directory = kind == DIRECTORY_KIND_DIRECTORY;
  uint64_t requested = typed && directory ? job->directory_child_rights : job->child_rights;
  uint64_t allowed = directory ? job->rights & PACKAGE_VIEW_RIGHTS :
      (job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0;
  if (requested & ~allowed) return CALL_DENIED;
  if (directory) {
    /* Ordinary untyped DIRECTORY lookup uses its single child mask. */
    if (!typed) job->directory_child_rights = requested;
    enum call_status status = index_retain(index);
    if (status != CALL_OK) return status;
    return wrap_view(job, index, set, false, path, length, file, requested);
  }
  const struct package_sdk_package *package = &index->packages[index->files[file].package];
  return npfs_package_open(context, job->node->inode, package->id, package->version,
      path_of(index, file), DIRECTORY_KIND_FILE, requested, &job->object);
}

/* Each entry is one child name of the directory; the cursor position is the
 * index of the first file at or under the next child. */
static enum call_status enumerate(struct npfs_job *job, const struct package_sdk_view *view,
    const struct package_sdk_index *index, uint64_t set)
{
  uint64_t generation = set + 1;
  job->entry = (struct directory_enumerate_reply){.cursor = {generation, job->cursor.position}};
  if (job->cursor.generation && job->cursor.generation != generation) {
    job->entry.outcome = DIRECTORY_CHANGED;
    job->entry.cursor.position = 0;
    return CALL_OK;
  }
  char prefix[PACKAGE_SDK_PATH_MAX + 2];
  size_t length = view->length;
  memcpy(prefix, view->prefix, length);
  if (length) prefix[length++] = '/';
  size_t file = index ? lower_bound(index, prefix, length) : 0;
  if (job->cursor.position > file) file = (size_t)job->cursor.position;
  if (!index || !starts_with(index, file, prefix, length)) {
    job->entry.outcome = DIRECTORY_END;
    return CALL_OK;
  }
  const char *name = path_of(index, file) + length;
  size_t name_length = 0;
  while (name[name_length] && name[name_length] != '/') ++name_length;
  bool directory = name[name_length] == '/';
  job->entry.name_size = name_length + 1;
  job->entry.kind = directory ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
  if (job->count < job->entry.name_size) {
    job->entry.outcome = DIRECTORY_BUFFER_TOO_SMALL;
    return CALL_OK;
  }
  memcpy(job->name, name, name_length);
  job->name[name_length] = 0;
  /* Skip the rest of this child's files. */
  size_t child = length + name_length + 1;
  ++file;
  while (directory && starts_with(index, file, path_of(index, file - 1), child)) ++file;
  job->entry.outcome = DIRECTORY_ENTRY;
  job->entry.cursor.position = file;
  return CALL_OK;
}

enum call_status npfs_sdk_perform(struct npfs_store_context *context, struct npfs_job *job)
{
  struct npfs_node *node = job->node;
  struct package_sdk_view *view = node->sdk;
  if (node->kind != DIRECTORY_KIND_DIRECTORY) return CALL_WRONG_TYPE;
  if (job->operation == NPFS_SDK_VIEW) {
    uint64_t required = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_READ_FILES;
    if ((job->rights & required) != required) return CALL_DENIED;
    if (view || node->commands || !npfs_store_is_root(node->inode)) return CALL_BAD_OPERATION;
    /* The cache trusts sets/ to be immutable, which pkg keeps only on its
     * packages volume. */
    struct directory_filesystem_info volume;
    npfs_store_info(node->inode, &volume);
    if (strnlen(volume.volume_name, sizeof(volume.volume_name)) != sizeof(PACKAGE_SDK_VOLUME) - 1 ||
        memcmp(volume.volume_name, PACKAGE_SDK_VOLUME, sizeof(PACKAGE_SDK_VOLUME) - 1)) {
      return CALL_BAD_OPERATION;
    }
    return wrap_view(job, NULL, 0, true, "", 0, 0, job->rights & PACKAGE_VIEW_RIGHTS);
  }
  if (job->operation == NPFS_INFO) {
    npfs_package_view_info(node, &job->file_info);
    return CALL_OK;
  }
  if (job->operation == NPFS_COMMAND_CAPTURE) {
    if (!(job->rights & DIRECTORY_RIGHT_LOOKUP)) return CALL_DENIED;
    if (!view->live) return CALL_BAD_OPERATION;
    uint64_t rights = job->rights & PACKAGE_VIEW_RIGHTS;
    uint64_t set;
    enum call_status status = CALL_OK;
    uint64_t current = 0;
    if (job->destination) {
      struct npfs_node *previous = job->destination;
      if (!npfs_commands_fixed_set(previous->commands, &set) ||
          !npfs_store_same_volume(node->inode, previous->inode)) return CALL_BAD_REQUEST;
      rights &= job->destination_rights;
      status = npfs_package_current(context, node->inode, &current);
    } else {
      status = npfs_package_current(context, node->inode, &set);
      current = set;
    }
    struct package_sdk_index *index = NULL;
    if (status == CALL_OK) status = index_for_set(context, node->inode, set, set == current, &index);
    if (status != CALL_OK) return status;
    return wrap_view(job, index, set, false, "", 0, 0, rights);
  }
  if (job->operation != NPFS_LOOKUP && job->operation != NPFS_ENUMERATE) {
    return job->operation == NPFS_FILESYSTEM_INFO || job->operation == NPFS_COMMAND_INFO ||
        job->operation == NPFS_COMMAND_LOOKUP || job->operation == NPFS_COMMAND_BIND ||
        job->operation == NPFS_COMMAND_VIEW || job->operation == NPFS_ROOT_VIEW ||
        job->operation == NPFS_ROOT_SELECT ? CALL_BAD_OPERATION : CALL_READ_ONLY;
  }
  uint64_t required = job->operation == NPFS_ENUMERATE ? DIRECTORY_RIGHT_ENUMERATE : DIRECTORY_RIGHT_LOOKUP;
  if (!(job->rights & required)) return CALL_DENIED;
  struct package_sdk_index *index = view->index;
  uint64_t set = view->set;
  if (view->live) {
    /* Each operation through the live view reads the set active at that moment. */
    enum call_status status = npfs_package_current(context, node->inode, &set);
    if (status == CALL_OK) status = index_for_set(context, node->inode, set, true, &index);
    if (status != CALL_OK) return status;
  }
  enum call_status status = job->operation == NPFS_ENUMERATE ? enumerate(job, view, index, set) :
      lookup(context, job, view, index, set);
  if (view->live) index_release(index);
  return status;
}

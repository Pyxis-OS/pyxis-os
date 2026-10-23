#include <abi/package_commands.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include "npfs_adapter.h"
#include "package_current.h"

#define COMMAND_DIRECTORY_RIGHTS \
  (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES)

/* records holds count command records followed by root_count root records.
 * A roots view serves only the root records: live over the whole set, or a
 * fixed selection with no command records. */
struct package_command_view {
  size_t references, count, root_count;
  uint64_t set;
  bool live, roots;
  struct package_command_record records[];
};

void npfs_commands_release(struct package_command_view *view)
{
  if (!view) return;
  npfs_require_worker();
  KASSERT(view->references);
  if (!--view->references) {
    npfs_adapter_free(view, sizeof(*view) +
        (view->count + view->root_count) * sizeof(view->records[0]));
  }
}

static bool zero(const void *bytes, size_t size)
{
  const uint8_t *data = bytes;
  for (size_t i = 0; i < size; ++i) if (data[i]) return false;
  return true;
}

static bool string(const char *bytes, size_t size, bool empty)
{
  size_t length = strnlen(bytes, size);
  return length < size && (empty || length) && zero(bytes + length, size - length);
}

static bool alphanumeric(char byte)
{
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
      (byte >= '0' && byte <= '9');
}

static bool package_name(const char *name, bool version)
{
  if (!alphanumeric(*name) || (!version && *name >= 'A' && *name <= 'Z')) return false;
  for (; *name; ++name) {
    if (alphanumeric(*name)) {
      if (!version && *name >= 'A' && *name <= 'Z') return false;
    } else if (*name != '.' && *name != '-' && !(version && *name == '+')) {
      return false;
    }
  }
  return true;
}

static bool component(const char *name, size_t size)
{
  if (!size || (size == 1 && name[0] == '.') ||
      (size == 2 && !memcmp(name, "..", 2))) return false;
  for (size_t i = 0; i < size; ++i) {
    if (!name[i] || name[i] == '/' || name[i] == '\\') return false;
  }
  return true;
}

static bool suffix(const char *name, size_t size, const char *ending)
{
  return size >= 4 && !memcmp(name + size - 4, ending, 4);
}

static bool utf8(const char *text)
{
  const uint8_t *bytes = (const uint8_t *)text;
  while (*bytes) {
    uint32_t value = *bytes++;
    if (value < 0x80) continue;
    unsigned count;
    uint32_t minimum;
    if (value >= 0xc2 && value <= 0xdf) { count = 1; minimum = 0x80; value &= 0x1f; }
    else if (value >= 0xe0 && value <= 0xef) { count = 2; minimum = 0x800; value &= 0x0f; }
    else if (value >= 0xf0 && value <= 0xf4) { count = 3; minimum = 0x10000; value &= 7; }
    else return false;
    for (unsigned i = 0; i < count; ++i) {
      if ((*bytes & 0xc0) != 0x80) return false;
      value = (value << 6) | (*bytes++ & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}

static bool abi_name(const char *name)
{
  if (!*name || !((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9'))) return false;
  for (; *name; ++name) {
    bool lower = (*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9');
    if (!lower && *name != '.' && *name != '-') return false;
  }
  return true;
}

static bool command_name(const char *name)
{
  if (!component(name, strlen(name)) || !utf8(name)) return false;
  for (; *name; ++name) {
    uint8_t byte = (uint8_t)*name;
    if (byte <= 0x20 || byte == 0x7f || byte == ':') return false;
  }
  return true;
}

static bool record_valid(const struct package_command_record *record)
{
  if (!string(record->name, sizeof(record->name), false) ||
      !string(record->id, sizeof(record->id), false) ||
      !string(record->version, sizeof(record->version), false) ||
      !string(record->path, sizeof(record->path), false) ||
      !string(record->command, sizeof(record->command), true) ||
      !string(record->abi, sizeof(record->abi), false) || !abi_name(record->abi) ||
      !zero(record->reserved, sizeof(record->reserved)) ||
      !command_name(record->name) ||
      !package_name(record->id, false) || !package_name(record->version, true) ||
      !utf8(record->path)) return false;
  bool bundle = record->kind == DIRECTORY_KIND_DIRECTORY;
  if (record->kind != DIRECTORY_KIND_FILE && !bundle) return false;
  if (bundle != (record->command[0] != 0) ||
      (bundle && (!command_name(record->command) ||
      memcmp(record->name, record->command, sizeof(record->name))))) return false;
  const char *start = record->path;
  for (const char *end = start;; ++end) {
    if (*end && *end != '/') continue;
    size_t length = (size_t)(end - start);
    if (!component(start, length)) return false;
    /* A complete bundle is selected at its root; no inner-file shortcut. */
    if (*end && suffix(start, length, ".pxb")) return false;
    if (!*end) return suffix(start, length, bundle ? ".pxb" : ".pxe");
    start = end + 1;
  }
}

static bool root_record_valid(const struct package_command_record *record)
{
  return record->kind == DIRECTORY_KIND_DIRECTORY &&
      string(record->name, sizeof(record->name), false) &&
      string(record->id, sizeof(record->id), false) &&
      string(record->version, sizeof(record->version), false) &&
      string(record->path, sizeof(record->path), true) && !record->path[0] &&
      string(record->command, sizeof(record->command), true) && !record->command[0] &&
      string(record->abi, sizeof(record->abi), true) && !record->abi[0] &&
      zero(record->reserved, sizeof(record->reserved)) &&
      package_name(record->id, false) && package_name(record->version, true) &&
      strlen(record->name) == strlen(record->id) &&
      !memcmp(record->name, record->id, strlen(record->id));
}

static enum call_status child(struct npfs_store_context *context, struct npfs_store_inode *parent,
    const char *name, size_t length, uint64_t kind, struct npfs_store_inode **out)
{
  enum call_status status = npfs_store_lookup(context, parent, name, length, out);
  if (status == CALL_OK && npfs_store_kind(*out) != kind) {
    npfs_store_release(*out);
    *out = NULL;
    return CALL_IO;
  }
  return status;
}

static enum call_status read_exact(struct npfs_store_context *context, struct npfs_store_inode *inode,
    uint64_t offset, void *bytes, size_t size)
{
  size_t read;
  enum call_status status = npfs_store_read(context, inode, offset, bytes, size, &read);
  return status == CALL_OK && read != size ? CALL_IO : status;
}

static size_t decimal(uint64_t number, char *bytes)
{
  char reversed[20];
  size_t count = 0;
  do {
    reversed[count++] = (char)('0' + number % 10);
    number /= 10;
  } while (number);
  for (size_t i = 0; i < count; ++i) bytes[i] = reversed[count - i - 1];
  return count;
}

static enum call_status capture(struct npfs_store_context *context, struct npfs_store_inode *root,
    struct package_command_view **out)
{
  *out = NULL;
  uint64_t set = 0;
  struct npfs_store_inode *pointer = NULL;
  enum call_status status = child(context, root, "current", 7, DIRECTORY_KIND_FILE, &pointer);
  if (status == CALL_OK) {
    char bytes[256];
    uint64_t size = npfs_store_size(pointer);
    status = size > sizeof(bytes) ? CALL_IO : read_exact(context, pointer, 0, bytes, (size_t)size);
    npfs_store_release(pointer);
    if (status == CALL_OK && !package_current_parse(bytes, (size_t)size, &set)) status = CALL_IO;
  } else if (status == CALL_NOT_FOUND) {
    status = CALL_OK;
  }
  if (status != CALL_OK) return status;
  struct package_command_index_header header = {0};
  struct npfs_store_inode *index = NULL;
  if (set) {
    struct npfs_store_inode *sets = NULL;
    status = child(context, root, "sets", 4, DIRECTORY_KIND_DIRECTORY, &sets);
    if (status == CALL_OK) {
      char name[32];
      size_t length = decimal(set, name);
      memcpy(name + length, ".commands", 9);
      status = child(context, sets, name, length + 9, DIRECTORY_KIND_FILE, &index);
      npfs_store_release(sets);
    }
    if (status == CALL_NOT_FOUND) status = CALL_IO;
    if (status != CALL_OK) return status;
    /* The view is sized from the set; only the index's byte bound limits it. */
    status = npfs_store_size(index) > PACKAGE_COMMAND_INDEX_MAX_BYTES ? CALL_LIMIT :
        read_exact(context, index, 0, &header, sizeof(header));
    if (status == CALL_OK && (memcmp(header.magic, PACKAGE_COMMAND_INDEX_MAGIC, sizeof(header.magic)) ||
        header.set != set || header.count > PACKAGE_COMMANDS_LIMIT || header.reserved ||
        header.record_size != sizeof(struct package_command_record) ||
        npfs_store_size(index) != sizeof(header) +
            ((uint64_t)header.count + header.roots) * sizeof(struct package_command_record))) {
      status = CALL_IO;
    }
    if (status != CALL_OK) {
      npfs_store_release(index);
      return status;
    }
  }
  size_t total = (size_t)header.count + header.roots;
  struct package_command_view *view;
  status = npfs_adapter_allocate(sizeof(*view) + total * sizeof(view->records[0]), (void **)&view);
  if (status == CALL_OK) {
    *view = (struct package_command_view){.references = 1, .count = header.count,
      .root_count = header.roots, .set = set};
    if (index) status = read_exact(context, index, sizeof(header), view->records,
        total * sizeof(view->records[0]));
    for (size_t i = 0; status == CALL_OK && i < total; ++i) {
      bool root_record = i >= view->count;
      bool first = i == 0 || i == view->count;
      if (!(root_record ? root_record_valid(&view->records[i]) : record_valid(&view->records[i])) ||
          (!first && memcmp(view->records[i - 1].name, view->records[i].name,
              sizeof(view->records[i].name)) >= 0)) {
        status = CALL_IO;
      }
    }
    if (status == CALL_OK) *out = view;
    else npfs_commands_release(view);
  }
  if (index) npfs_store_release(index);
  return status;
}

static void info(struct npfs_node *node, struct package_command_view *view,
    struct package_command_info *out)
{
  struct directory_filesystem_info backing;
  npfs_store_info(node->inode, &backing);
  struct file_info_reply identity;
  npfs_store_file_info(node->inode, &identity);
  *out = (struct package_command_info){.set = view->set, .count = view->count,
      .origin = identity.domain, .flags = view->live ? PACKAGE_COMMAND_VIEW_LIVE : 0,
      .limit = PACKAGE_COMMANDS_LIMIT};
  memcpy(out->pool_id, backing.pool_id, sizeof(out->pool_id));
  memcpy(out->volume_id, backing.volume_id, sizeof(out->volume_id));
}

static enum call_status wrap_view(struct npfs_job *job, struct package_command_view *view,
    uint64_t rights)
{
  struct npfs_node *node;
  enum call_status status = npfs_allocate_node(DIRECTORY_KIND_DIRECTORY, rights, &node);
  if (status != CALL_OK) {
    npfs_commands_release(view);
    return status;
  }
  npfs_store_retain(job->node->inode);
  node->inode = job->node->inode;
  node->commands = view;
  job->object = &node->wrapper.directory.object;
  job->child_rights = rights;
  return CALL_OK;
}

static enum call_status open_target(struct npfs_store_context *context, struct npfs_job *job,
    const struct package_command_record *record)
{
  struct npfs_store_inode *inode;
  enum call_status status = child(context, job->node->inode, "store", 5, DIRECTORY_KIND_DIRECTORY, &inode);
  const char *names[] = {record->id, record->version, "tree", record->path};
  /* A root record's empty path selects the tree itself. */
  size_t count = record->path[0] ? 4 : 3;
  for (size_t i = 0; status == CALL_OK && i < count; ++i) {
    const char *start = names[i];
    for (const char *end = start;; ++end) {
      if (*end && *end != '/') continue;
      struct npfs_store_inode *next = NULL;
      uint64_t kind = i == count - 1 && !*end ? record->kind : DIRECTORY_KIND_DIRECTORY;
      status = child(context, inode, start, (size_t)(end - start), kind, &next);
      npfs_store_release(inode);
      inode = next;
      if (status != CALL_OK || !*end) break;
      start = end + 1;
    }
  }
  if (status != CALL_OK) return status == CALL_NOT_FOUND ? CALL_IO : status;
  uint64_t rights = record->kind == DIRECTORY_KIND_DIRECTORY ?
      job->directory_child_rights : job->child_rights;
  return npfs_wrap_inode(inode, rights, &job->object);
}

static enum call_status lookup(struct npfs_store_context *context, struct npfs_job *job,
    struct package_command_view *view)
{
  bool typed = job->operation == NPFS_COMMAND_LOOKUP || job->kind == DIRECTORY_KIND_ANY;
  if (!component(job->name, job->count) || job->count > PACKAGE_COMMAND_NAME_MAX ||
      (typed && ((job->child_rights & ~FILE_RIGHT_READ) ||
      (job->directory_child_rights & ~COMMAND_DIRECTORY_RIGHTS)))) return CALL_BAD_REQUEST;
  if (!typed && job->kind != DIRECTORY_KIND_FILE && job->kind != DIRECTORY_KIND_DIRECTORY) {
    return CALL_BAD_REQUEST;
  }
  size_t first = view->roots ? view->count : 0;
  size_t end = view->roots ? view->count + view->root_count : view->count;
  for (size_t i = first; i < end; ++i) {
    const struct package_command_record *record = &view->records[i];
    if (strlen(record->name) != job->count || memcmp(record->name, job->name, job->count)) continue;
    if (!typed && job->kind != record->kind) return CALL_WRONG_TYPE;
    uint64_t requested = typed && record->kind == DIRECTORY_KIND_DIRECTORY ?
        job->directory_child_rights : job->child_rights;
    uint64_t allowed = record->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
        (job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0;
    if (requested & ~allowed) return CALL_DENIED;
    /* Ordinary untyped DIRECTORY lookup uses its single child mask. */
    if (!typed && record->kind == DIRECTORY_KIND_DIRECTORY) job->directory_child_rights = requested;
    enum call_status status = open_target(context, job, record);
    if (status == CALL_OK && job->operation == NPFS_COMMAND_LOOKUP) {
      job->command_target = *record;
      info(job->node, view, &job->command_info);
    }
    return status;
  }
  return CALL_NOT_FOUND;
}

static enum call_status enumerate(struct npfs_job *job, struct package_command_view *view)
{
  uint64_t generation = view->set + 1;
  job->entry = (struct directory_enumerate_reply){.cursor = {generation, job->cursor.position}};
  if (job->cursor.generation && job->cursor.generation != generation) {
    job->entry.outcome = DIRECTORY_CHANGED;
    job->entry.cursor.position = 0;
  } else if (job->cursor.position >= (view->roots ? view->root_count : view->count)) {
    job->entry.outcome = DIRECTORY_END;
  } else {
    size_t first = view->roots ? view->count : 0;
    const struct package_command_record *record = &view->records[first + job->cursor.position];
    job->entry.name_size = strlen(record->name) + 1;
    job->entry.kind = record->kind;
    if (job->count < job->entry.name_size) {
      job->entry.outcome = DIRECTORY_BUFFER_TOO_SMALL;
    } else {
      job->entry.outcome = DIRECTORY_ENTRY;
      ++job->entry.cursor.position;
      memcpy(job->name, record->name, job->entry.name_size);
    }
  }
  return CALL_OK;
}

/* A fixed roots view: PREVIOUS's selection plus the root record NAME from the
 * capture VIEW, kept sorted. */
static enum call_status select_root(struct npfs_job *job, const struct package_command_view *view,
    const struct package_command_view *previous)
{
  if (!component(job->name, job->count) || job->count > PACKAGE_COMMAND_ID_MAX) {
    return CALL_BAD_REQUEST;
  }
  const struct package_command_record *found = NULL;
  for (size_t i = view->count; i < view->count + view->root_count && !found; ++i) {
    const struct package_command_record *record = &view->records[i];
    if (strlen(record->name) == job->count && !memcmp(record->name, job->name, job->count)) {
      found = record;
    }
  }
  if (!found) return CALL_NOT_FOUND;
  size_t kept = previous ? previous->root_count : 0;
  for (size_t i = 0; i < kept; ++i) {
    if (!memcmp(previous->records[i].name, found->name, sizeof(found->name))) return CALL_ALREADY_EXISTS;
  }
  struct package_command_view *selection;
  enum call_status status = npfs_adapter_allocate(sizeof(*selection) +
      (kept + 1) * sizeof(selection->records[0]), (void **)&selection);
  if (status != CALL_OK) return status;
  *selection = (struct package_command_view){.references = 1, .root_count = kept + 1,
    .set = view->set, .roots = true};
  size_t at = 0;
  while (at < kept && memcmp(previous->records[at].name, found->name, sizeof(found->name)) < 0) ++at;
  if (at) memcpy(selection->records, previous->records, at * sizeof(selection->records[0]));
  selection->records[at] = *found;
  if (kept > at) memcpy(selection->records + at + 1, previous->records + at,
      (kept - at) * sizeof(selection->records[0]));
  uint64_t rights = job->rights & COMMAND_DIRECTORY_RIGHTS;
  if (previous) rights &= job->destination_rights;
  return wrap_view(job, selection, rights);
}

enum call_status npfs_commands_perform(struct npfs_store_context *context, struct npfs_job *job)
{
  struct npfs_node *node = job->node;
  struct package_command_view *view = node->commands;
  if (node->kind != DIRECTORY_KIND_DIRECTORY) return CALL_WRONG_TYPE;
  if (job->operation == NPFS_COMMAND_VIEW || job->operation == NPFS_ROOT_VIEW) {
    uint64_t required = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_READ_FILES;
    if ((job->rights & required) != required) return CALL_DENIED;
    if (view || !npfs_store_is_root(node->inode)) return CALL_BAD_OPERATION;
    enum call_status status = npfs_adapter_allocate(sizeof(*view), (void **)&view);
    if (status != CALL_OK) return status;
    *view = (struct package_command_view){.references = 1, .live = true,
      .roots = job->operation == NPFS_ROOT_VIEW};
    return wrap_view(job, view, job->rights & COMMAND_DIRECTORY_RIGHTS);
  }
  if (job->operation == NPFS_COMMAND_BIND) {
    uint64_t required = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_READ_FILES;
    if ((job->rights & required) != required) return CALL_DENIED;
    if (view || !npfs_store_is_root(node->inode)) return CALL_BAD_OPERATION;
    struct npfs_node *selected = job->destination;
    if (!selected || !selected->commands || selected->commands->live || selected->commands->roots ||
        !npfs_store_same_volume(node->inode, selected->inode)) return CALL_BAD_REQUEST;
    /* Only the independently supplied root conveys directory authority. The
     * selected capture verifies its origin and contributes the caller lineage. */
    job->child_rights = job->rights & COMMAND_DIRECTORY_RIGHTS;
    npfs_store_retain(node->inode);
    return npfs_wrap_inode(node->inode, job->child_rights, &job->object);
  }
  if (!view) return CALL_BAD_OPERATION;
  if (job->operation == NPFS_ROOT_SELECT) {
    if (!(job->rights & DIRECTORY_RIGHT_LOOKUP)) return CALL_DENIED;
    if (view->live || view->roots) return CALL_BAD_OPERATION;
    struct npfs_node *previous = job->destination;
    if (previous && (!previous->commands || previous->commands->live ||
        !previous->commands->roots || previous->commands->set != view->set ||
        !npfs_store_same_volume(node->inode, previous->inode))) return CALL_BAD_REQUEST;
    return select_root(job, view, previous ? previous->commands : NULL);
  }
  /* A roots view is only a directory of package trees. */
  if (view->roots && job->operation != NPFS_LOOKUP && job->operation != NPFS_ENUMERATE &&
      job->operation != NPFS_INFO) {
    return job->operation == NPFS_FILESYSTEM_INFO || job->operation == NPFS_COMMAND_INFO ||
        job->operation == NPFS_COMMAND_CAPTURE || job->operation == NPFS_COMMAND_LOOKUP ?
        CALL_BAD_OPERATION : CALL_READ_ONLY;
  }
  if (job->operation == NPFS_COMMAND_INFO) {
    info(node, view, &job->command_info);
    return CALL_OK;
  }
  if (job->operation == NPFS_INFO) {
    job->file_info = (struct file_info_reply){0};
    return CALL_OK;
  }
  if (job->operation == NPFS_FILESYSTEM_INFO) return CALL_BAD_OPERATION;
  uint64_t required = job->operation == NPFS_ENUMERATE ? DIRECTORY_RIGHT_ENUMERATE : DIRECTORY_RIGHT_LOOKUP;
  if (!(job->rights & required)) return CALL_DENIED;
  if (job->operation == NPFS_COMMAND_CAPTURE) {
    uint64_t rights = job->rights & COMMAND_DIRECTORY_RIGHTS;
    if (job->destination) {
      struct npfs_node *previous = job->destination;
      if (!previous->commands || previous->commands->live || previous->commands->roots ||
          !npfs_store_same_volume(node->inode, previous->inode)) return CALL_BAD_REQUEST;
      if (!view->live && view->set != previous->commands->set) return CALL_BAD_REQUEST;
      view = previous->commands;
      rights &= job->destination_rights;
      if (view->references == SIZE_MAX) return CALL_LIMIT;
      ++view->references;
    } else if (view->live) {
      enum call_status status = capture(context, node->inode, &view);
      if (status != CALL_OK) return status;
    } else {
      if (view->references == SIZE_MAX) return CALL_LIMIT;
      ++view->references;
    }
    return wrap_view(job, view, rights);
  }
  if (job->operation != NPFS_LOOKUP && job->operation != NPFS_COMMAND_LOOKUP &&
      job->operation != NPFS_ENUMERATE) return CALL_READ_ONLY;
  if (job->operation == NPFS_COMMAND_LOOKUP && view->live) return CALL_BAD_OPERATION;
  bool temporary = view->live;
  if (temporary) {
    /* Each lookup through a live view reads the set active at that moment. */
    bool roots = view->roots;
    enum call_status status = capture(context, node->inode, &view);
    if (status != CALL_OK) return status;
    view->roots = roots;
  }
  enum call_status status = job->operation == NPFS_ENUMERATE ? enumerate(job, view) :
      lookup(context, job, view);
  if (temporary) npfs_commands_release(view);
  return status;
}

#include <arch/smp.h>
#include <kernel/fs/initrd_tree.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>

static struct directory_entry *find_entry(struct directory_object *directory,
                                          const char *name, size_t length)
{
  for (struct directory_entry *entry = directory->first; entry; entry = entry->next) {
    if (entry->name_length == length && !memcmp(entry->name, name, length)) {
      return entry;
    }
  }
  return NULL;
}

static enum initrd_result add_entry(struct directory_object *directory,
    const char *name, size_t length, const struct initrd_file *file,
    struct directory_entry **result)
{
  if (length > SIZE_MAX - sizeof(struct directory_entry) - 1 ||
      directory->entry_count == SIZE_MAX) {
    return INITRD_INVALID;
  }
  struct directory_entry *entry = kmalloc(sizeof(*entry) + length + 1);
  if (!entry) {
    return INITRD_NO_MEMORY;
  }

  struct kernel_object *object;
  if (file) {
    struct file_object *child = file_create_initrd(file);
    object = child ? &child->object : NULL;
  } else {
    struct directory_object *child = directory_create(DIRECTORY_INITRD);
    object = child ? &child->object : NULL;
  }
  if (!object) {
    kfree(entry);
    return INITRD_NO_MEMORY;
  }
  *entry = (struct directory_entry){.object = object, .name_length = length};
  memcpy(entry->name, name, length);
  entry->name[length] = 0;

  if (directory->last) {
    directory->last->next = entry;
  } else {
    directory->first = entry;
  }
  directory->last = entry;
  ++directory->entry_count;
  *result = entry;
  return INITRD_OK;
}

static enum initrd_result insert_path(struct directory_object *root,
                                       const struct initrd_entry *record)
{
  const char *path = record->name;
  size_t length = record->name_length;
  if (!length || path[0] == '/') {
    return INITRD_INVALID;
  }
  while (length >= 2 && path[0] == '.' && path[1] == '/') {
    path += 2;
    length -= 2;
  }
  if ((!length || (length == 1 && path[0] == '.')) && record->directory) {
    return INITRD_OK; /* Explicit archive root. */
  }
  if (length && path[length - 1] == '/' && record->directory) {
    --length;
  }
  if (!length) {
    return INITRD_INVALID;
  }

  struct directory_object *directory = root;
  size_t offset = 0;
  while (offset < length) {
    size_t start = offset;
    while (offset < length && path[offset] != '/') {
      ++offset;
    }
    size_t component = offset - start;
    if (!component || (component == 1 && path[start] == '.') ||
        (component == 2 && path[start] == '.' && path[start + 1] == '.')) {
      return INITRD_INVALID;
    }
    bool leaf = offset == length;
    bool want_directory = !leaf || record->directory;
    struct directory_entry *entry = find_entry(directory, path + start, component);
    if (entry) {
      if (!want_directory || entry->object->type != OBJECT_DIRECTORY) {
        return INITRD_INVALID;
      }
    } else {
      enum initrd_result result = add_entry(directory, path + start, component,
          want_directory ? NULL : &record->file, &entry);
      if (result != INITRD_OK) {
        return result;
      }
    }
    if (leaf) {
      return INITRD_OK;
    }
    directory = (struct directory_object *)entry->object;
    ++offset;
  }
  return INITRD_INVALID;
}

enum initrd_result initrd_tree_create(struct directory_object **root)
{
  KASSERT(arch_cpu_index() == 0);
  if (!root) {
    return INITRD_INVALID;
  }
  *root = NULL;
  struct directory_object *directory = directory_create(DIRECTORY_INITRD);
  if (!directory) {
    return INITRD_NO_MEMORY;
  }

  size_t offset = 0;
  struct initrd_entry entry;
  enum initrd_result result;
  while ((result = initrd_next(&offset, &entry)) == INITRD_OK) {
    result = insert_path(directory, &entry);
    if (result != INITRD_OK) {
      break;
    }
  }
  if (result != INITRD_END) {
    object_release(&directory->object);
    return result;
  }
  *root = directory;
  return INITRD_OK;
}

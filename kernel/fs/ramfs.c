#include <abi/directory.h>
#include <arch/smp.h>
#include <kernel/fs/ramfs.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>

struct directory_entry *ramfs_allocate_entry(uint64_t kind, size_t name_length)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(kind == DIRECTORY_KIND_DIRECTORY || kind == DIRECTORY_KIND_FILE);
  KASSERT(name_length && name_length <= SIZE_MAX - sizeof(struct directory_entry) - 1);
  struct directory_entry *entry = kmalloc(sizeof(*entry) + name_length + 1);
  if (!entry) {
    return NULL;
  }

  struct kernel_object *object;
  if (kind == DIRECTORY_KIND_DIRECTORY) {
    struct directory_object *directory = directory_create(DIRECTORY_RAM);
    object = directory ? &directory->object : NULL;
  } else {
    struct file_object *file = file_create_ram();
    object = file ? &file->object : NULL;
  }
  if (!object) {
    kfree(entry);
    return NULL;
  }
  *entry = (struct directory_entry){.object = object, .name_length = name_length};
  return entry;
}

void ramfs_discard_entry(struct directory_entry *entry)
{
  KASSERT(arch_cpu_index() == 0 && entry && !entry->next);
  object_release(entry->object);
  kfree(entry);
}

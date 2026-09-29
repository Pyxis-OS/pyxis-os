#include <abi/directory.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/fs/ramfs.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>

static struct directory_entry *ramfs_allocate_name(size_t name_length)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(name_length && name_length <= SIZE_MAX - sizeof(struct directory_entry) - 1);
  struct directory_entry *entry = kmalloc(sizeof(*entry) + name_length + 1);
  if (!entry) {
    return NULL;
  }
  *entry = (struct directory_entry){.name_length = name_length};
  return entry;
}

static struct directory_entry *ramfs_allocate_entry(uint64_t kind, size_t name_length)
{
  KASSERT(kind == DIRECTORY_KIND_DIRECTORY || kind == DIRECTORY_KIND_FILE);
  struct directory_entry *entry = ramfs_allocate_name(name_length);
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
  entry->object = object;
  return entry;
}

static void ramfs_discard_entry(struct directory_entry *entry)
{
  KASSERT(arch_cpu_index() == 0 && entry && !entry->next);
  if (entry->object) {
    object_release(entry->object);
  }
  kfree(entry);
}

void ramfs_request_execute(struct ramfs_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  switch (request->operation) {
  case RAMFS_ALLOCATE_ENTRY:
    request->entry = ramfs_allocate_entry(request->kind, request->name_length);
    break;
  case RAMFS_ALLOCATE_NAME:
    request->entry = ramfs_allocate_name(request->name_length);
    break;
  case RAMFS_DISCARD:
    ramfs_discard_entry(request->entry);
    request->entry = NULL;
    break;
  default:
    KASSERT(false);
  }
}

static struct directory_entry *request_allocation(enum ramfs_request_operation operation,
    uint64_t kind, size_t name_length)
{
  struct ramfs_request *request = (struct ramfs_request *)bsp_request_prepare(BSP_SERVICE_RAMFS);
  request->operation = operation;
  request->kind = kind;
  request->name_length = name_length;
  request->entry = NULL;
  bsp_request_submit_and_wait(&request->request);
  struct directory_entry *entry = request->entry;
  request->entry = NULL;
  bsp_request_release(&request->request);
  return entry;
}

struct directory_entry *ramfs_request_entry(uint64_t kind, size_t name_length)
{
  return request_allocation(RAMFS_ALLOCATE_ENTRY, kind, name_length);
}

struct directory_entry *ramfs_request_name(size_t name_length)
{
  return request_allocation(RAMFS_ALLOCATE_NAME, 0, name_length);
}

void ramfs_request_discard(struct directory_entry *entry)
{
  KASSERT(entry && !entry->next);
  struct ramfs_request *request = (struct ramfs_request *)bsp_request_prepare(BSP_SERVICE_RAMFS);
  request->operation = RAMFS_DISCARD;
  request->entry = entry;
  bsp_request_submit_and_wait(&request->request);
  bsp_request_release(&request->request);
}

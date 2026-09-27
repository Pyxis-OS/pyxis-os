#include <arch/smp.h>
#include <kernel/object/capability.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/object.h>
#include <kernel/panic.h>

#define INITIAL_CAPACITY 8
#define HANDLE_INDEX_BITS 32
#define HANDLE_INDEX_MASK UINT32_MAX
#define HANDLE_SLOT_COUNT (UINT64_C(1) << HANDLE_INDEX_BITS)

struct capability_entry {
  struct kernel_object *object;
  uint64_t rights;
  uint32_t generation; /* Zero retires the slot permanently after wrap. */
};

enum capability_result capability_grow(struct capability_table *table)
{
  KASSERT(arch_cpu_index() == 0 && table);
  size_t limit = SIZE_MAX / sizeof(*table->entries);
  if (limit > HANDLE_SLOT_COUNT) {
    limit = HANDLE_SLOT_COUNT;
  }
  if (table->capacity == limit) {
    return CAP_LIMIT;
  }

  size_t capacity = table->capacity ? table->capacity : INITIAL_CAPACITY;
  if (table->capacity) {
    capacity = table->capacity > limit / 2 ? limit : table->capacity * 2;
  }
  struct capability_entry *entries = kmalloc(capacity * sizeof(*entries));
  if (!entries) {
    return CAP_NO_MEMORY;
  }

  if (table->capacity) {
    memcpy(entries, table->entries, table->capacity * sizeof(*entries));
  }
  for (size_t i = table->capacity; i < capacity; ++i) {
    entries[i] = (struct capability_entry){.generation = 1};
  }
  kfree(table->entries);
  table->entries = entries;
  table->capacity = capacity;
  return CAP_OK;
}

enum capability_result capability_insert(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, handle_t *handle)
{
  if (handle) {
    *handle = HANDLE_INVALID;
  }
  if (!table || !object || !handle || !object_rights_valid(object->type, rights)) {
    return CAP_INVALID;
  }

  size_t index;
  for (index = 0; index < table->capacity; ++index) {
    if (!table->entries[index].object && table->entries[index].generation) {
      break;
    }
  }
  if (index == table->capacity) {
    return CAP_FULL;
  }
  if (!object_retain(object)) {
    return CAP_LIMIT;
  }

  struct capability_entry *entry = &table->entries[index];
  entry->object = object;
  entry->rights = rights;
  *handle = ((uint64_t)entry->generation << HANDLE_INDEX_BITS) | index;
  return CAP_OK;
}

size_t capability_free_slots(const struct capability_table *table)
{
  if (!table) {
    return 0;
  }

  size_t free_slots = 0;
  for (size_t i = 0; i < table->capacity; ++i) {
    if (!table->entries[i].object && table->entries[i].generation) {
      ++free_slots;
    }
  }
  return free_slots;
}

enum capability_result capability_insert_batch(struct capability_table *table,
    struct kernel_object *const *objects, const uint64_t *rights, size_t count,
    handle_t *handles)
{
  if (count > CAPABILITY_BATCH_MAX) {
    return CAP_INVALID;
  }
  if (handles) {
    for (size_t i = 0; i < count; ++i) {
      handles[i] = HANDLE_INVALID;
    }
  }
  if (!table || (count && (!objects || !rights || !handles))) {
    return CAP_INVALID;
  }

  for (size_t i = 0; i < count; ++i) {
    if (!objects[i] || !object_rights_valid(objects[i]->type, rights[i])) {
      return CAP_INVALID;
    }
  }

  size_t slots[CAPABILITY_BATCH_MAX];
  size_t selected = 0;
  for (size_t i = 0; i < table->capacity && selected < count; ++i) {
    if (!table->entries[i].object && table->entries[i].generation) {
      slots[selected++] = i;
    }
  }
  if (selected != count) {
    return CAP_FULL;
  }

  size_t retained = 0;
  for (; retained < count; ++retained) {
    if (!object_retain(objects[retained])) {
      for (size_t i = 0; i < retained; ++i) {
        object_release(objects[i]);
      }
      return CAP_LIMIT;
    }
  }

  for (size_t i = 0; i < count; ++i) {
    struct capability_entry *entry = &table->entries[slots[i]];
    entry->object = objects[i];
    entry->rights = rights[i];
    handles[i] = ((uint64_t)entry->generation << HANDLE_INDEX_BITS) | slots[i];
  }
  return CAP_OK;
}

enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, handle_t *handle)
{
  KASSERT(arch_cpu_index() == 0);
  enum capability_result result = capability_insert(table, object, rights, handle);
  if (result != CAP_FULL) {
    return result;
  }
  result = capability_grow(table);
  if (result != CAP_OK) {
    return result;
  }
  return capability_insert(table, object, rights, handle);
}

static struct capability_entry *find_entry(struct capability_table *table,
                                            handle_t handle)
{
  size_t index = handle & HANDLE_INDEX_MASK;
  uint32_t generation = handle >> HANDLE_INDEX_BITS;
  if (!table || !generation || index >= table->capacity) {
    return NULL;
  }

  struct capability_entry *entry = &table->entries[index];
  if (!entry->object || entry->generation != generation) {
    return NULL;
  }
  return entry;
}

enum capability_result capability_resolve(struct capability_table *table,
    handle_t handle, uint64_t required_rights, struct kernel_object **object,
    uint64_t *rights)
{
  if (rights) {
    *rights = 0;
  }
  if (!object) {
    return CAP_INVALID;
  }
  *object = NULL;

  struct capability_entry *entry = find_entry(table, handle);
  if (!entry) {
    return CAP_BAD_HANDLE;
  }
  if ((entry->rights & required_rights) != required_rights) {
    return CAP_DENIED;
  }
  *object = entry->object;
  if (rights) {
    *rights = entry->rights;
  }
  return CAP_OK;
}

enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle)
{
  struct capability_entry *entry = find_entry(table, handle);
  if (!entry) {
    return CAP_BAD_HANDLE;
  }

  struct kernel_object *object = entry->object;
  entry->object = NULL;
  entry->rights = 0;
  /* Unsigned wrap gives zero, which install skips rather than resurrecting
   * any handle previously issued for this slot. */
  ++entry->generation;
  object_release(object);
  return CAP_OK;
}

enum capability_result capability_grant(struct capability_table *destination,
    struct capability_table *source, handle_t handle, uint64_t rights,
    handle_t *result)
{
  KASSERT(arch_cpu_index() == 0);
  if (!result) {
    return CAP_INVALID;
  }
  *result = HANDLE_INVALID;
  struct kernel_object *object;
  enum capability_result status = capability_resolve(source, handle, rights,
      &object, NULL);
  if (status != CAP_OK) {
    return status;
  }
  if (object->type == OBJECT_ENDPOINT_RECEIPT ||
      object->type == OBJECT_ENDPOINT_RECEIVER) {
    return CAP_DENIED;
  }
  return capability_install(destination, object, rights, result);
}

void capability_table_destroy(struct capability_table *table)
{
  KASSERT(arch_cpu_index() == 0);
  for (size_t i = 0; i < table->capacity; ++i) {
    if (table->entries[i].object) {
      object_release(table->entries[i].object);
    }
  }
  kfree(table->entries);
  *table = (struct capability_table){0};
}

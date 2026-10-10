#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/object/capability.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/object.h>
#include <kernel/object/launcher.h>
#include <kernel/object/endpoint.h>
#include <kernel/panic.h>
#include <kernel/process.h>

#define INITIAL_CAPACITY 8
#define HANDLE_INDEX_BITS 32
#define HANDLE_INDEX_MASK UINT32_MAX
#define HANDLE_SLOT_COUNT (UINT64_C(1) << HANDLE_INDEX_BITS)

struct capability_entry {
  struct kernel_object *object;
  uint64_t rights;
  uint64_t transport;
  uint32_t generation; /* Zero retires the slot permanently after wrap. */
};

enum capability_result capability_grow(struct capability_table *table)
{
  KASSERT(arch_cpu_index() == 0 && table);
  spin_lock(&table->lock);
  size_t previous_capacity = table->capacity;
  spin_unlock(&table->lock);
  size_t limit = SIZE_MAX / sizeof(*table->entries);
  if (limit > HANDLE_SLOT_COUNT) {
    limit = HANDLE_SLOT_COUNT;
  }
  if (previous_capacity == limit) {
    return CAP_LIMIT;
  }

  size_t capacity = previous_capacity ? previous_capacity : INITIAL_CAPACITY;
  if (previous_capacity) {
    capacity = previous_capacity > limit / 2 ? limit : previous_capacity * 2;
  }
  struct capability_entry *entries = kmalloc(capacity * sizeof(*entries));
  if (!entries) {
    return CAP_NO_MEMORY;
  }

  for (size_t i = previous_capacity; i < capacity; ++i) {
    entries[i] = (struct capability_entry){.generation = 1};
  }
  spin_lock(&table->lock);
  /* The existing exclusive BSP loan still covers allocation and publication. */
  KASSERT(table->capacity == previous_capacity);
  struct capability_entry *previous_entries = table->entries;
  if (previous_capacity) {
    memcpy(entries, previous_entries, previous_capacity * sizeof(*entries));
  }
  table->entries = entries;
  table->capacity = capacity;
  spin_unlock(&table->lock);
  kfree(previous_entries);
  return CAP_OK;
}

void capability_growth_execute(struct capability_growth_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->table);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  request->result = capability_grow(request->table);
  request->table = NULL;
}

enum capability_result capability_request_growth(void)
{
  struct capability_growth_request *request =
      (struct capability_growth_request *)bsp_request_prepare(BSP_SERVICE_CAPABILITY_GROW);
  struct process *process = process_current();
  KASSERT(process);
  request->table = &process->capabilities;
  bsp_request_submit_and_wait(&request->request);
  enum capability_result result = request->result;
  bsp_request_release(&request->request);
  return result;
}

enum capability_result capability_insert(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle)
{
  if (handle) {
    *handle = HANDLE_INVALID;
  }
  if (!table || !object || !handle ||
      !object_authority_valid(object, rights, transport)) {
    return CAP_INVALID;
  }
  if (table->execution_group && object->type == OBJECT_LAUNCHER &&
      launcher_execution_group(object) != table->execution_group) {
    return CAP_DENIED;
  }

  spin_lock(&table->lock);
  size_t index;
  for (index = 0; index < table->capacity; ++index) {
    if (!table->entries[index].object && table->entries[index].generation) {
      break;
    }
  }
  bool full = index == table->capacity;
  spin_unlock(&table->lock);
  if (full) {
    return CAP_FULL;
  }
  if (!object_grant_retain(object, rights)) {
    return CAP_LIMIT;
  }

  spin_lock(&table->lock);
  struct capability_entry *entry = &table->entries[index];
  KASSERT(!entry->object && entry->generation);
  entry->object = object;
  entry->rights = rights;
  entry->transport = transport;
  *handle = ((uint64_t)entry->generation << HANDLE_INDEX_BITS) | index;
  spin_unlock(&table->lock);
  return CAP_OK;
}

size_t capability_free_slots(struct capability_table *table)
{
  if (!table) {
    return 0;
  }

  spin_lock(&table->lock);
  size_t free_slots = 0;
  for (size_t i = 0; i < table->capacity; ++i) {
    if (!table->entries[i].object && table->entries[i].generation) {
      ++free_slots;
    }
  }
  spin_unlock(&table->lock);
  return free_slots;
}

enum capability_result capability_insert_batch(struct capability_table *table,
    struct kernel_object *const *objects, const uint64_t *rights,
    const uint64_t *transport, size_t count, handle_t *handles)
{
  if (count > CAPABILITY_BATCH_MAX) {
    return CAP_INVALID;
  }
  if (handles) {
    for (size_t i = 0; i < count; ++i) {
      handles[i] = HANDLE_INVALID;
    }
  }
  if (!table || (count && (!objects || !rights || !transport || !handles))) {
    return CAP_INVALID;
  }

  for (size_t i = 0; i < count; ++i) {
    if (!objects[i] || !object_authority_valid(objects[i], rights[i], transport[i])) {
      return CAP_INVALID;
    }
    if (table->execution_group && objects[i]->type == OBJECT_LAUNCHER &&
        launcher_execution_group(objects[i]) != table->execution_group) {
      return CAP_DENIED;
    }
  }

  spin_lock(&table->lock);
  size_t slots[CAPABILITY_BATCH_MAX];
  size_t selected = 0;
  for (size_t i = 0; i < table->capacity && selected < count; ++i) {
    if (!table->entries[i].object && table->entries[i].generation) {
      slots[selected++] = i;
    }
  }
  spin_unlock(&table->lock);
  if (selected != count) {
    return CAP_FULL;
  }

  size_t retained = 0;
  for (; retained < count; ++retained) {
    if (!object_grant_retain(objects[retained], rights[retained])) {
      for (size_t i = 0; i < retained; ++i) {
        object_grant_release(objects[i], rights[i]);
      }
      return CAP_LIMIT;
    }
  }

  spin_lock(&table->lock);
  for (size_t i = 0; i < count; ++i) {
    struct capability_entry *entry = &table->entries[slots[i]];
    KASSERT(!entry->object && entry->generation);
    entry->object = objects[i];
    entry->rights = rights[i];
    entry->transport = transport[i];
    handles[i] = ((uint64_t)entry->generation << HANDLE_INDEX_BITS) | slots[i];
  }
  spin_unlock(&table->lock);
  return CAP_OK;
}

enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle)
{
  KASSERT(arch_cpu_index() == 0);
  enum capability_result result = capability_insert(table, object, rights,
      transport, handle);
  if (result != CAP_FULL) {
    return result;
  }
  result = capability_grow(table);
  if (result != CAP_OK) {
    return result;
  }
  return capability_insert(table, object, rights, transport, handle);
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

enum capability_result capability_acquire(struct capability_table *table,
    handle_t handle, uint64_t required_rights, uint64_t required_transport,
    struct capability_reference *reference)
{
  if (!reference) {
    return CAP_INVALID;
  }
  *reference = (struct capability_reference){0};
  if (!table) {
    return CAP_BAD_HANDLE;
  }
  spin_lock(&table->lock);
  struct capability_entry *entry = find_entry(table, handle);
  enum capability_result result = CAP_OK;
  if (!entry) {
    result = CAP_BAD_HANDLE;
  } else if ((entry->rights & required_rights) != required_rights ||
      (entry->transport & required_transport) != required_transport) {
    result = CAP_DENIED;
  } else if (!object_retain(entry->object)) {
    result = CAP_LIMIT;
  } else {
    *reference = (struct capability_reference){entry->object, entry->rights, entry->transport};
  }
  spin_unlock(&table->lock);
  return result;
}

void capability_release(struct capability_reference *reference)
{
  struct kernel_object *object = reference->object;
  *reference = (struct capability_reference){0};
  if (object) {
    object_release(object);
  }
}

static void release_closed_grant(struct kernel_object *object, uint64_t rights)
{
  endpoint_handle_close(object);
  object_grant_release(object, rights);
}

enum capability_result capability_close(struct capability_table *table,
                                         handle_t handle)
{
  if (!table) {
    return CAP_BAD_HANDLE;
  }
  spin_lock(&table->lock);
  struct capability_entry *entry = find_entry(table, handle);
  if (!entry) {
    spin_unlock(&table->lock);
    return CAP_BAD_HANDLE;
  }

  struct kernel_object *object = entry->object;
  uint64_t rights = entry->rights;
  entry->object = NULL;
  entry->rights = 0;
  entry->transport = 0;
  /* Unsigned wrap gives zero, which install skips rather than resurrecting
   * any handle previously issued for this slot. */
  ++entry->generation;
  spin_unlock(&table->lock);
  release_closed_grant(object, rights);
  return CAP_OK;
}

enum capability_result capability_grant(struct capability_table *destination,
    struct capability_table *source, handle_t handle, uint64_t rights,
    uint64_t transport, handle_t *result)
{
  KASSERT(arch_cpu_index() == 0);
  if (!result) {
    return CAP_INVALID;
  }
  *result = HANDLE_INVALID;
  struct capability_reference reference;
  enum capability_result status = capability_acquire(source, handle, rights,
      transport, &reference);
  if (status != CAP_OK) {
    return status;
  }
  if (reference.object->type == OBJECT_ENDPOINT_RECEIPT ||
      reference.object->type == OBJECT_ENDPOINT_RECEIVER) {
    status = CAP_DENIED;
  } else {
    status = capability_install(destination, reference.object, rights, transport, result);
  }
  capability_release(&reference);
  return status;
}

void capability_table_destroy(struct capability_table *table)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&table->lock);
  struct capability_entry *entries = table->entries;
  size_t capacity = table->capacity;
  table->entries = NULL;
  table->capacity = 0;
  table->execution_group = NULL;
  spin_unlock(&table->lock);
  for (size_t i = 0; i < capacity; ++i) {
    if (entries[i].object) {
      release_closed_grant(entries[i].object, entries[i].rights);
    }
  }
  kfree(entries);
}

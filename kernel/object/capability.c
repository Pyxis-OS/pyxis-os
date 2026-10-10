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
  bool reserved;
};

static size_t capacity_limit(void)
{
  size_t limit = SIZE_MAX / sizeof(struct capability_entry);
  if (limit > HANDLE_SLOT_COUNT) {
    limit = HANDLE_SLOT_COUNT;
  }
  return limit;
}

enum capability_result capability_grow(struct capability_table *table)
{
  KASSERT(arch_cpu_index() == 0 && table);
  size_t limit = capacity_limit();
  for (;;) {
    spin_lock(&table->lock);
    size_t previous_capacity = table->capacity;
    spin_unlock(&table->lock);
    if (previous_capacity == limit) {
      return CAP_LIMIT;
    }
    size_t capacity = previous_capacity ?
        (previous_capacity > limit / 2 ? limit : previous_capacity * 2) :
        INITIAL_CAPACITY;
    struct capability_entry *entries = kmalloc(capacity * sizeof(*entries));
    if (!entries) {
      return CAP_NO_MEMORY;
    }
    for (size_t i = previous_capacity; i < capacity; ++i) {
      entries[i] = (struct capability_entry){.generation = 1};
    }
    spin_lock(&table->lock);
    if (table->capacity != previous_capacity) {
      spin_unlock(&table->lock);
      kfree(entries);
      continue;
    }
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

enum capability_result capability_reserve(struct capability_table *table,
    size_t count, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots)
{
  if (!reservation) {
    return CAP_INVALID;
  }
  *reservation = (struct capability_reservation){0};
  if (!table || (count && !slots)) {
    return CAP_INVALID;
  }
  if (count > capacity_limit()) {
    return CAP_LIMIT;
  }
  spin_lock(&table->lock);
  size_t selected = 0;
  for (size_t i = 0; i < table->capacity && selected < count; ++i) {
    struct capability_entry *entry = &table->entries[i];
    if (!entry->object && !entry->reserved && entry->generation) {
      slots[selected++] = (struct capability_reserved_slot){i, entry->generation};
    }
  }
  if (selected != count) {
    spin_unlock(&table->lock);
    return CAP_FULL;
  }
  for (size_t i = 0; i < count; ++i) {
    table->entries[slots[i].index].reserved = true;
  }
  table->reserved += count;
  *reservation = (struct capability_reservation){table, count};
  spin_unlock(&table->lock);
  return CAP_OK;
}

static void check_reserved_slots(struct capability_table *table,
    const struct capability_reserved_slot *slots, size_t count)
{
  KASSERT(table->reserved >= count);
  for (size_t i = 0; i < count; ++i) {
    KASSERT(slots[i].index < table->capacity);
    struct capability_entry *entry = &table->entries[slots[i].index];
    KASSERT(entry->reserved && !entry->object &&
        entry->generation == slots[i].generation);
  }
}

void capability_reservation_release(struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots)
{
  KASSERT(reservation);
  if (!reservation->table) {
    KASSERT(!reservation->count);
    return;
  }
  struct capability_table *table = reservation->table;
  spin_lock(&table->lock);
  check_reserved_slots(table, slots, reservation->count);
  for (size_t i = 0; i < reservation->count; ++i) {
    table->entries[slots[i].index].reserved = false;
  }
  table->reserved -= reservation->count;
  *reservation = (struct capability_reservation){0};
  spin_unlock(&table->lock);
}

enum capability_result capability_request_reservation(size_t count,
    struct capability_reservation *reservation,
    struct capability_reserved_slot *slots)
{
  struct process *process = process_current();
  KASSERT(process);
  for (;;) {
    enum capability_result result = capability_reserve(&process->capabilities,
        count, reservation, slots);
    if (result != CAP_FULL) {
      return result;
    }
    result = capability_request_growth();
    if (result != CAP_OK) {
      return result;
    }
  }
}

enum capability_result capability_grant_retain(struct kernel_object *object,
    uint64_t rights, uint64_t transport, struct capability_grant *grant)
{
  if (!grant) {
    return CAP_INVALID;
  }
  *grant = (struct capability_grant){0};
  if (!object || !object_authority_valid(object, rights, transport)) {
    return CAP_INVALID;
  }
  if (!object_grant_retain(object, rights)) {
    return CAP_LIMIT;
  }
  *grant = (struct capability_grant){object, rights, transport};
  return CAP_OK;
}

void capability_grant_release(struct capability_grant *grant)
{
  struct kernel_object *object = grant->object;
  uint64_t rights = grant->rights;
  *grant = (struct capability_grant){0};
  if (object) {
    object_grant_release(object, rights);
  }
}

enum capability_result capability_validate_grants(const struct capability_table *table,
    const struct capability_grant *grants, size_t count)
{
  if (!table || (count && !grants)) {
    return CAP_INVALID;
  }
  for (size_t i = 0; i < count; ++i) {
    struct kernel_object *object = grants[i].object;
    if (!object || !object_authority_valid(object, grants[i].rights, grants[i].transport)) {
      return CAP_INVALID;
    }
    if (table->execution_group && object->type == OBJECT_LAUNCHER &&
        launcher_execution_group(object) != table->execution_group) {
      return CAP_DENIED;
    }
  }
  return CAP_OK;
}

void capability_install_reserved(struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots, struct capability_grant *grants,
    size_t count, handle_t *handles)
{
  KASSERT(reservation && reservation->table && count <= reservation->count);
  KASSERT(!count || (grants && handles));
  struct capability_table *table = reservation->table;
  spin_lock(&table->lock);
  check_reserved_slots(table, slots, reservation->count);
  for (size_t i = 0; i < count; ++i) {
    struct capability_entry *entry = &table->entries[slots[i].index];
    KASSERT(grants[i].object);
    entry->object = grants[i].object;
    entry->rights = grants[i].rights;
    entry->transport = grants[i].transport;
    handles[i] = ((uint64_t)entry->generation << HANDLE_INDEX_BITS) | slots[i].index;
    grants[i] = (struct capability_grant){0};
  }
  for (size_t i = 0; i < reservation->count; ++i) {
    table->entries[slots[i].index].reserved = false;
  }
  table->reserved -= reservation->count;
  *reservation = (struct capability_reservation){0};
  spin_unlock(&table->lock);
}

enum capability_result capability_insert(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle)
{
  return capability_insert_batch(table, &object, &rights, &transport, 1, handle);
}

size_t capability_free_slots(struct capability_table *table)
{
  if (!table) {
    return 0;
  }

  spin_lock(&table->lock);
  size_t free_slots = 0;
  for (size_t i = 0; i < table->capacity; ++i) {
    if (!table->entries[i].object && !table->entries[i].reserved &&
        table->entries[i].generation) {
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

  struct capability_grant grants[CAPABILITY_BATCH_MAX];
  for (size_t i = 0; i < count; ++i) {
    grants[i] = (struct capability_grant){objects[i], rights[i], transport[i]};
  }
  enum capability_result result = capability_validate_grants(table, grants, count);
  if (result != CAP_OK) {
    return result;
  }
  struct capability_reserved_slot slots[CAPABILITY_BATCH_MAX];
  struct capability_reservation reservation;
  result = capability_reserve(table, count, &reservation, slots);
  if (result != CAP_OK) {
    return result;
  }
  size_t retained = 0;
  for (; retained < count; ++retained) {
    if (!object_grant_retain(objects[retained], rights[retained])) {
      for (size_t i = 0; i < retained; ++i) {
        capability_grant_release(&grants[i]);
      }
      capability_reservation_release(&reservation, slots);
      return CAP_LIMIT;
    }
  }
  capability_install_reserved(&reservation, slots, grants, count, handles);
  return CAP_OK;
}

enum capability_result capability_install(struct capability_table *table,
    struct kernel_object *object, uint64_t rights, uint64_t transport,
    handle_t *handle)
{
  KASSERT(arch_cpu_index() == 0);
  for (;;) {
    enum capability_result result = capability_insert(table, object, rights,
        transport, handle);
    if (result != CAP_FULL) {
      return result;
    }
    result = capability_grow(table);
    if (result != CAP_OK) {
      return result;
    }
  }
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

static enum capability_result acquire_grant(struct capability_table *source,
    handle_t handle, uint64_t rights, uint64_t transport, bool same_authority,
    struct capability_grant *grant)
{
  if (!grant) {
    return CAP_INVALID;
  }
  *grant = (struct capability_grant){0};
  struct capability_reference reference;
  enum capability_result result = capability_acquire(source, handle, rights,
      transport, &reference);
  if (result != CAP_OK) {
    return result;
  }
  if (reference.object->type == OBJECT_ENDPOINT_RECEIPT ||
      reference.object->type == OBJECT_ENDPOINT_RECEIVER) {
    capability_release(&reference);
    return CAP_DENIED;
  }
  if (same_authority) {
    rights = reference.rights;
    transport = reference.transport;
  }
  result = capability_grant_retain(reference.object, rights, transport, grant);
  if (result == CAP_OK) {
    spin_lock(&source->lock);
    struct capability_entry *entry = find_entry(source, handle);
    bool current = entry && entry->object == reference.object &&
        entry->rights == reference.rights && entry->transport == reference.transport;
    spin_unlock(&source->lock);
    if (!current) {
      capability_grant_release(grant);
      result = CAP_BAD_HANDLE;
    }
  }
  capability_release(&reference);
  return result;
}

enum capability_result capability_grant_acquire(struct capability_table *source,
    handle_t handle, uint64_t rights, uint64_t transport,
    struct capability_grant *grant)
{
  return acquire_grant(source, handle, rights, transport, false, grant);
}

enum capability_result capability_grant_acquire_same(struct capability_table *source,
    handle_t handle, struct capability_grant *grant)
{
  return acquire_grant(source, handle, 0, 0, true, grant);
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
  struct capability_grant grant;
  enum capability_result status = capability_grant_acquire(source, handle, rights,
      transport, &grant);
  if (status != CAP_OK) {
    return status;
  }
  status = capability_validate_grants(destination, &grant, 1);
  if (status != CAP_OK) {
    capability_grant_release(&grant);
    return status;
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  for (;;) {
    status = capability_reserve(destination, 1, &reservation, &slot);
    if (status != CAP_FULL) {
      break;
    }
    status = capability_grow(destination);
    if (status != CAP_OK) {
      break;
    }
  }
  if (status == CAP_OK) {
    capability_install_reserved(&reservation, &slot, &grant, 1, result);
  }
  capability_grant_release(&grant);
  return status;
}

void capability_table_destroy(struct capability_table *table)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&table->lock);
  KASSERT(!table->reserved);
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

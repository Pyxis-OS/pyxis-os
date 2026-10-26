#include <kernel/bulk.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/process.h>

struct bulk_gate {
  struct kernel_object storage;
  atomic_size_t bytes;
};

static void destroy_gate(struct kernel_object *object)
{
  struct bulk_gate *gate = (struct bulk_gate *)object;
  KASSERT(!atomic_load_explicit(&gate->bytes, memory_order_acquire));
  kfree(gate);
}

static struct bulk_gate *process_gate(struct process *process)
{
  struct bulk_gate *gate = atomic_load_explicit(&process->bulk_gate, memory_order_acquire);
  if (gate) {
    return gate;
  }
  struct bulk_gate *candidate = kmalloc(sizeof(*candidate));
  if (!candidate) {
    return NULL;
  }
  object_init(&candidate->storage, OBJECT_BULK_GATE, destroy_gate);
  atomic_init(&candidate->bytes, 0);
  if (atomic_compare_exchange_strong_explicit(&process->bulk_gate, &gate, candidate,
      memory_order_acq_rel, memory_order_acquire)) {
    return candidate;
  }
  /* The losing candidate was never published and owns no other resources. */
  kfree(candidate);
  return gate;
}

static enum call_status charge_gate(struct bulk_gate *gate, size_t bytes)
{
  size_t current = atomic_load_explicit(&gate->bytes, memory_order_acquire);
  for (;;) {
    KASSERT(current <= BULK_PROCESS_BYTES_LIMIT);
    if (bytes > BULK_PROCESS_BYTES_LIMIT - current) {
      return CALL_LIMIT;
    }
    if (atomic_compare_exchange_weak_explicit(&gate->bytes, &current, current + bytes,
        memory_order_acq_rel, memory_order_acquire)) {
      return CALL_OK;
    }
  }
}

static void uncharge_gate(struct bulk_gate *gate, size_t bytes)
{
  size_t previous = atomic_fetch_sub_explicit(&gate->bytes, bytes, memory_order_acq_rel);
  KASSERT(previous >= bytes);
}

static void destroy_buffer(struct kernel_object *object)
{
  struct bulk_buffer *buffer = (struct bulk_buffer *)object;
  struct bulk_gate *gate = buffer->gate;
  size_t capacity = buffer->capacity;
  kfree(buffer->data);
  kfree(buffer);
  uncharge_gate(gate, capacity);
  object_release(&gate->storage);
}

enum call_status bulk_buffer_create(struct process *process, size_t capacity,
    struct bulk_buffer **result)
{
  if (result) {
    *result = NULL;
  }
  if (!process || !result || capacity > FILE_READ_BULK_MAX_BYTES) {
    return CALL_BAD_REQUEST;
  }
  struct bulk_gate *gate = process_gate(process);
  if (!gate) {
    return CALL_NO_MEMORY;
  }
  if (!object_retain(&gate->storage)) {
    return CALL_LIMIT;
  }
  enum call_status status = charge_gate(gate, capacity);
  if (status != CALL_OK) {
    object_release(&gate->storage);
    return status;
  }
  struct bulk_buffer *buffer = kmalloc(sizeof(*buffer));
  uint8_t *data = capacity && buffer ? kmalloc(capacity) : NULL;
  if (!buffer || (capacity && !data)) {
    kfree(buffer);
    uncharge_gate(gate, capacity);
    object_release(&gate->storage);
    return CALL_NO_MEMORY;
  }
  *buffer = (struct bulk_buffer){.gate = gate, .data = data, .capacity = capacity};
  object_init(&buffer->storage, OBJECT_BULK_STORAGE, destroy_buffer);
  *result = buffer;
  return CALL_OK;
}

bool bulk_buffer_retain(struct bulk_buffer *buffer)
{
  return object_retain(&buffer->storage);
}

void bulk_buffer_release(struct bulk_buffer *buffer)
{
  if (buffer) {
    object_release(&buffer->storage);
  }
}

void bulk_process_exit(struct process *process)
{
  struct bulk_gate *gate = atomic_exchange_explicit(&process->bulk_gate, NULL,
      memory_order_acq_rel);
  if (gate) {
    object_release(&gate->storage);
  }
}

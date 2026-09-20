#include <arch/smp.h>
#include <kernel/object.h>
#include <kernel/panic.h>
#include <stdint.h>

static struct kernel_object *retired_objects;
static atomic_bool retired_locked;

static void lock_retired(void)
{
  while (atomic_exchange_explicit(&retired_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_retired(void)
{
  atomic_store_explicit(&retired_locked, false, memory_order_release);
}

void object_init(struct kernel_object *object,
                 void (*destroy)(struct kernel_object *))
{
  KASSERT(object && destroy);
  atomic_init(&object->references, 1);
  object->retired_next = NULL;
  object->destroy = destroy;
}

bool object_retain(struct kernel_object *object)
{
  size_t count = atomic_load_explicit(&object->references, memory_order_relaxed);
  for (;;) {
    KASSERT(count);
    if (count == SIZE_MAX) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&object->references, &count,
          count + 1, memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
}

void object_release(struct kernel_object *object)
{
  size_t previous = atomic_fetch_sub_explicit(&object->references, 1,
                                              memory_order_acq_rel);
  KASSERT(previous);
  if (previous != 1) {
    return;
  }

  /* The last owner lends the object's own link to the queue. Even an AP can
   * retire it without allocating a queue node or touching the heap. */
  lock_retired();
  object->retired_next = retired_objects;
  retired_objects = object;
  unlock_retired();
  /* The BSP may destroy object immediately after publication. */
}

bool object_reap_pending(void)
{
  lock_retired();
  bool pending = retired_objects != NULL;
  unlock_retired();
  return pending;
}

void object_reap(void)
{
  KASSERT(arch_cpu_index() == 0);
  lock_retired();
  struct kernel_object *object = retired_objects;
  retired_objects = NULL;
  unlock_retired();

  while (object) {
    struct kernel_object *next = object->retired_next;
    object->destroy(object);
    object = next;
  }
}

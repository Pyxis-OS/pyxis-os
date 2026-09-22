#include <abi/memory.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

_Static_assert(MEMORY_PAGE_SIZE == PAGE_SIZE, "memory ABI page size");

static void destroy_memory(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *memory_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_MEMORY, destroy_memory);
  }
  return object;
}

struct syscall_result memory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != MEMORY_ALLOCATE && operation != MEMORY_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & MEMORY_RIGHT_MANAGE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union memory_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct memory_region region;
  if (operation == MEMORY_ALLOCATE) {
    size_t size = request.allocate.size;
    if (!size || size > SIZE_MAX - (PAGE_SIZE - 1) || reply_capacity < sizeof(region)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(region), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    region = (struct memory_region){.size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)};
  } else {
    region = request.release;
    if (!region.address || !region.size || (region.address & (PAGE_SIZE - 1)) ||
        (region.size & (PAGE_SIZE - 1)) || region.size > UINTPTR_MAX - region.address) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }

  enum mm_result result = task_request_memory(operation, &region);
  if (result != MM_OK) {
    KASSERT(result == MM_INVALID || result == MM_NO_MEMORY);
    return (struct syscall_result){result == MM_INVALID ? CALL_BAD_REQUEST : CALL_NO_MEMORY, 0};
  }
  if (operation == MEMORY_RELEASE) {
    return (struct syscall_result){CALL_OK, 0};
  }
  /* ALLOCATE adds a disjoint region; the checked reply mapping remains valid.
   * RELEASE never touches user memory again after changing the mappings. */
  KASSERT(copy_to_user(reply_address, &region, sizeof(region)));
  return (struct syscall_result){CALL_OK, sizeof(region)};
}

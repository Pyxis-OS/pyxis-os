#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/user_memory.h>

bool user_buffer_lease_acquire(struct user_buffer_lease *lease)
{
  *lease = (struct user_buffer_lease){0};
  struct process *process = process_current();
  if (!process || !process_vm_borrow_acquire(process)) {
    return false;
  }
  lease->process = process;
  return true;
}

void user_buffer_lease_release(struct user_buffer_lease *lease)
{
  if (lease->process) {
    process_vm_borrow_release(lease->process);
    *lease = (struct user_buffer_lease){0};
  }
}

static bool check_leased(struct user_buffer_lease *lease, uintptr_t address,
    size_t bytes, enum user_buffer_access access)
{
  return (access == USER_BUFFER_READ || access == USER_BUFFER_WRITE) &&
      vm_user_buffer_accessible(lease->process->address_space, address, bytes,
          access == USER_BUFFER_WRITE);
}

bool user_buffer_check(uintptr_t address, size_t bytes, enum user_buffer_access access)
{
  struct user_buffer_lease lease;
  if (!user_buffer_lease_acquire(&lease)) {
    return false;
  }
  bool valid = check_leased(&lease, address, bytes, access);
  user_buffer_lease_release(&lease);
  return valid;
}

bool copy_from_user(void *destination, uintptr_t source, size_t bytes)
{
  struct user_buffer_lease lease;
  if (!user_buffer_lease_acquire(&lease)) {
    return false;
  }
  bool valid = check_leased(&lease, source, bytes, USER_BUFFER_READ);
  if (valid && bytes) {
    memcpy(destination, (const void *)source, bytes);
  }
  user_buffer_lease_release(&lease);
  return valid;
}

bool copy_to_user(uintptr_t destination, const void *source, size_t bytes)
{
  return copy_reply_to_user(destination, source, bytes, 0, NULL, 0);
}

bool copy_reply_to_user(uintptr_t reply_address, const void *reply, size_t reply_size,
    uintptr_t data_address, const void *data, size_t data_size)
{
  struct user_buffer_lease lease;
  if (!user_buffer_lease_acquire(&lease)) {
    return false;
  }
  bool valid = check_leased(&lease, reply_address, reply_size, USER_BUFFER_WRITE) &&
      check_leased(&lease, data_address, data_size, USER_BUFFER_WRITE);
  if (valid) {
    if (data_size) {
      memcpy((void *)data_address, data, data_size);
    }
    if (reply_size) {
      memcpy((void *)reply_address, reply, reply_size);
    }
  }
  user_buffer_lease_release(&lease);
  return valid;
}

bool copy_returned_handles_to_user(uintptr_t destination, const void *source,
    size_t bytes, const handle_t *handles, size_t count)
{
  if (copy_to_user(destination, source, bytes)) {
    return true;
  }
  capability_close_returned(&process_current()->capabilities, handles, count);
  return false;
}

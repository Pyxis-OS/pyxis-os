#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

bool user_buffer_check(uintptr_t address, size_t bytes,
                        enum user_buffer_access access)
{
  if (access != USER_BUFFER_READ && access != USER_BUFFER_WRITE) {
    return false;
  }
  struct process *process = process_current();
  return process && vm_user_buffer_accessible(process->address_space, address,
                                              bytes, access == USER_BUFFER_WRITE);
}

bool copy_from_user(void *destination, uintptr_t source, size_t bytes)
{
  if (!user_buffer_check(source, bytes, USER_BUFFER_READ)) {
    return false;
  }
  if (bytes) {
    memcpy(destination, (const void *)source, bytes);
  }
  return true;
}

bool copy_to_user(uintptr_t destination, const void *source, size_t bytes)
{
  if (!user_buffer_check(destination, bytes, USER_BUFFER_WRITE)) {
    return false;
  }
  if (bytes) {
    memcpy((void *)destination, source, bytes);
  }
  return true;
}

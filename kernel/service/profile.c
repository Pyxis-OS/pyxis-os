#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/service/profile.h>
#include <kernel/task.h>

struct task_profile *profile_storage_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task_profile *profile = kmalloc(sizeof(*profile));
  if (profile) {
    *profile = (struct task_profile){0};
  }
  return profile;
}

void profile_storage_destroy(struct task_profile *profile)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  kfree(profile);
}

struct profile_snapshot *profile_memory_current(void)
{
  return &task_profile_current()->memory;
}

struct profile_host_snapshot *profile_host_current(void)
{
  return &task_profile_current()->host;
}

enum call_status profile_memory_control(uint64_t operation, struct profile_snapshot *reply)
{
  struct profile_snapshot *profile = profile_memory_current();
  bool active = profile->flags & PROFILE_ACTIVE;
  if (operation == PROFILE_BEGIN) {
    if (active) {
      return CALL_BUSY;
    }
    *profile = (struct profile_snapshot){.flags = PROFILE_ACTIVE};
  } else {
    if (operation == PROFILE_END) {
      if (!active) {
        return CALL_BAD_REQUEST;
      }
      profile->flags &= ~PROFILE_ACTIVE;
    } else {
      KASSERT(operation == PROFILE_SNAPSHOT);
    }
    *reply = *profile;
  }
  return CALL_OK;
}

enum call_status profile_host_control(uint64_t operation, struct profile_host_snapshot *reply)
{
  struct profile_host_snapshot *profile = profile_host_current();
  bool active = profile->flags & PROFILE_ACTIVE;
  if (operation == PROFILE_HOST_BEGIN) {
    if (active) {
      return CALL_BUSY;
    }
    *profile = (struct profile_host_snapshot){.flags = PROFILE_ACTIVE};
  } else {
    if (operation == PROFILE_HOST_END) {
      if (!active) {
        return CALL_BAD_REQUEST;
      }
      profile->flags &= ~PROFILE_ACTIVE;
    } else {
      KASSERT(operation == PROFILE_HOST_SNAPSHOT);
    }
    *reply = *profile;
  }
  return CALL_OK;
}

#include <kernel/mm/kernel_owner.h>
#include <kernel/object/wifi.h>
#include <abi/startup.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/object/directory.h>
#include <kernel/object/environment.h>
#include <kernel/object/thread.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user/startup.h>

struct startup_sizes {
  size_t metadata;
  size_t arguments;
};

static bool add_string_size(const char *text, size_t *size)
{
  if (!text || *size >= STARTUP_MAX_SIZE) {
    return false;
  }
  size_t remaining = STARTUP_MAX_SIZE - *size;
  size_t length = strnlen(text, remaining);
  if (length == remaining) {
    return false;
  }
  *size += length + 1;
  return true;
}

static bool entry_name_valid(const char *name)
{
  if (!name || !*name) {
    return false;
  }
  size_t length = 0;
  for (; name[length] && length <= ENVIRONMENT_NAME_MAX; ++length) {
    char c = name[length];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-' || c == '.')) {
      return false;
    }
  }
  return length <= ENVIRONMENT_NAME_MAX;
}

static bool measure_startup(struct process *process,
    const struct process_startup *source, struct startup_sizes *sizes)
{
  if (!source || source->entry_count > STARTUP_MAX_SIZE / sizeof(*source->entries) ||
      (source->entry_count && !source->entries) ||
      source->working_directory_count > STARTUP_MAX_SIZE / sizeof(handle_t) ||
      (source->working_directory_count && !source->working_directories) ||
      (!source->working_directory_count && source->working_path) ||
      source->argc >= STARTUP_MAX_SIZE / sizeof(uint64_t) ||
      (source->argc && !source->argv)) {
    return false;
  }
  sizes->metadata = sizeof(struct startup_info) +
      source->working_directory_count * sizeof(handle_t);
  sizes->arguments = (source->argc + 1) * sizeof(uint64_t);
  size_t environment = source->entry_count * sizeof(struct process_environment_entry);
  for (size_t i = 0; i < source->entry_count; ++i) {
    const struct process_environment_entry *entry = &source->entries[i];
    if (!entry_name_valid(entry->name) || (entry->flags & ~ENVIRONMENT_EXPORTED) ||
        !add_string_size(entry->name, &environment)) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (strlen(entry->name) == strlen(source->entries[j].name) &&
          !memcmp(entry->name, source->entries[j].name, strlen(entry->name))) {
        return false;
      }
    }
    if (entry->kind == ENVIRONMENT_TEXT) {
      const char *text = (const char *)(uintptr_t)entry->value;
      if (entry->length >= STARTUP_MAX_SIZE || !text ||
          strnlen(text, entry->length + 1) != entry->length ||
          !add_string_size(text, &environment)) {
        return false;
      }
    } else if (entry->kind == ENVIRONMENT_CAP && !entry->length) {
      struct capability_reference reference;
      if (capability_acquire(&process->capabilities, entry->value, 0, 0,
            &reference) != CAP_OK) {
        return false;
      }
      capability_release(&reference);
    } else {
      return false;
    }
  }
  for (size_t i = 0; i < source->working_directory_count; ++i) {
    struct capability_reference reference;
    if (capability_acquire(&process->capabilities, source->working_directories[i],
          0, 0, &reference) != CAP_OK) {
      return false;
    }
    bool valid = object_effective_type(reference.object) == OBJECT_DIRECTORY;
    capability_release(&reference);
    if (!valid) {
      return false;
    }
  }
  if (source->working_path && !add_string_size(source->working_path, &sizes->metadata)) {
    return false;
  }
  for (size_t i = 0; i < source->argc; ++i) {
    if (!add_string_size(source->argv[i], &sizes->arguments)) {
      return false;
    }
  }
  if (environment > STARTUP_MAX_SIZE - sizes->metadata) {
    return false;
  }
  size_t charged_metadata = (sizes->metadata + environment + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  sizes->metadata = (sizes->metadata + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  sizes->arguments = (sizes->arguments + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  return charged_metadata <= STARTUP_MAX_SIZE &&
      sizes->arguments <= STARTUP_MAX_SIZE - charged_metadata;
}

static enum mm_result seed_environment(struct process *process,
    const struct process_startup *source)
{
  for (size_t i = 0; i < source->entry_count; ++i) {
    const struct process_environment_entry *entry = &source->entries[i];
    enum call_status status;
    if (entry->kind == ENVIRONMENT_CAP) {
      struct capability_grant grant = {0};
      struct capability_reference reference;
      enum capability_result captured = capability_acquire(&process->capabilities,
          entry->value, 0, 0, &reference);
      if (captured == CAP_OK) {
        if (!entry->flags && (reference.rights & WIFI_RIGHT_POLICY_ONCE) &&
            wifi_policy_owned(reference.object, process)) {
          captured = capability_grant_derive(reference.object, reference.rights,
              reference.transport, reference.restrictions, &grant);
        } else {
          captured = capability_grant_acquire_same(&process->capabilities, entry->value, &grant);
        }
        capability_release(&reference);
      }
      if (captured != CAP_OK) {
        return captured == CAP_NO_MEMORY ? MM_NO_MEMORY : MM_INVALID;
      }
      status = environment_table_seed_owned(process->environment, entry->name,
          entry->flags, &grant);
      capability_grant_release(&grant);
    } else {
      status = environment_table_seed_text(process->environment, entry->name,
          entry->flags, (const char *)(uintptr_t)entry->value, entry->length);
    }
    if (status != CALL_OK) {
      return status == CALL_NO_MEMORY ? MM_NO_MEMORY : MM_INVALID;
    }
  }
  return MM_OK;
}

static uint64_t copy_string(uint8_t *buffer, size_t *offset, uintptr_t address,
    const char *text)
{
  size_t length = strlen(text) + 1;
  uint64_t result = address + *offset;
  memcpy(buffer + *offset, text, length);
  *offset += length;
  return result;
}

static void fill_startup(uint8_t *buffer, uintptr_t address,
    const struct process_startup *source, const struct startup_sizes *sizes,
    handle_t environment, handle_t thread_fs, size_t usable_cpu_count)
{
  struct startup_info *info = (struct startup_info *)buffer;
  *info = (struct startup_info){
    .version = STARTUP_VERSION, .size = sizes->metadata + sizes->arguments,
    .read_only_size = sizes->metadata, .environment_handle = environment,
    .working_directory_count = source->working_directory_count,
    .argc = source->argc, .argv = address + sizes->metadata, .thread_fs = thread_fs,
    .usable_cpu_count = usable_cpu_count,
  };
  size_t offset = sizeof(*info);
  if (source->working_directory_count) {
    info->working_directories = address + offset;
    size_t bytes = source->working_directory_count * sizeof(handle_t);
    memcpy(buffer + offset, source->working_directories, bytes);
    offset += bytes;
  }
  if (source->working_path) {
    info->working_path = copy_string(buffer, &offset, address, source->working_path);
  }
  uint64_t *argv = (uint64_t *)(buffer + sizes->metadata);
  offset = sizes->metadata + (source->argc + 1) * sizeof(*argv);
  for (size_t i = 0; i < source->argc; ++i) {
    argv[i] = copy_string(buffer, &offset, address, source->argv[i]);
  }
}

static enum mm_result copy_startup_pages(struct vm_space *space, uintptr_t address,
                                          const uint8_t *buffer, size_t size)
{
  uintptr_t scratch;
  enum mm_result result = vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, &scratch);
  if (result != MM_OK) {
    return result;
  }

  for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
    struct page_translation translation;
    KASSERT(vm_query(space, address + offset, &translation) == MM_OK);
    result = vm_map(vm_kernel_space(), scratch, translation.physical, PAGE_WRITE);
    if (result != MM_OK) {
      break;
    }
    memcpy((void *)scratch, buffer + offset, PAGE_SIZE);

    /* Only the borrowed kernel alias is removed; VM owns the user frame. */
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), scratch, &physical) == MM_OK);
    KASSERT(physical == translation.physical);
  }
  KASSERT(vm_release(vm_kernel_space(), scratch, PAGE_SIZE) == MM_OK);
  return result;
}

static enum mm_result process_prepare_startup_owned(struct process *process,
    const struct process_startup *source)
{
  KASSERT(arch_cpu_index() == 0);
  struct startup_sizes sizes;
  if (!process || process->startup_address || !measure_startup(process, source, &sizes)) {
    return MM_INVALID;
  }
  size_t usable_cpu_count = task_space_usable_cpu_count(process->space);
  if (!usable_cpu_count) {
    return MM_INVALID;
  }
  enum mm_result result = seed_environment(process, source);
  if (result != MM_OK) {
    return result;
  }
  size_t total = sizes.metadata + sizes.arguments;
  uint8_t *buffer = kmalloc(total);
  if (!buffer) {
    return MM_NO_MEMORY;
  }
  memset(buffer, 0, total);
  struct kernel_object *threads = thread_service_create();
  struct kernel_object *environment = environment_create();
  handle_t thread_fs = HANDLE_INVALID, environment_handle = HANDLE_INVALID;
  enum capability_result installed = threads && environment ?
      capability_install(&process->capabilities, threads, THREAD_RIGHT_SET_FS, 0,
          &thread_fs) : CAP_NO_MEMORY;
  if (installed == CAP_OK) {
    installed = capability_install(&process->capabilities, environment,
        ENVIRONMENT_RIGHTS, 0, &environment_handle);
  }
  if (threads) {
    object_release(threads);
  }
  if (environment) {
    object_release(environment);
  }
  if (installed != CAP_OK) {
    if (thread_fs) {
      KASSERT(capability_close(&process->capabilities, thread_fs) == CAP_OK);
    }
    kfree(buffer);
    return MM_NO_MEMORY;
  }
  uintptr_t address;
  result = vm_alloc(process->address_space, total, PAGE_SIZE, PAGE_USER, &address);
  if (result == MM_OK) {
    fill_startup(buffer, address, source, &sizes, environment_handle, thread_fs,
        usable_cpu_count);
    result = copy_startup_pages(process->address_space, address, buffer, total);
    if (result == MM_OK) {
      for (size_t offset = sizes.metadata; offset < total; offset += PAGE_SIZE) {
        result = vm_protect(process->address_space, address + offset, PAGE_USER | PAGE_WRITE);
        if (result != MM_OK) {
          break;
        }
      }
    }
    if (result != MM_OK) {
      KASSERT(vm_free(process->address_space, address, total) == MM_OK);
    }
  }
  kfree(buffer);
  if (result != MM_OK) {
    KASSERT(capability_close(&process->capabilities, environment_handle) == CAP_OK);
    KASSERT(capability_close(&process->capabilities, thread_fs) == CAP_OK);
    return result;
  }
  /* ENV now owns each named logical grant. Retire its initial local source
   * handle exactly once; only the independently owned cwd handles survive. */
  for (size_t i = 0; i < source->entry_count; ++i) {
    const struct process_environment_entry *entry = &source->entries[i];
    if (entry->kind != ENVIRONMENT_CAP) {
      continue;
    }
    bool keep = false;
    for (size_t j = 0; j < source->working_directory_count; ++j) {
      keep |= entry->value == source->working_directories[j];
    }
    for (size_t j = 0; j < i; ++j) {
      keep |= source->entries[j].kind == ENVIRONMENT_CAP &&
          source->entries[j].value == entry->value;
    }
    if (!keep) {
      KASSERT(capability_close(&process->capabilities, entry->value) == CAP_OK);
    }
  }
  process->startup_address = address;
  return MM_OK;
}

enum mm_result process_prepare_startup(struct process *process,
    const struct process_startup *source)
{
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    return MM_BUSY;
  }
  enum mm_result result = process_prepare_startup_owned(process, source);
  vm_kernel_owner_release(&owner);
  return result;
}

#include <arch/paging.h>
#include <kernel/limit.h>
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

#define STARTUP_STEP_PAGES 64

static void startup_yield(void)
{
  if (task_current_actor()) {
    task_yield_masked();
  }
}

static size_t startup_string_length(const char *text)
{
  size_t length = 0;
  size_t step = STARTUP_STEP_PAGES * PAGE_SIZE;
  for (;;) {
    size_t count = strnlen(text + length, step);
    length += count;
    if (count < step) {
      return length;
    }
    startup_yield();
  }
}

static void copy_startup_bytes(void *destination, const void *source, size_t size)
{
  for (size_t offset = 0; offset < size;) {
    size_t count = size - offset;
    if (count > STARTUP_STEP_PAGES * PAGE_SIZE) {
      count = STARTUP_STEP_PAGES * PAGE_SIZE;
    }
    memcpy((uint8_t *)destination + offset, (const uint8_t *)source + offset, count);
    offset += count;
    if (offset < size) {
      startup_yield();
    }
  }
}

static enum call_status add_string_size(const char *text, size_t *size,
    struct call_limit *detail)
{
  if (!text) {
    return CALL_BAD_REQUEST;
  }
  size_t length = startup_string_length(text);
  if (length == SIZE_MAX || *size > SIZE_MAX - length - 1) {
    return call_limit_encoding(detail);
  }
  *size += length + 1;
  return CALL_OK;
}

enum call_status process_startup_measure(const struct process_startup *source,
    struct process_startup_storage *sizes, struct call_limit *detail)
{
  *sizes = (struct process_startup_storage){0};
  if (!source || (source->entry_count && !source->entries) ||
      (source->working_directory_count && !source->working_directories) ||
      (!source->working_directory_count && source->working_path) ||
      (source->argc && !source->argv)) {
    return CALL_BAD_REQUEST;
  }
  if (source->working_directory_count >
      (SIZE_MAX - sizeof(struct startup_info)) / sizeof(handle_t) ||
      source->argc >= SIZE_MAX / sizeof(uint64_t)) {
    return call_limit_encoding(detail);
  }
  sizes->metadata = sizeof(struct startup_info) +
      source->working_directory_count * sizeof(handle_t);
  sizes->arguments = (source->argc + 1) * sizeof(uint64_t);
  size_t environment = 0;
  for (size_t i = 0; i < source->entry_count; ++i) {
    const struct process_environment_entry *entry = &source->entries[i];
    enum call_status status = environment_seed_measure(entry->name, entry->kind,
        entry->flags, entry->length, &environment, detail);
    if (status != CALL_OK) {
      return status;
    }
    for (size_t j = 0; j < i; ++j) {
      if (strlen(entry->name) == strlen(source->entries[j].name) &&
          !memcmp(entry->name, source->entries[j].name, strlen(entry->name))) {
        return CALL_BAD_REQUEST;
      }
    }
    if (entry->kind == ENVIRONMENT_TEXT) {
      const char *text = (const char *)(uintptr_t)entry->value;
      if (!text || strnlen(text, entry->length + 1) != entry->length) {
        return CALL_BAD_REQUEST;
      }
    }
  }
  enum call_status status = environment_seed_admit(source->entry_count, environment, detail);
  if (status != CALL_OK) {
    return status;
  }
  if (source->working_path) {
    status = add_string_size(source->working_path, &sizes->metadata, detail);
    if (status != CALL_OK) {
      return status;
    }
  }
  for (size_t i = 0; i < source->argc; ++i) {
    status = add_string_size(source->argv[i], &sizes->arguments, detail);
    if (status != CALL_OK) {
      return status;
    }
    if ((i + 1) % 64 == 0) {
      startup_yield();
    }
  }
  if (sizes->metadata > SIZE_MAX - PAGE_SIZE + 1 ||
      sizes->arguments > SIZE_MAX - PAGE_SIZE + 1) {
    return call_limit_encoding(detail);
  }
  sizes->metadata = (sizes->metadata + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  sizes->arguments = (sizes->arguments + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (sizes->metadata > SIZE_MAX - sizes->arguments) {
    return call_limit_encoding(detail);
  }
  size_t total = sizes->metadata + sizes->arguments;
  if (total > arch_user_vm_size()) {
    return call_limit_set(detail, CALL_LIMIT_STARTUP_ADDRESS, total,
        arch_user_vm_size(), CALL_LIMIT_HAS_REQUESTED | CALL_LIMIT_HAS_LIMIT);
  }
  return CALL_OK;
}

static enum call_status capability_status(enum capability_result result,
    struct call_limit *detail)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return detail && detail->reason ? CALL_LIMIT : call_limit_reference(detail);
  case CAP_FULL:
    return call_limit_set(detail, CALL_LIMIT_HANDLE_ENCODING, 0, 0, 0);
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  default: return CALL_BAD_REQUEST;
  }
}

static enum call_status validate_startup_grants(struct process *process,
    const struct process_startup *source, struct call_limit *detail)
{
  for (size_t i = 0; i < source->working_directory_count; ++i) {
    struct capability_reference reference;
    enum capability_result result = capability_acquire_with_limit(&process->capabilities,
        source->working_directories[i], 0, 0, &reference, detail);
    if (result != CAP_OK) {
      return capability_status(result, detail);
    }
    bool valid = object_effective_type(reference.object) == OBJECT_DIRECTORY;
    capability_release(&reference);
    if (!valid) {
      return CALL_BAD_REQUEST;
    }
  }
  return CALL_OK;
}

static enum call_status seed_environment(struct process *process,
    const struct process_startup *source, struct call_limit *detail)
{
  for (size_t i = 0; i < source->entry_count; ++i) {
    const struct process_environment_entry *entry = &source->entries[i];
    enum call_status status;
    if (entry->kind == ENVIRONMENT_CAP) {
      struct capability_grant grant = {0};
      struct capability_reference reference;
      enum capability_result captured = capability_acquire_with_limit(&process->capabilities,
          entry->value, 0, 0, &reference, detail);
      if (captured == CAP_OK) {
        if (!entry->flags && (reference.rights & WIFI_RIGHT_POLICY_ONCE) &&
            wifi_policy_owned(reference.object, process)) {
          captured = capability_grant_derive(reference.object, reference.rights,
              reference.transport, reference.restrictions, &grant);
        } else {
          captured = capability_grant_acquire_same_with_limit(&process->capabilities, entry->value, &grant, detail);
        }
        capability_release(&reference);
      }
      if (captured != CAP_OK) {
        return capability_status(captured, detail);
      }
      status = environment_table_seed_owned(process->environment, entry->name,
          entry->flags, &grant, detail);
      capability_grant_release(&grant);
    } else {
      status = environment_table_seed_text(process->environment, entry->name,
          entry->flags, (const char *)(uintptr_t)entry->value, entry->length, detail);
    }
    if (status != CALL_OK) {
      return status;
    }
  }
  return CALL_OK;
}

static uint64_t copy_string(uint8_t *buffer, size_t *offset, uintptr_t address,
    const char *text)
{
  size_t length = startup_string_length(text) + 1;
  uint64_t result = address + *offset;
  copy_startup_bytes(buffer + *offset, text, length);
  *offset += length;
  return result;
}

static void fill_startup(uint8_t *buffer, uintptr_t address,
    const struct process_startup *source, const struct process_startup_storage *sizes,
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
    copy_startup_bytes(buffer + offset, source->working_directories, bytes);
    offset += bytes;
  }
  if (source->working_path) {
    info->working_path = copy_string(buffer, &offset, address, source->working_path);
  }
  uint64_t *argv = (uint64_t *)(buffer + sizes->metadata);
  offset = sizes->metadata + (source->argc + 1) * sizeof(*argv);
  for (size_t i = 0; i < source->argc; ++i) {
    argv[i] = copy_string(buffer, &offset, address, source->argv[i]);
    if ((i + 1) % 64 == 0) {
      startup_yield();
    }
  }
}

static enum mm_result copy_startup_pages(struct vm_space *space, uintptr_t address,
                                          const uint8_t *buffer, size_t size)
{
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
    struct page_translation translation;
    KASSERT(vm_query(space, address + offset, &translation) == MM_OK);
    void *page = arch_frame_map(translation.physical);
    memcpy(page, buffer + offset, PAGE_SIZE);
    arch_frame_unmap();
    if (!((offset / PAGE_SIZE + 1) % STARTUP_STEP_PAGES) && task_current_actor()) {
      task_yield_masked();
    }
  }
  return MM_OK;
}

enum call_status process_prepare_startup(struct process *process,
    const struct process_startup *source, struct process_startup_storage *storage,
    struct call_limit *detail)
{
  process_prepare_require(process);
  if (!process || process->startup_address) {
    return CALL_BAD_REQUEST;
  }
  struct process_startup_storage measured;
  enum call_status status = process_startup_measure(source, &measured, detail);
  if (status != CALL_OK) {
    return status;
  }
  status = validate_startup_grants(process, source, detail);
  if (status != CALL_OK) {
    return status;
  }
  size_t usable_cpu_count = task_space_usable_cpu_count(process->space);
  if (!usable_cpu_count) {
    return CALL_BAD_REQUEST;
  }
  bool local = !storage;
  if (local) {
    storage = &measured;
    storage->buffer = kmalloc(measured.metadata + measured.arguments);
    if (!storage->buffer) {
      return CALL_NO_MEMORY;
    }
  }
  KASSERT(storage->buffer && storage->metadata == measured.metadata &&
      storage->arguments == measured.arguments);
  struct process_startup_storage sizes = *storage;
  size_t total = sizes.metadata + sizes.arguments;
  uint8_t *buffer = sizes.buffer;
  status = seed_environment(process, source, detail);
  if (status != CALL_OK) {
    if (local) {
      kfree(buffer);
    }
    return status;
  }
  for (size_t offset = 0; offset < total;) {
    size_t count = total - offset;
    if (count > STARTUP_STEP_PAGES * PAGE_SIZE) {
      count = STARTUP_STEP_PAGES * PAGE_SIZE;
    }
    memset(buffer + offset, 0, count);
    offset += count;
    if (offset < total) {
      startup_yield();
    }
  }
  struct kernel_object *threads = thread_service_create();
  struct kernel_object *environment = environment_create();
  handle_t thread_fs = HANDLE_INVALID, environment_handle = HANDLE_INVALID;
  enum capability_result installed = threads && environment ?
      capability_install_with_limit(&process->capabilities, threads, THREAD_RIGHT_SET_FS, 0,
          &thread_fs, detail) : CAP_NO_MEMORY;
  if (installed == CAP_OK) {
    installed = capability_install_with_limit(&process->capabilities, environment,
        ENVIRONMENT_RIGHTS, 0, &environment_handle, detail);
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
    if (local) {
      kfree(buffer);
    }
    return capability_status(installed, detail);
  }
  uintptr_t address;
  enum mm_result result = vm_reserve(process->address_space, total, PAGE_SIZE, &address);
  if (result == MM_OK) {
    struct vm_backing backing;
    result = vm_back_begin(process->address_space, address, total, PAGE_USER, &backing);
    if (result == MM_OK) {
      while (!vm_back_step(process->address_space, &backing, STARTUP_STEP_PAGES)) {
        if (task_current_actor()) {
          task_yield_masked();
        }
      }
      result = backing.status;
    }
    if (result != MM_OK) {
      KASSERT(vm_release(process->address_space, address, total) == MM_OK);
    }
  }
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
        if (!((offset / PAGE_SIZE + 1) % STARTUP_STEP_PAGES) && task_current_actor()) {
          task_yield_masked();
        }
      }
    }
    if (result != MM_OK) {
      struct vm_reclaim reclaim;
      KASSERT(vm_free_begin(process->address_space, address, total, &reclaim) == MM_OK);
      while (!vm_free_step(&reclaim, STARTUP_STEP_PAGES)) {
        if (task_current_actor()) {
          task_yield_masked();
        }
      }
    }
  }
  if (local) {
    kfree(buffer);
  }
  if (result != MM_OK) {
    KASSERT(capability_close(&process->capabilities, environment_handle) == CAP_OK);
    KASSERT(capability_close(&process->capabilities, thread_fs) == CAP_OK);
    return result == MM_BUSY ? CALL_BUSY :
        result == MM_NO_MEMORY ? CALL_NO_MEMORY : CALL_BAD_REQUEST;
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
  return CALL_OK;
}

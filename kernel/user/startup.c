#include <abi/startup.h>
#include <abi/console.h>
#include <abi/file.h>
#include <abi/pipe.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/object/directory.h>
#include <kernel/string.h>
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

static bool same_name(const char *left, const char *right)
{
  size_t length = strlen(left);
  return strlen(right) == length && memcmp(left, right, length) == 0;
}

static bool measure_bindings(struct process *process,
    const struct process_binding *bindings, size_t count, bool directories,
    size_t *size)
{
  for (size_t i = 0; i < count; ++i) {
    const struct process_binding *binding = &bindings[i];
    if (!add_string_size(binding->name, size) || !binding->name[0]) {
      return false;
    }
    if (directories) {
      for (const char *name = binding->name; *name; ++name) {
        if (*name == ':' || *name == '/') {
          return false;
        }
      }
    }
    struct kernel_object *object;
    if (capability_resolve(&process->capabilities, binding->handle, 0, 0,
          &object, NULL, NULL) != CAP_OK ||
        (directories && object->type != OBJECT_DIRECTORY)) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (same_name(binding->name, bindings[j].name)) {
        return false;
      }
    }
  }
  return true;
}

static bool validate_streams(struct process *process,
                             const struct process_startup *source)
{
  for (size_t i = 0; i < STARTUP_STREAM_COUNT; ++i) {
    const struct startup_stream *stream = &source->streams[i];
    if (stream->protocol == STARTUP_STREAM_NONE) {
      if (stream->handle != HANDLE_INVALID) {
        return false;
      }
      continue;
    }

    enum object_type type;
    uint64_t expected_rights;
    if (stream->protocol == PROTOCOL_CONSOLE) {
      type = OBJECT_CONSOLE;
      expected_rights = i == STARTUP_STDIN ? CONSOLE_RIGHT_READ : CONSOLE_RIGHT_WRITE;
    } else if (stream->protocol == PROTOCOL_FILE) {
      type = OBJECT_FILE;
      expected_rights = i == STARTUP_STDIN ? FILE_RIGHT_READ : FILE_RIGHT_WRITE;
    } else if (stream->protocol == PROTOCOL_PIPE) {
      type = OBJECT_PIPE;
      expected_rights = i == STARTUP_STDIN ? PIPE_RIGHT_READ : PIPE_RIGHT_WRITE;
    } else {
      return false;
    }

    struct kernel_object *object;
    uint64_t rights;
    if (capability_resolve(&process->capabilities, stream->handle, expected_rights,
          0, &object, &rights, NULL) != CAP_OK ||
        object->type != type || rights != expected_rights) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (stream->handle == source->streams[j].handle) {
        return false;
      }
    }
    for (size_t j = 0; j < source->resource_count; ++j) {
      if (stream->handle == source->resources[j].handle) {
        return false;
      }
    }
    for (size_t j = 0; j < source->root_count; ++j) {
      if (stream->handle == source->roots[j].handle) {
        return false;
      }
    }
    for (size_t j = 0; j < source->working_directory_count; ++j) {
      if (stream->handle == source->working_directories[j]) {
        return false;
      }
    }
  }
  return true;
}

static bool measure_startup(struct process *process,
                             const struct process_startup *source,
                             struct startup_sizes *sizes)
{
  if (!source ||
      source->working_directory_count > STARTUP_MAX_SIZE / sizeof(handle_t) ||
      (source->working_directory_count && !source->working_directories) ||
      (!source->working_directory_count && source->working_path) ||
      source->resource_count > STARTUP_MAX_SIZE / sizeof(struct startup_binding) ||
      source->root_count > STARTUP_MAX_SIZE / sizeof(struct startup_binding) ||
      source->environment_count > STARTUP_MAX_SIZE / sizeof(struct startup_variable) ||
      source->argc >= STARTUP_MAX_SIZE / sizeof(uint64_t) ||
      (source->resource_count && !source->resources) ||
      (source->root_count && !source->roots) ||
      (source->environment_count && !source->environment) ||
      (source->argc && !source->argv) ||
      !validate_streams(process, source)) {
    return false;
  }

  sizes->metadata = sizeof(struct startup_info) +
                    source->resource_count * sizeof(struct startup_binding) +
                    source->root_count * sizeof(struct startup_binding) +
                    source->working_directory_count * sizeof(handle_t) +
                    source->environment_count * sizeof(struct startup_variable);
  sizes->arguments = (source->argc + 1) * sizeof(uint64_t);
  if (sizes->metadata > STARTUP_MAX_SIZE) {
    return false;
  }

  if (!measure_bindings(process, source->resources, source->resource_count, false,
        &sizes->metadata) ||
      !measure_bindings(process, source->roots, source->root_count, true, &sizes->metadata)) {
    return false;
  }

  for (size_t i = 0; i < source->working_directory_count; ++i) {
    struct kernel_object *object;
    if (capability_resolve(&process->capabilities, source->working_directories[i],
          0, 0, &object, NULL, NULL) != CAP_OK || object->type != OBJECT_DIRECTORY) {
      return false;
    }
  }
  if (source->working_path && !add_string_size(source->working_path, &sizes->metadata)) {
    return false;
  }

  for (size_t i = 0; i < source->environment_count; ++i) {
    const struct process_variable *variable = &source->environment[i];
    if (!add_string_size(variable->name, &sizes->metadata) || !variable->name[0] ||
        !add_string_size(variable->value, &sizes->metadata)) {
      return false;
    }
    for (const char *cursor = variable->name; *cursor; ++cursor) {
      if (*cursor == '=') {
        return false;
      }
    }
    for (size_t j = 0; j < i; ++j) {
      if (same_name(variable->name, source->environment[j].name)) {
        return false;
      }
    }
  }

  for (size_t i = 0; i < source->argc; ++i) {
    if (!add_string_size(source->argv[i], &sizes->arguments)) {
      return false;
    }
  }

  sizes->metadata = (sizes->metadata + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  sizes->arguments = (sizes->arguments + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  return sizes->arguments <= STARTUP_MAX_SIZE - sizes->metadata;
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
                         const struct process_startup *source,
                         const struct startup_sizes *sizes)
{
  struct startup_info *info = (struct startup_info *)buffer;
  *info = (struct startup_info){
    .version = STARTUP_VERSION,
    .size = sizes->metadata + sizes->arguments,
    .read_only_size = sizes->metadata,
    .resource_count = source->resource_count,
    .root_count = source->root_count,
    .working_directory_count = source->working_directory_count,
    .environment_count = source->environment_count,
    .argc = source->argc,
    .argv = address + sizes->metadata,
  };
  memcpy(info->streams, source->streams, sizeof(info->streams));

  size_t offset = sizeof(*info);
  struct startup_binding *resources = (struct startup_binding *)(buffer + offset);
  if (source->resource_count) {
    info->resources = address + offset;
    offset += source->resource_count * sizeof(*resources);
  }
  struct startup_binding *roots = (struct startup_binding *)(buffer + offset);
  if (source->root_count) {
    info->roots = address + offset;
    offset += source->root_count * sizeof(*roots);
  }
  if (source->working_directory_count) {
    info->working_directories = address + offset;
    size_t bytes = source->working_directory_count * sizeof(handle_t);
    memcpy(buffer + offset, source->working_directories, bytes);
    offset += bytes;
  }
  struct startup_variable *environment = (struct startup_variable *)(buffer + offset);
  if (source->environment_count) {
    info->environment = address + offset;
    offset += source->environment_count * sizeof(*environment);
  }

  for (size_t i = 0; i < source->resource_count; ++i) {
    resources[i].name = copy_string(buffer, &offset, address, source->resources[i].name);
    resources[i].handle = source->resources[i].handle;
  }
  for (size_t i = 0; i < source->root_count; ++i) {
    roots[i].name = copy_string(buffer, &offset, address, source->roots[i].name);
    roots[i].handle = source->roots[i].handle;
  }
  if (source->working_path) {
    info->working_path = copy_string(buffer, &offset, address, source->working_path);
  }
  for (size_t i = 0; i < source->environment_count; ++i) {
    environment[i].name = copy_string(buffer, &offset, address, source->environment[i].name);
    environment[i].value = copy_string(buffer, &offset, address, source->environment[i].value);
  }

  uint64_t *argv = (uint64_t *)(buffer + sizes->metadata);
  offset = sizes->metadata + (source->argc + 1) * sizeof(*argv);
  for (size_t i = 0; i < source->argc; ++i) {
    argv[i] = copy_string(buffer, &offset, address, source->argv[i]);
  }
  /* The zeroed buffer supplies argv[argc] and all page padding. */
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

enum mm_result process_prepare_startup(struct process *process,
                                       const struct process_startup *source)
{
  KASSERT(arch_cpu_index() == 0);
  struct startup_sizes sizes;
  if (!process || process->startup_address || !measure_startup(process, source, &sizes)) {
    return MM_INVALID;
  }

  size_t total = sizes.metadata + sizes.arguments;
  uint8_t *buffer = kmalloc(total);
  if (!buffer) {
    return MM_NO_MEMORY;
  }
  memset(buffer, 0, total);

  uintptr_t address;
  enum mm_result result = vm_alloc(process->address_space, total, PAGE_SIZE,
                                    PAGE_USER, &address);
  if (result != MM_OK) {
    kfree(buffer);
    return result;
  }
  fill_startup(buffer, address, source, &sizes);
  result = copy_startup_pages(process->address_space, address, buffer, total);
  kfree(buffer);

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
    return result;
  }
  process->startup_address = address;
  return MM_OK;
}

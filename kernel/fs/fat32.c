#include <abi/mount.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/extents.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/metadata.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <pyxis_fs/fat32.h>
#include "npfs_store.h"

#define FAT32_MOUNT_LIMIT 4u
#define FAT32_WORKSPACE_BYTES (32u * 1024u * 1024u)
#define FAT32_STEP_LIMIT UINT64_C(67108864)
#define FAT32_SECTOR_LIMIT UINT64_C(16777216)
#define FAT32_DEPTH_LIMIT 64u

struct fat32_mount {
  struct fat32_mount *next;
  struct storage_extent extent;
  block_device_id device;
  struct fat32_volume volume;
  struct fat32_workspace workspace;
  struct npfs_store_context *context; /* Borrowed only while this worker executes. */
  struct fat32_node *nodes;
  uint64_t domain;
  size_t data_nodes, controls;
  enum call_status backing_status;
  bool probing, published, retired;
};

struct fat32_node {
  struct fat32_node *next, *retired_next;
  struct fat32_mount *mount;
  struct fat32_locator locator;
  uint64_t id, kind, ceiling;
  atomic_size_t admitted;
  atomic_bool returned;
  struct execution_group *cleanup_group;
  union {
    struct directory_object directory;
    struct file_object file;
    struct mount_object control;
  } wrapper;
};

static struct fat32_mount *mounts;
static struct fat32_node *retired_nodes;
static size_t instance_count, workspace_used;

static struct kernel_object *node_object(struct fat32_node *node)
{
  if (node->kind == OBJECT_MOUNT) {
    return &node->wrapper.control.object;
  }
  return node->kind == OBJECT_DIRECTORY ? &node->wrapper.directory.object :
      &node->wrapper.file.object;
}

static enum call_status block_status(enum block_result result)
{
  switch (result) {
  case BLOCK_OK: return CALL_OK;
  case BLOCK_TIMED_OUT: return CALL_TIMED_OUT;
  case BLOCK_UNAVAILABLE:
  case BLOCK_UNSUPPORTED: return CALL_UNAVAILABLE;
  default: return CALL_IO;
  }
}

static bool read_sectors(void *argument, uint64_t first, uint32_t count, void *bytes)
{
  struct fat32_mount *mount = argument;
  struct npfs_store_context *context = mount->context;
  KASSERT(context);
  if (!count || count > DISK_IO_MAX_BYTES / FAT32_SECTOR_BYTES ||
      (!mount->probing && (first < mount->extent.first_block ||
       first - mount->extent.first_block >= mount->extent.block_count ||
       count > mount->extent.block_count - (first - mount->extent.first_block)))) {
    mount->backing_status = CALL_IO;
    return false;
  }
  struct block_ticket ticket;
  enum block_result result;
  for (;;) {
    if (task_deadline_expired(context->deadline)) {
      mount->backing_status = CALL_TIMED_OUT;
      return false;
    }
    uint64_t flags = cpu_save_interrupts();
    result = block_submit(mount->extent.device, BLOCK_READ, first, count, NULL, &ticket);
    cpu_restore_interrupts(flags);
    if (result != BLOCK_FULL) {
      break;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  if (result == BLOCK_OK) {
    result = block_wait(&ticket, context->deadline);
    uint64_t flags = cpu_save_interrupts();
    if (result != BLOCK_OK) {
      KASSERT(block_abandon(&ticket) == BLOCK_OK);
    } else {
      struct block_completion completion;
      KASSERT(block_collect(&ticket, bytes, count * FAT32_SECTOR_BYTES, &completion) == BLOCK_OK);
      result = completion.result;
      if (result == BLOCK_OK && completion.bytes != count * FAT32_SECTOR_BYTES) {
        result = BLOCK_IO_ERROR;
      }
    }
    cpu_restore_interrupts(flags);
  }
  if (result != BLOCK_OK) {
    context->backing_error = result;
    mount->backing_status = block_status(result);
    return false;
  }
  return true;
}

static enum call_status format_status(struct fat32_mount *mount, enum fat32_status status)
{
  switch (status) {
  case FAT32_OK: return CALL_OK;
  case FAT32_INVALID: return CALL_BAD_REQUEST;
  case FAT32_UNSUPPORTED: return CALL_UNAVAILABLE;
  case FAT32_IO: return mount->backing_status == CALL_OK ? CALL_IO : mount->backing_status;
  case FAT32_NO_MEMORY: return CALL_NO_MEMORY;
  case FAT32_LIMIT: return CALL_LIMIT;
  case FAT32_NAME_TOO_LONG: return CALL_NAME_TOO_LONG;
  case FAT32_NOT_FOUND: return CALL_NOT_FOUND;
  case FAT32_NOT_DIRECTORY:
  case FAT32_IS_DIRECTORY: return CALL_WRONG_TYPE;
  default: return CALL_IO;
  }
}

static void free_workspace(struct fat32_mount *mount)
{
  if (!mount->workspace.memory) {
    return;
  }
  KASSERT(mount->workspace.bytes <= workspace_used);
  workspace_used -= mount->workspace.bytes;
  uint64_t flags = cpu_save_interrupts();
  kfree(mount->workspace.memory);
  cpu_restore_interrupts(flags);
  mount->workspace.memory = NULL;
  mount->workspace.bytes = 0;
}

static void remove_instance(struct fat32_mount *mount)
{
  KASSERT(!mount->data_nodes && !mount->controls && !mount->nodes && !mount->context);
  if (!mount->retired) {
    free_workspace(mount);
    storage_extent_release(&mount->extent);
    KASSERT(instance_count);
    --instance_count;
  }
  struct fat32_mount **link = &mounts;
  while (*link != mount) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = mount->next;
  npfs_adapter_free(mount, sizeof(*mount));
}

void fat32_mark_returned(struct fat32_node *node)
{
  atomic_store_explicit(&node->returned, true, memory_order_release);
}

void fat32_request_published(struct fat32_node *node)
{
  atomic_fetch_add_explicit(&node->admitted, 1, memory_order_relaxed);
}

void fat32_request_complete(struct fat32_node *node)
{
  KASSERT(atomic_fetch_sub_explicit(&node->admitted, 1, memory_order_relaxed));
}

static bool other_requests(struct fat32_mount *mount, struct fat32_node *current)
{
  for (struct fat32_node *node = mount->nodes; node; node = node->next) {
    size_t own = node == current ? 1 : 0;
    if (atomic_load_explicit(&node->admitted, memory_order_acquire) > own) {
      return true;
    }
  }
  return false;
}

void fat32_retire_node(struct fat32_node *node)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  node->cleanup_group = object_cleanup_defer();
  node->retired_next = retired_nodes;
  retired_nodes = node;
  npfs_notify();
}

struct fat32_node *fat32_take_retired(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct fat32_node *nodes = retired_nodes;
  retired_nodes = NULL;
  return nodes;
}

void fat32_cleanup_nodes(struct fat32_node *nodes)
{
  npfs_require_worker();
  while (nodes) {
    struct fat32_node *node = nodes;
    nodes = node->retired_next;
    struct fat32_mount *mount = node->mount;
    struct fat32_node **link = &mount->nodes;
    while (*link != node) {
      KASSERT(*link);
      link = &(*link)->next;
    }
    *link = node->next;
    if (node->kind == OBJECT_MOUNT) {
      if (atomic_load_explicit(&node->returned, memory_order_acquire)) {
        mount->published = true;
      }
      KASSERT(mount->controls);
      --mount->controls;
    } else {
      KASSERT(mount->data_nodes);
      --mount->data_nodes;
    }
    struct execution_group *group = node->cleanup_group;
    npfs_adapter_free(node, sizeof(*node));
    if (!mount->data_nodes && !mount->controls && (mount->retired || !mount->published)) {
      remove_instance(mount);
    }
    if (group) {
      uint64_t flags = cpu_save_interrupts();
      execution_group_cleanup_end(group);
      cpu_restore_interrupts(flags);
    }
  }
}

static bool same_locator(const struct fat32_locator *a, const struct fat32_locator *b)
{
  return a->parent_cluster == b->parent_cluster &&
      a->parent_parent_cluster == b->parent_parent_cluster &&
      a->short_entry_slot == b->short_entry_slot;
}

static enum call_status wrap(struct fat32_mount *mount, uint64_t kind,
    const struct fat32_locator *locator, uint64_t ceiling, struct kernel_object **out)
{
  if (kind != OBJECT_MOUNT) {
    for (struct fat32_node *node = mount->nodes; node; node = node->next) {
      if (node->kind == kind && same_locator(&node->locator, locator)) {
        uint64_t flags = cpu_save_interrupts();
        /* A cached wrapper can reach zero on an AP while still awaiting this
         * worker's destruction. Try-retain never resurrects that object. */
        struct kernel_object *object = node_object(node);
        size_t references = atomic_load_explicit(&object->references, memory_order_relaxed);
        bool retained = false;
        while (references && references != SIZE_MAX) {
          if (atomic_compare_exchange_weak_explicit(&object->references, &references,
              references + 1, memory_order_relaxed, memory_order_relaxed)) {
            retained = true;
            break;
          }
        }
        cpu_restore_interrupts(flags);
        if (references == SIZE_MAX) {
          return CALL_LIMIT;
        }
        if (retained) {
          *out = node_object(node);
          return CALL_OK;
        }
      }
    }
  }
  struct fat32_node *node;
  enum call_status status = npfs_adapter_allocate(sizeof(*node), (void **)&node);
  if (status != CALL_OK) {
    return status;
  }
  atomic_init(&node->admitted, 0);
  atomic_init(&node->returned, false);
  node->mount = mount;
  node->kind = kind;
  node->ceiling = ceiling;
  if (locator) {
    node->locator = *locator;
  }
  uint64_t flags = cpu_save_interrupts();
  bool identified = kind == OBJECT_MOUNT || fs_metadata_allocate_id(&node->id);
  if (identified) {
    if (kind == OBJECT_DIRECTORY) {
      directory_init_fat32(&node->wrapper.directory, node);
    } else if (kind == OBJECT_FILE) {
      file_init_fat32(&node->wrapper.file, node);
    } else {
      mount_init_fat32(&node->wrapper.control, node);
    }
  }
  cpu_restore_interrupts(flags);
  if (!identified) {
    npfs_adapter_free(node, sizeof(*node));
    return CALL_LIMIT;
  }
  node->next = mount->nodes;
  mount->nodes = node;
  if (kind == OBJECT_MOUNT) {
    ++mount->controls;
  } else {
    ++mount->data_nodes;
  }
  *out = node_object(node);
  return CALL_OK;
}

static enum call_status select_partition(struct npfs_store_context *context,
    struct npfs_job *job, struct gpt_partition *partition, struct block_info *info)
{
  static const struct gpt_guid esp_type = {.bytes = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
  }};
  for (;;) {
    bool pending = false;
    size_t matches = 0;
    block_device_id selected = 0;
    struct gpt_partition found = {0};
    struct gpt_guid disk = {0};
    uint64_t flags = cpu_save_interrupts();
    if (job->device && (!block_installer_inventory_available() ||
        !block_installer_device(job->device))) {
      cpu_restore_interrupts(flags);
      return CALL_UNAVAILABLE;
    }
    pending = !job->device && !block_discovery_finished();
    for (size_t i = 0; i < block_device_count(); ++i) {
      block_device_id id = block_device_at(i);
      if (job->device && job->device != id) {
        continue;
      }
      const struct gpt_snapshot *snapshot = gpt_get_snapshot(id);
      if (!snapshot) {
        pending = true;
        continue;
      }
      if (snapshot->status != GPT_HEALTHY) {
        if (job->device) {
          enum call_status status;
          switch (snapshot->status) {
          case GPT_ABSENT: status = CALL_NOT_FOUND; break;
          case GPT_UNAVAILABLE:
          case GPT_UNSUPPORTED: status = CALL_UNAVAILABLE; break;
          case GPT_NO_MEMORY: status = CALL_NO_MEMORY; break;
          case GPT_TIMED_OUT: status = CALL_TIMED_OUT; break;
          default: status = CALL_IO; break;
          }
          cpu_restore_interrupts(flags);
          return status;
        }
        continue;
      }
      if (!job->device && memcmp(snapshot->disk_guid.bytes, job->origin.disk, 16)) {
        continue;
      }
      for (size_t p = 0; p < snapshot->partition_count; ++p) {
        const struct gpt_partition *entry = &snapshot->partitions[p];
        if ((job->device && entry->entry_number != job->partition) ||
            (!job->device && (memcmp(entry->guid.bytes, job->origin.partition, 16) ||
             memcmp(entry->type.bytes, esp_type.bytes, sizeof(esp_type.bytes))))) {
          continue;
        }
        found = *entry;
        disk = snapshot->disk_guid;
        selected = id;
        ++matches;
      }
    }
    enum block_result result = matches == 1 ? block_get_info(selected, info) : BLOCK_UNAVAILABLE;
    cpu_restore_interrupts(flags);
    if (pending) {
      if (task_deadline_expired(context->deadline)) {
        return CALL_TIMED_OUT;
      }
      kernel_task_sleep_until(task_deadline_after_ms(1));
      continue;
    }
    if (matches != 1) {
      return matches ? CALL_IO : CALL_NOT_FOUND;
    }
    if (result != BLOCK_OK) {
      return block_status(result);
    }
    if (info->block_size != FAT32_SECTOR_BYTES || info->max_transfer < FAT32_SECTOR_BYTES) {
      return CALL_UNAVAILABLE;
    }
    if (info->write_failed) {
      return CALL_IO;
    }
    job->device = selected;
    job->disk = disk;
    job->partition = found.entry_number;
    *partition = found;
    return CALL_OK;
  }
}

static enum call_status allocate_workspace(struct fat32_mount *mount, size_t bytes)
{
  if (!bytes || bytes > FAT32_WORKSPACE_BYTES - workspace_used) {
    return CALL_LIMIT;
  }
  workspace_used += bytes;
  uint64_t flags = cpu_save_interrupts();
  void *memory = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!memory) {
    workspace_used -= bytes;
    return CALL_NO_MEMORY;
  }
  mount->workspace = (struct fat32_workspace){memory, bytes, FAT32_STEP_LIMIT,
      FAT32_SECTOR_LIMIT, FAT32_DEPTH_LIMIT};
  return CALL_OK;
}

static enum call_status acquire_mount(struct npfs_store_context *context,
    struct npfs_job *job, struct fat32_mount **out)
{
  struct gpt_partition partition;
  struct block_info info;
  enum call_status status = select_partition(context, job, &partition, &info);
  if (status != CALL_OK) {
    return status;
  }
  for (struct fat32_mount *mount = mounts; mount; mount = mount->next) {
    if (!mount->retired && mount->extent.device == job->device &&
        mount->extent.first_block == partition.first_block &&
        mount->extent.block_count == partition.block_count) {
      if (mount->volume.gpt_entry != job->partition ||
          memcmp(mount->volume.partition_guid, partition.guid.bytes, 16)) {
        return CALL_BUSY;
      }
      mount->context = context;
      mount->backing_status = CALL_OK;
      *out = mount;
      return CALL_OK;
    }
  }
  if (instance_count == FAT32_MOUNT_LIMIT) {
    return CALL_LIMIT;
  }
  struct fat32_mount *mount;
  status = npfs_adapter_allocate(sizeof(*mount), (void **)&mount);
  if (status != CALL_OK) {
    return status;
  }
  status = storage_extent_reserve(&mount->extent, job->device, partition.first_block,
      partition.block_count, STORAGE_EXTENT_PENDING_FAT);
  if (status != CALL_OK) {
    npfs_adapter_free(mount, sizeof(*mount));
    return status;
  }
  ++instance_count;
  mount->device = job->device;
  mount->next = mounts;
  mounts = mount;
  mount->context = context;
  mount->probing = true;
  status = allocate_workspace(mount, fat32_open_workspace_size());
  if (status == CALL_OK) {
    struct fat32_source source = {read_sectors, mount, info.block_count,
        info.block_size, info.max_transfer / info.block_size};
    if (source.max_read_sectors > DISK_IO_MAX_BYTES / FAT32_SECTOR_BYTES) {
      source.max_read_sectors = DISK_IO_MAX_BYTES / FAT32_SECTOR_BYTES;
    }
    status = format_status(mount, fat32_open(&source, job->partition,
        &mount->workspace, &mount->volume));
  }
  mount->probing = false;
  if (status == CALL_OK && (mount->volume.partition_first != partition.first_block ||
      mount->volume.partition_sectors != partition.block_count ||
      memcmp(mount->volume.partition_guid, partition.guid.bytes, 16) ||
      memcmp(mount->volume.disk_guid, job->disk.bytes, 16) ||
      memcmp(mount->volume.partition_type, partition.type.bytes, 16) ||
      mount->volume.partition_attributes != partition.attributes)) {
    status = CALL_IO;
  }
  size_t needed = 0;
  if (status == CALL_OK) {
    status = format_status(mount, fat32_workspace_size(&mount->volume, FAT32_DEPTH_LIMIT, &needed));
  }
  if (status == CALL_OK) {
    free_workspace(mount);
    status = allocate_workspace(mount, needed);
  }
  if (status == CALL_OK) {
    struct fat32_check_report report;
    status = format_status(mount, fat32_check(&mount->volume, &mount->workspace, &report));
  }
  if (status == CALL_OK) {
    uint64_t flags = cpu_save_interrupts();
    bool identified = fs_metadata_allocate_id(&mount->domain);
    cpu_restore_interrupts(flags);
    status = identified ? CALL_OK : CALL_LIMIT;
  }
  if (status != CALL_OK) {
    mount->context = NULL;
    remove_instance(mount);
    return status;
  }
  storage_extent_publish(&mount->extent, STORAGE_EXTENT_FAT);
  *out = mount;
  return CALL_OK;
}

static enum call_status root(struct fat32_mount *mount, uint64_t rights,
    struct kernel_object **out)
{
  struct fat32_entry entry;
  struct fat32_locator locator;
  enum call_status status = format_status(mount,
      fat32_root(&mount->volume, &mount->workspace, &entry, &locator));
  return status == CALL_OK ? wrap(mount, OBJECT_DIRECTORY, &locator, rights, out) : status;
}

struct byte_output { uint8_t *bytes; size_t used, capacity; };
static bool collect_bytes(void *argument, const void *bytes, size_t length)
{
  struct byte_output *output = argument;
  if (length > output->capacity - output->used) {
    return false;
  }
  memcpy(output->bytes + output->used, bytes, length);
  output->used += length;
  return true;
}

static enum call_status data_operation(struct npfs_job *job, struct fat32_mount *mount)
{
  struct fat32_node *node = job->fat_node;
  bool directory = node->kind == OBJECT_DIRECTORY;
  uint64_t all = directory ? FAT32_ROOT_RIGHTS : FILE_RIGHT_READ;
  if (job->rights & ~all) {
    return CALL_DENIED;
  }
  struct fat32_entry entry;
  enum call_status status;
  switch (job->operation) {
  case NPFS_LOOKUP: {
    if (!directory || !(job->rights & DIRECTORY_RIGHT_LOOKUP)) {
      return CALL_DENIED;
    }
    uint64_t allowed = job->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
        (job->rights & DIRECTORY_RIGHT_READ_FILES ? FILE_RIGHT_READ : 0);
    if ((job->kind != DIRECTORY_KIND_FILE && job->kind != DIRECTORY_KIND_DIRECTORY) ||
        (job->child_rights & ~allowed)) {
      return CALL_DENIED;
    }
    struct fat32_locator child;
    status = format_status(mount, fat32_lookup(&mount->volume, &node->locator,
        job->name, job->count, &mount->workspace, &entry, &child));
    if (status != CALL_OK) {
      return status;
    }
    uint64_t kind = entry.attributes & FAT32_ATTR_DIRECTORY ? OBJECT_DIRECTORY : OBJECT_FILE;
    if ((kind == OBJECT_DIRECTORY) != (job->kind == DIRECTORY_KIND_DIRECTORY)) {
      return CALL_WRONG_TYPE;
    }
    return wrap(mount, kind, &child, job->child_rights, &job->object);
  }
  case NPFS_ENUMERATE: {
    if (!directory || !(job->rights & DIRECTORY_RIGHT_ENUMERATE)) {
      return CALL_DENIED;
    }
    if (!job->cursor.generation && job->cursor.position) {
      return CALL_BAD_REQUEST;
    }
    struct directory_enumerate_reply reply = {.cursor = job->cursor};
    if (job->cursor.generation && job->cursor.generation != node->id) {
      reply.outcome = DIRECTORY_CHANGED;
      job->entry = reply;
      return CALL_OK;
    }
    uint64_t next;
    bool found;
    struct fat32_locator child;
    status = format_status(mount, fat32_enumerate(&mount->volume, &node->locator,
        job->cursor.position, &mount->workspace, &entry, &child, &next, &found));
    if (status != CALL_OK) {
      return status;
    }
    if (!found) {
      reply.outcome = DIRECTORY_END;
      reply.cursor = (struct directory_cursor){node->id, next};
    } else {
      reply.name_size = entry.name_bytes + 1;
      reply.kind = entry.attributes & FAT32_ATTR_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
      if (job->count < reply.name_size) {
        reply.outcome = DIRECTORY_BUFFER_TOO_SMALL;
      } else {
        reply.outcome = DIRECTORY_ENTRY;
        reply.cursor = (struct directory_cursor){node->id, next};
        memcpy(job->name, entry.name, entry.name_bytes);
        job->name[entry.name_bytes] = 0;
      }
    }
    job->entry = reply;
    return CALL_OK;
  }
  case NPFS_READ: {
    if (directory || !(job->rights & FILE_RIGHT_READ) || job->count > sizeof(job->data)) {
      return CALL_BAD_REQUEST;
    }
    struct byte_output output = {job->data, 0, job->count};
    status = format_status(mount, fat32_read_range(&mount->volume, &node->locator,
        job->offset, job->count, &mount->workspace, collect_bytes, &output));
    if (status == CALL_OK) {
      job->count = output.used;
    }
    return status;
  }
  case NPFS_CAPTURE: {
    if (directory || !(job->rights & FILE_RIGHT_READ)) {
      return CALL_DENIED;
    }
    status = format_status(mount, fat32_locate(&mount->volume, &node->locator,
        &mount->workspace, &entry));
    if (status != CALL_OK) {
      return status;
    }
    uint64_t flags = cpu_save_interrupts();
    status = image_capture_allocate(entry.file_bytes, &job->captured);
    cpu_restore_interrupts(flags);
    if (status != CALL_OK) {
      return status;
    }
    struct byte_output output = {(void *)job->captured.address, 0, job->captured.size};
    status = format_status(mount, fat32_read_range(&mount->volume, &node->locator,
        0, job->captured.size, &mount->workspace, collect_bytes, &output));
    return status == CALL_OK && output.used != job->captured.size ? CALL_IO : status;
  }
  case NPFS_SIZE:
  case NPFS_INFO:
    status = format_status(mount, fat32_locate(&mount->volume, &node->locator,
        &mount->workspace, &entry));
    if (status == CALL_OK) {
      job->offset = directory ? 0 : entry.file_bytes;
      job->file_info = (struct file_info_reply){
        .valid = FILE_INFO_DOMAIN_VALID | FILE_INFO_OBJECT_VALID |
            (directory ? 0 : FILE_INFO_SIZE_VALID),
        .domain = mount->domain, .object = node->id, .size = job->offset,
      };
    }
    return status;
  default: return CALL_BAD_OPERATION;
  }
}

static void state(struct fat32_mount *mount, struct mount_state_reply *reply)
{
  *reply = (struct mount_state_reply){mount->domain, mount->device,
      mount->volume.gpt_entry, mount->volume.partition_first, mount->volume.partition_sectors,
      (mount->retired ? MOUNT_STATE_RETIRED : 0) |
      (mount->volume.clean_shutdown ? MOUNT_STATE_CLEAN_SHUTDOWN : 0) |
      (mount->volume.no_hard_error ? MOUNT_STATE_NO_HARD_ERROR : 0)};
}

enum call_status fat32_perform(struct npfs_store_context *context, struct npfs_job *job)
{
  npfs_require_worker();
  struct fat32_mount *mount = job->fat_node ? job->fat_node->mount : NULL;
  enum call_status status;
  if (job->operation == NPFS_FAT_OPEN) {
    status = acquire_mount(context, job, &mount);
    if (status != CALL_OK) {
      return status;
    }
    status = root(mount, job->rights, &job->object);
    if (status == CALL_OK) {
      status = wrap(mount, OBJECT_MOUNT, NULL, job->rights, &job->control);
    }
    mount->context = NULL;
    if (status != CALL_OK && !mount->nodes && !mount->published) {
      remove_instance(mount);
    }
    return status;
  }
  if (!mount) {
    return CALL_BAD_REQUEST;
  }
  mount->context = context;
  mount->backing_status = CALL_OK;
  switch (job->operation) {
  case NPFS_FAT_STATE:
    mount->published = true;
    state(mount, &job->mount_state);
    status = CALL_OK;
    break;
  case NPFS_FAT_RETIRE:
    mount->published = true;
    if (mount->retired) {
      status = CALL_OK;
    } else if (mount->data_nodes || other_requests(mount, job->fat_node)) {
      status = CALL_BUSY;
    } else {
      free_workspace(mount);
      storage_extent_release(&mount->extent);
      KASSERT(instance_count);
      --instance_count;
      mount->retired = true;
      status = CALL_OK;
    }
    break;
  case NPFS_FAT_ROOT:
    status = (job->rights & ~job->fat_node->ceiling) ? CALL_DENIED :
        mount->retired ? CALL_UNAVAILABLE : root(mount, job->rights, &job->object);
    if (status == CALL_OK) {
      mount->published = true;
    }
    break;
  default:
    status = mount->retired ? CALL_UNAVAILABLE : data_operation(job, mount);
    if (status == CALL_OK) {
      mount->published = true;
    }
    break;
  }
  mount->context = NULL;
  return status;
}

static enum call_status capability_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: return CALL_DENIED;
  }
}

static struct syscall_result open_pair(block_device_id device, const struct boot_origin *origin,
    uint32_t partition, uint64_t rights, uintptr_t reply_address, size_t reply_capacity)
{
  if ((rights & ~FAT32_ROOT_RIGHTS) || reply_capacity < sizeof(struct mount_fat32_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct mount_fat32_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  enum call_status status = capability_status(capability_request_reservation(2, &reservation, slots));
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct npfs_request *request = npfs_request_prepare(NPFS_FAT_OPEN);
  request->job.device = device;
  request->job.partition = partition;
  request->job.rights = rights;
  if (origin) {
    request->job.origin = *origin;
  }
  npfs_request_submit_and_wait(request);
  status = request->job.status;
  struct kernel_object *objects[2] = {request->job.object, request->job.control};
  request->job.object = NULL;
  request->job.control = NULL;
  npfs_request_release(request);
  struct capability_grant grants[2] = {0};
  struct mount_fat32_reply reply = {0};
  if (status == CALL_OK) {
    KASSERT(objects[0] && objects[1]);
    if (task_stop_requested()) {
      status = CALL_ENDPOINT_CLOSED;
    } else {
      status = capability_status(capability_grant_retain(objects[0], rights, 0, &grants[0]));
      if (status == CALL_OK) {
        status = capability_status(capability_grant_retain(objects[1],
            MOUNT_RIGHT_OPEN_ROOT | MOUNT_RIGHT_OBSERVE | MOUNT_RIGHT_RETIRE, 0, &grants[1]));
      }
      if (status == CALL_OK) {
        status = capability_status(capability_validate_grants(reservation.table, grants, 2));
      }
      if (status == CALL_OK) {
        handle_t handles[2];
        capability_install_reserved(&reservation, slots, grants, 2, handles);
        reply = (struct mount_fat32_reply){handles[0], handles[1]};
      }
    }
  }
  for (size_t i = 0; i < 2; ++i) {
    capability_grant_release(&grants[i]);
  }
  capability_reservation_release(&reservation, slots);
  if (status == CALL_OK) {
    handle_t handles[] = {reply.root, reply.control};
    if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), handles, 2)) {
      status = CALL_BAD_BUFFER;
    } else {
      /* A successful return makes final control close ordinary retention. Failed
       * unpublished opens instead drain their wrappers before provisional abort. */
      fat32_mark_returned(((struct mount_object *)objects[1])->fat32);
    }
  }
  for (size_t i = 0; i < 2; ++i) {
    if (objects[i]) {
      object_release(objects[i]);
    }
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}

struct syscall_result fat32_open_device(block_device_id device,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct disk_fat32_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!request.partition || request.partition > UINT32_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  return open_pair(device, NULL, request.partition, request.rights, reply_address, reply_capacity);
}

struct syscall_result fat32_mount_call(struct mount_object *mount, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (mount->backend == MOUNT_BOOT_ESP) {
    if (operation != MOUNT_OPEN_FAT32) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    if (!(rights & MOUNT_RIGHT_OPEN_ROOT)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    struct mount_fat32_root_request request;
    if (request_size != sizeof(request)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return open_pair(0, &mount->origin, 0, request.rights, reply_address, reply_capacity);
  }
  enum npfs_operation op;
  uint64_t required;
  size_t reply_size;
  switch (operation) {
  case MOUNT_OPEN_ROOT:
    op = NPFS_FAT_ROOT; required = MOUNT_RIGHT_OPEN_ROOT;
    reply_size = sizeof(struct mount_reply);
    break;
  case MOUNT_OBSERVE:
    op = NPFS_FAT_STATE; required = MOUNT_RIGHT_OBSERVE;
    reply_size = sizeof(struct mount_state_reply);
    break;
  case MOUNT_RETIRE:
    op = NPFS_FAT_RETIRE; required = MOUNT_RIGHT_RETIRE;
    reply_size = 0;
    break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t root_rights = 0;
  if (op == NPFS_FAT_ROOT) {
    struct mount_fat32_root_request request;
    if (request_size != sizeof(request)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (request.rights & ~FAT32_ROOT_RIGHTS) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (request.rights & ~mount->fat32->ceiling) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    root_rights = request.rights;
  } else if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (op == NPFS_FAT_ROOT) {
    enum call_status status = capability_status(capability_request_reservation(1, &reservation, &slot));
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  struct npfs_request *request = npfs_request_prepare(op);
  request->job.fat_node = mount->fat32;
  request->job.rights = root_rights;
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  struct mount_state_reply observed = request->job.mount_state;
  struct kernel_object *root_object = request->job.object;
  request->job.object = NULL;
  npfs_request_release(request);
  if (op == NPFS_FAT_ROOT) {
    struct capability_grant grant = {0};
    struct mount_reply reply = {0};
    if (status == CALL_OK) {
      status = task_stop_requested() ? CALL_ENDPOINT_CLOSED :
          capability_status(capability_grant_retain(root_object, root_rights, 0, &grant));
      if (status == CALL_OK) {
        status = capability_status(capability_validate_grants(reservation.table, &grant, 1));
      }
      if (status == CALL_OK) {
        capability_install_reserved(&reservation, &slot, &grant, 1, &reply.root);
      }
    }
    capability_grant_release(&grant);
    capability_reservation_release(&reservation, &slot);
    if (root_object) {
      object_release(root_object);
    }
    if (status == CALL_OK && !copy_returned_handles_to_user(reply_address,
        &reply, sizeof(reply), &reply.root, 1)) {
      status = CALL_BAD_BUFFER;
    }
  } else if (status == CALL_OK && reply_size &&
      !copy_to_user(reply_address, &observed, reply_size)) {
    status = CALL_BAD_BUFFER;
  }
  return (struct syscall_result){status, status == CALL_OK ? reply_size : 0};
}

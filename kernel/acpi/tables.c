#include <kernel/acpi/tables.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>
#include "host.h"

/* The interpreter's installed-table array, exported by name. Only the ACPI
 * worker reads the array; the queue below carries requests to it. A table's
 * bytes are copied into a sealed FILE the first time it is opened, and every
 * later open shares that snapshot: the array entry may point into the firmware
 * mapping window or the interpreter's heap, neither of which a user mapping may
 * reference. Retained memory is therefore bounded by the firmware's own tables,
 * however often they are opened. */

/* Signatures whose bytes never leave the kernel. MSDM carries the OEM product
 * key; it is still listed, with its length, so the gap is visible. */
static const char *const withheld_signatures[] = {"MSDM"};

/* BSP, IF=0: available and the queue. The worker pops with IF=0 and runs the
 * request with IF=1. */
static bool available;
static struct acpi_tables_request *queue_head, *queue_tail;

/* Worker only. One sealed snapshot per table index, filled on first open and
 * holding one reference; opens retain it for their caller. The interpreter's
 * array only grows while its tables stay installed, so an index keeps its
 * bytes. A heap array sized to the table count when last needed; entries are
 * NULL until opened. */
static struct file_object **snapshots;
static size_t snapshot_capacity;

static void require_bsp_if0(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

void acpi_tables_available(void)
{
  acpi_require_worker();
  uint64_t flags = cpu_save_interrupts();
  available = true;
  cpu_restore_interrupts(flags);
}

void acpi_tables_forward(struct acpi_tables_request *request)
{
  require_bsp_if0();
  if (!available) {
    request->status = CALL_UNAVAILABLE;
    bsp_request_complete(&request->request);
    return;
  }
  request->next = NULL;
  if (queue_tail) {
    queue_tail->next = request;
  } else {
    queue_head = request;
  }
  queue_tail = request;
  acpi_worker_notify();
}

bool acpi_tables_pending(void)
{
  require_bsp_if0();
  return queue_head != NULL;
}

static bool withheld(const char signature[4])
{
  for (size_t i = 0; i < sizeof(withheld_signatures) / sizeof(withheld_signatures[0]); ++i) {
    if (!memcmp(signature, withheld_signatures[i], 4)) {
      return true;
    }
  }
  return false;
}

/* Worker. The interpreter keeps its array only while its table subsystem is
 * initialized; a fatal namespace failure resets it, and the export then has
 * nothing to read. */
static bool tables_online(void)
{
  return uacpi_get_current_init_level() >= UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED;
}

static bool table_info(size_t index, uacpi_table_info *info)
{
  uacpi_status status = uacpi_table_info_get_by_index(index, info);
  if (status != UACPI_STATUS_OK) {
    kerror("ACPI: table %zu unreadable: %s\n", index, uacpi_status_to_string(status));
    return false;
  }
  return true;
}

/* Bytes a directory name cannot hold become '_'. Distinct signatures can
 * normalize to one name, so duplicates are counted after normalization. */
static void normalize_signature(const char signature[4], char name[4])
{
  for (size_t i = 0; i < 4; ++i) {
    unsigned char byte = signature[i];
    name[i] = byte < 0x20 || byte > 0x7e || byte == '/' ? '_' : (char)byte;
  }
}

/* A normalized signature occurring once names itself; otherwise its tables are
 * numbered from 1 in array order. Every base is exactly four bytes, so a
 * numbered name never equals another table's name. */
static bool table_name(size_t index, size_t count, char name[ACPI_TABLE_NAME_MAX + 1])
{
  uacpi_table_info info;
  if (!table_info(index, &info)) {
    return false;
  }
  char base[4];
  normalize_signature(info.signature, base);
  size_t same = 0, ordinal = 0;
  for (size_t i = 0; i < count; ++i) {
    uacpi_table_info other;
    if (!table_info(i, &other)) {
      return false;
    }
    char other_base[4];
    normalize_signature(other.signature, other_base);
    if (!memcmp(other_base, base, sizeof(base))) {
      ++same;
      if (i <= index) {
        ++ordinal;
      }
    }
  }
  memcpy(name, base, sizeof(base));
  name[sizeof(base)] = 0;
  if (same > 1) {
    /* Twenty decimal digits at most: the name buffer allows for them. */
    sprintf(name + sizeof(base), "%zu", ordinal);
  }
  return true;
}

/* Worker. Finds NAME in the current array; a missing name is NOT_FOUND. */
static enum call_status find_table(const char *name, size_t *index)
{
  size_t count = uacpi_table_count();
  size_t length = strlen(name);
  for (size_t i = 0; i < count; ++i) {
    char candidate[ACPI_TABLE_NAME_MAX + 1];
    if (!table_name(i, count, candidate)) {
      return CALL_UNAVAILABLE;
    }
    if (strlen(candidate) == length && !memcmp(candidate, name, length)) {
      *index = i;
      return CALL_OK;
    }
  }
  return CALL_NOT_FOUND;
}

static void fill_info(const uacpi_table_info *source, struct acpi_table_info *info)
{
  *info = (struct acpi_table_info){.length = source->size};
  memcpy(info->signature, source->signature, sizeof(info->signature));
  if (source->origin == UACPI_TABLE_ORIGIN_FIRMWARE_PHYSICAL ||
      source->origin == UACPI_TABLE_ORIGIN_HOST_PHYSICAL) {
    info->physical_address = source->phys_addr;
  }
  if (source->origin == UACPI_TABLE_ORIGIN_FIRMWARE_VIRTUAL) {
    info->flags |= ACPI_TABLE_RUNTIME_LOADED;
  }
  if (source->flags & UACPI_TABLE_CSUM_CHECKED) {
    info->flags |= ACPI_TABLE_CHECKSUM_CHECKED;
  }
  if (source->flags & UACPI_TABLE_CSUM_BAD) {
    info->flags |= ACPI_TABLE_CHECKSUM_BAD;
  }
  if (withheld(source->signature)) {
    info->flags |= ACPI_TABLE_WITHHELD;
  }
}

/* The cursor generation is the table count plus one, so it is never the zero
 * "unacquired" generation; a table installed later renumbers names, which
 * the changed count reports as CHANGED. */
static enum call_status enumerate(struct acpi_tables_request *request)
{
  size_t count = uacpi_table_count();
  uint64_t generation = count + 1;
  struct directory_enumerate_reply reply = {.cursor = request->cursor};
  if (request->cursor.generation && request->cursor.generation != generation) {
    reply.outcome = DIRECTORY_CHANGED;
  } else if (request->cursor.position > count) {
    return CALL_BAD_REQUEST;
  } else if (request->cursor.position == count) {
    reply.outcome = DIRECTORY_END;
    reply.cursor.generation = generation;
  } else {
    if (!table_name(request->cursor.position, count, request->name)) {
      return CALL_UNAVAILABLE;
    }
    reply.kind = DIRECTORY_KIND_FILE;
    reply.name_size = strlen(request->name) + 1;
    if (request->capacity < reply.name_size) {
      reply.outcome = DIRECTORY_BUFFER_TOO_SMALL;
    } else {
      reply.outcome = DIRECTORY_ENTRY;
      reply.cursor.generation = generation;
      ++reply.cursor.position;
    }
  }
  request->entry = reply;
  return CALL_OK;
}

static enum call_status query(struct acpi_tables_request *request)
{
  size_t index;
  enum call_status status = find_table(request->name, &index);
  if (status != CALL_OK) {
    return status;
  }
  uacpi_table_info info;
  if (!table_info(index, &info)) {
    return CALL_UNAVAILABLE;
  }
  fill_info(&info, &request->info);
  return CALL_OK;
}

/* Grows the snapshot array to cover COUNT tables. Heap use runs with IF=0, like
 * other BSP kernel-task heap use. */
static bool snapshots_cover(size_t count)
{
  if (count <= snapshot_capacity) {
    return true;
  }
  uint64_t flags = cpu_save_interrupts();
  struct file_object **grown = kmalloc(count * sizeof(*grown));
  if (grown) {
    memset(grown, 0, count * sizeof(*grown));
    if (snapshot_capacity) {
      memcpy(grown, snapshots, snapshot_capacity * sizeof(*grown));
      kfree(snapshots);
    }
    snapshots = grown;
    snapshot_capacity = count;
  }
  cpu_restore_interrupts(flags);
  return grown != NULL;
}

/* The interpreter dropped its tables: their snapshots can no longer be reached
 * by name. Open handles keep their own references. */
static void snapshots_drop(void)
{
  if (!snapshot_capacity) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  for (size_t i = 0; i < snapshot_capacity; ++i) {
    if (snapshots[i]) {
      object_release(&snapshots[i]->object);
    }
  }
  kfree(snapshots);
  snapshots = NULL;
  snapshot_capacity = 0;
  cpu_restore_interrupts(flags);
}

/* Copies the table into a sealed FILE the first time. Allocation and the
 * scratch-mapped copy run with IF=0, once per table; the interpreter's
 * reference keeps the source mapped until it is dropped. */
static enum call_status snapshot_table(size_t index)
{
  uacpi_table table;
  uacpi_status mapped = uacpi_table_get_by_index(index, &table);
  if (mapped != UACPI_STATUS_OK) {
    kerror("ACPI: table %zu unreadable: %s\n", index, uacpi_status_to_string(mapped));
    return CALL_UNAVAILABLE;
  }
  uint64_t flags = cpu_save_interrupts();
  struct file_object *file = file_prepare_snapshot(table.hdr->length);
  if (file) {
    file_fill_snapshot(file, table.ptr);
    snapshots[index] = file;
  }
  cpu_restore_interrupts(flags);
  uacpi_table_unref(&table);
  return file ? CALL_OK : CALL_NO_MEMORY;
}

static enum call_status open_table(struct acpi_tables_request *request)
{
  size_t index;
  enum call_status status = find_table(request->name, &index);
  if (status != CALL_OK) {
    return status;
  }
  if (request->kind != DIRECTORY_KIND_FILE && request->kind != DIRECTORY_KIND_ANY) {
    return CALL_WRONG_TYPE;
  }
  uacpi_table_info info;
  if (!table_info(index, &info)) {
    return CALL_UNAVAILABLE;
  }
  if (withheld(info.signature)) {
    return CALL_DENIED;
  }
  if (!snapshots_cover(uacpi_table_count())) {
    return CALL_NO_MEMORY;
  }
  if (!snapshots[index]) {
    status = snapshot_table(index);
    if (status != CALL_OK) {
      return status;
    }
  }
  uint64_t flags = cpu_save_interrupts();
  bool retained = object_retain(&snapshots[index]->object);
  cpu_restore_interrupts(flags);
  if (!retained) {
    return CALL_LIMIT;
  }
  request->file = snapshots[index];
  return CALL_OK;
}

static enum call_status execute(struct acpi_tables_request *request)
{
  if (!tables_online()) {
    snapshots_drop();
    return CALL_UNAVAILABLE;
  }
  switch (request->operation) {
  case ACPI_TABLES_ENUMERATE:
    return enumerate(request);
  case ACPI_TABLES_QUERY:
    return query(request);
  case ACPI_TABLES_OPEN:
    return open_table(request);
  }
  panic("unknown ACPI table operation %u", (unsigned)request->operation);
}

void acpi_tables_run(size_t limit)
{
  acpi_require_worker();
  for (size_t served = 0; served < limit; ++served) {
    uint64_t flags = cpu_save_interrupts();
    struct acpi_tables_request *request = queue_head;
    if (request) {
      queue_head = request->next;
      if (!queue_head) {
        queue_tail = NULL;
      }
      request->next = NULL;
    }
    cpu_restore_interrupts(flags);
    if (!request) {
      return;
    }
    request->status = execute(request);
    flags = cpu_save_interrupts();
    bsp_request_complete(&request->request);
    cpu_restore_interrupts(flags);
  }
}

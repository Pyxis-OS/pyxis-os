#include <kernel/acpi/tables.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>
#include "host.h"

/* The interpreter's installed-table array, exported by name. Only the ACPI
 * worker reads the array; the queue below carries requests to it. Every
 * table's bytes are copied into a sealed FILE when it is opened: the array
 * entry may point into the firmware mapping window or the interpreter's heap,
 * neither of which a user mapping may reference. */

/* Signatures whose bytes never leave the kernel. MSDM carries the OEM product
 * key; it is still listed, with its length, so the gap is visible. */
static const char *const withheld_signatures[] = {"MSDM"};

/* BSP, IF=0: available and the queue. The worker pops with IF=0 and runs the
 * request with IF=1. */
static bool available;
static struct acpi_tables_request *queue_head, *queue_tail;

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

/* A signature occurring once names itself; otherwise its tables are numbered
 * from 1 in array order. Bytes a directory name cannot hold become '_'. */
static bool table_name(size_t index, size_t count, char name[ACPI_TABLE_NAME_MAX + 1])
{
  uacpi_table_info info;
  if (!table_info(index, &info)) {
    return false;
  }
  size_t same = 0, ordinal = 0;
  for (size_t i = 0; i < count; ++i) {
    uacpi_table_info other;
    if (!table_info(i, &other)) {
      return false;
    }
    if (!memcmp(other.signature, info.signature, sizeof(info.signature))) {
      ++same;
      if (i <= index) {
        ++ordinal;
      }
    }
  }
  for (size_t i = 0; i < sizeof(info.signature); ++i) {
    unsigned char byte = info.signature[i];
    name[i] = byte < 0x20 || byte > 0x7e || byte == '/' ? '_' : (char)byte;
  }
  name[sizeof(info.signature)] = 0;
  if (same > 1) {
    /* Twenty decimal digits at most: the name buffer allows for them. */
    sprintf(name + sizeof(info.signature), "%zu", ordinal);
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

/* Copies the table into a sealed FILE. Allocation and the scratch-mapped
 * copy run with IF=0, like other BSP kernel-task heap use; the interpreter's
 * reference keeps the source mapped until it is dropped. */
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
  uacpi_table table;
  uacpi_status mapped = uacpi_table_get_by_index(index, &table);
  if (mapped != UACPI_STATUS_OK) {
    kerror("ACPI: table %s unreadable: %s\n", request->name, uacpi_status_to_string(mapped));
    return CALL_UNAVAILABLE;
  }
  size_t size = table.hdr->length;
  uint64_t flags = cpu_save_interrupts();
  struct file_object *file = file_prepare_snapshot(size);
  if (file) {
    file_fill_snapshot(file, table.ptr);
  }
  cpu_restore_interrupts(flags);
  uacpi_table_unref(&table);
  if (!file) {
    return CALL_NO_MEMORY;
  }
  request->file = file;
  return CALL_OK;
}

static enum call_status execute(struct acpi_tables_request *request)
{
  if (!tables_online()) {
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

void acpi_tables_run(void)
{
  acpi_require_worker();
  for (;;) {
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

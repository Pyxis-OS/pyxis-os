#include <kernel/object/wifi.h>
#include <abi/file.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/object/environment.h>
#include <kernel/object/command_search.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

struct environment_cell {
  struct kernel_object object;
  atomic_size_t references; /* Logical cell owners; storage retires separately. */
  char name[ENVIRONMENT_NAME_MAX + 1];
  uint64_t kind;
  uint64_t flags;
  uint64_t identity;
  size_t length;
  struct capability_grant grant;
  char text[];
};

struct environment_table {
  /* IF=0. Only pointer/counter copies and storage retains; no nesting,
   * callbacks, user copies, allocation, waits or grant operations. */
  struct spinlock lock;
  struct environment_cell *cells[ENVIRONMENT_ENTRIES_LIMIT];
  uint64_t generation;
  uint64_t next_identity;
  size_t count;
  size_t bytes;
};

struct environment_snapshot {
  struct kernel_object object;
  /* IF=0. Logical-owner election and bounded cell retains only. No nesting,
   * callbacks, allocation, user copies or waits. */
  struct spinlock lock;
  size_t owners;
  struct environment_info_reply info;
  struct environment_cell *cells[];
};

struct environment_staging {
  size_t count;
  uint64_t set_flags[ENVIRONMENT_MUTATIONS_LIMIT];
  struct environment_cell *cells[ENVIRONMENT_MUTATIONS_LIMIT];
};

static enum call_status grant_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  case CAP_INVALID: return CALL_BAD_REQUEST;
  default: panic("unexpected environment capability result %u", (unsigned)result);
  }
}

static bool name_valid(const char *name)
{
  size_t i = 0;
  for (; i <= ENVIRONMENT_NAME_MAX && name[i]; ++i) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-' || c == '.')) {
      return false;
    }
  }
  return i && i <= ENVIRONMENT_NAME_MAX;
}

static bool padded_name_valid(const char name[ENVIRONMENT_NAME_MAX + 1])
{
  if (!name_valid(name)) {
    return false;
  }
  for (size_t i = strlen(name) + 1; i <= ENVIRONMENT_NAME_MAX; ++i) {
    if (name[i]) {
      return false;
    }
  }
  return true;
}

static size_t cell_bytes(const struct environment_cell *cell)
{
  return strlen(cell->name) + 1 + (cell->kind == ENVIRONMENT_TEXT ? cell->length + 1 : 0);
}

static size_t find_index(const struct environment_table *table, const char *name)
{
  for (size_t i = 0; i < ENVIRONMENT_ENTRIES_LIMIT; ++i) {
    if (table->cells[i] && strlen(table->cells[i]->name) == strlen(name) &&
        memcmp(table->cells[i]->name, name, strlen(name)) == 0) {
      return i;
    }
  }
  return ENVIRONMENT_ENTRIES_LIMIT;
}

static bool cell_retain(struct environment_cell *cell)
{
  size_t count = atomic_load_explicit(&cell->references, memory_order_relaxed);
  for (;;) {
    KASSERT(count);
    if (count == SIZE_MAX) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&cell->references, &count, count + 1,
        memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
}

/* Outside every table guard. Last capture ends logical authority synchronously;
 * only storage reclamation waits for the BSP object reaper. */
static void cell_release(struct environment_cell *cell)
{
  size_t count = atomic_fetch_sub_explicit(&cell->references, 1, memory_order_acq_rel);
  KASSERT(count);
  if (count == 1) {
    capability_grant_release(&cell->grant);
    object_release(&cell->object);
  }
}

static void destroy_cell(struct kernel_object *object)
{
  struct environment_cell *cell = (struct environment_cell *)object;
  KASSERT(!atomic_load_explicit(&cell->references, memory_order_relaxed));
  KASSERT(!cell->grant.object);
  kfree(cell);
}

static struct environment_cell *cell_create(const char *name, uint64_t kind,
    uint64_t flags, size_t length)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!name_valid(name) || (flags & ~ENVIRONMENT_EXPORTED) ||
      (kind != ENVIRONMENT_CAP && kind != ENVIRONMENT_TEXT) ||
      length > ENVIRONMENT_BYTES_LIMIT || (kind == ENVIRONMENT_CAP && length)) {
    return NULL;
  }
  size_t text_bytes = kind == ENVIRONMENT_TEXT ? length + 1 : 0;
  struct environment_cell *cell = kmalloc(sizeof(*cell) + text_bytes);
  if (!cell) {
    return NULL;
  }
  memset(cell, 0, sizeof(*cell) + text_bytes);
  object_init(&cell->object, OBJECT_ENVIRONMENT_CELL, destroy_cell);
  atomic_init(&cell->references, 1);
  memcpy(cell->name, name, strlen(name) + 1);
  cell->kind = kind;
  cell->flags = flags;
  cell->length = length;
  return cell;
}

struct environment_table *environment_table_create(void)
{
  KASSERT(!cpu_interrupts_enabled());
  struct environment_table *table = kmalloc(sizeof(*table));
  if (table) {
    memset(table, 0, sizeof(*table));
    atomic_init(&table->lock.locked, false);
    table->generation = 1;
    table->next_identity = 1;
  }
  return table;
}

void environment_table_destroy(struct environment_table *table)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!table) {
    return;
  }
  for (size_t i = 0; i < ENVIRONMENT_ENTRIES_LIMIT; ++i) {
    if (table->cells[i]) {
      cell_release(table->cells[i]);
    }
    if ((i + 1) % 256 == 0 && task_current_actor()) {
      task_yield_masked();
    }
  }
  kfree(table);
}

static void destroy_environment(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *environment_create(void)
{
  KASSERT(!cpu_interrupts_enabled());
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_ENVIRONMENT, destroy_environment);
  }
  return object;
}

static void staging_destroy(struct environment_staging *staging)
{
  KASSERT(arch_cpu_index() == 0);
  if (!staging) {
    return;
  }
  for (size_t i = 0; i < staging->count; ++i) {
    if (staging->cells[i]) {
      cell_release(staging->cells[i]);
    }
  }
  kfree(staging);
}

/* The actor retains the table. Every candidate cell already owns its logical grant.
 * Preflight the entire batch under the guard, then publish without failure. */
static enum call_status commit_set(struct environment_table *table,
    struct environment_staging *staging)
{
  KASSERT(!cpu_interrupts_enabled());
  size_t indices[ENVIRONMENT_MUTATIONS_LIMIT];
  struct environment_cell *old[ENVIRONMENT_MUTATIONS_LIMIT] = {0};
  size_t count = table->count, bytes = table->bytes, changes = 0;
  enum call_status status = CALL_OK;
  spin_lock(&table->lock);
  for (size_t i = 0; i < staging->count; ++i) {
    struct environment_cell *cell = staging->cells[i];
    indices[i] = find_index(table, cell->name);
    bool exists = indices[i] != ENVIRONMENT_ENTRIES_LIMIT;
    if (exists && (staging->set_flags[i] & ENVIRONMENT_SET_REQUIRE_ABSENT)) {
      status = CALL_ALREADY_EXISTS;
      break;
    }
    if (exists && (staging->set_flags[i] & ENVIRONMENT_SET_NO_REPLACE)) {
      continue;
    }
    if (exists) {
      bytes -= cell_bytes(table->cells[indices[i]]);
    } else {
      ++count;
    }
    bytes += cell_bytes(cell);
    ++changes;
  }
  if (status == CALL_OK && (count > ENVIRONMENT_ENTRIES_LIMIT ||
      bytes > ENVIRONMENT_BYTES_LIMIT || (changes &&
      (table->generation == UINT64_MAX || changes > UINT64_MAX - table->next_identity)))) {
    status = CALL_LIMIT;
  }
  if (status == CALL_OK && changes) {
    for (size_t i = 0; i < staging->count; ++i) {
      size_t index = indices[i];
      if (index != ENVIRONMENT_ENTRIES_LIMIT &&
          (staging->set_flags[i] & ENVIRONMENT_SET_NO_REPLACE)) {
        continue;
      }
      if (index == ENVIRONMENT_ENTRIES_LIMIT) {
        for (index = 0; table->cells[index]; ++index) {
          KASSERT(index + 1 < ENVIRONMENT_ENTRIES_LIMIT);
        }
      }
      old[i] = table->cells[index];
      staging->cells[i]->identity = table->next_identity++;
      table->cells[index] = staging->cells[i];
      staging->cells[i] = NULL;
    }
    table->count = count;
    table->bytes = bytes;
    ++table->generation;
  }
  spin_unlock(&table->lock);
  for (size_t i = 0; i < staging->count; ++i) {
    if (old[i]) {
      cell_release(old[i]);
    }
  }
  return status;
}

static enum call_status seed_cell_owned(struct environment_table *table,
    struct environment_cell *cell)
{
  struct environment_staging staging = {.count = 1, .cells = {cell}};
  enum call_status status = commit_set(table, &staging);
  if (staging.cells[0]) {
    cell_release(staging.cells[0]);
  }
  return status;
}

enum call_status environment_table_seed_owned(struct environment_table *table,
    const char *name, uint64_t flags, struct capability_grant *grant)
{
  if (!name_valid(name) || (flags & ~ENVIRONMENT_EXPORTED) || !grant->object) {
    return CALL_BAD_REQUEST;
  }
  struct environment_cell *cell = cell_create(name, ENVIRONMENT_CAP, flags, 0);
  if (!cell) {
    return CALL_NO_MEMORY;
  }
  cell->grant = *grant;
  struct environment_staging staging = {.count = 1, .cells = {cell}};
  enum call_status status = commit_set(table, &staging);
  if (status == CALL_OK) {
    *grant = (struct capability_grant){0};
  } else {
    cell->grant = (struct capability_grant){0};
    cell_release(cell);
  }
  return status;
}

enum call_status environment_table_seed_text(struct environment_table *table,
    const char *name, uint64_t flags, const char *text, size_t length)
{
  if (!name_valid(name) || (flags & ~ENVIRONMENT_EXPORTED) ||
      (length && !text)) {
    return CALL_BAD_REQUEST;
  }
  if (length > ENVIRONMENT_BYTES_LIMIT) {
    return CALL_LIMIT;
  }
  struct environment_cell *cell = cell_create(name, ENVIRONMENT_TEXT, flags, length);
  if (!cell) {
    return CALL_NO_MEMORY;
  }
  for (size_t i = 0; i < length; ++i) {
    if (!text[i]) {
      cell_release(cell);
      return CALL_BAD_REQUEST;
    }
  }
  if (length) {
    memcpy(cell->text, text, length);
  }
  return seed_cell_owned(table, cell);
}

bool environment_snapshot_grant_retain(struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_ENVIRONMENT_SNAPSHOT);
  struct environment_snapshot *snapshot = (struct environment_snapshot *)object;
  spin_lock(&snapshot->lock);
  bool retained = snapshot->owners && snapshot->owners != SIZE_MAX;
  if (retained) {
    ++snapshot->owners;
  }
  spin_unlock(&snapshot->lock);
  return retained;
}

void environment_snapshot_grant_release(struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_ENVIRONMENT_SNAPSHOT);
  struct environment_snapshot *snapshot = (struct environment_snapshot *)object;
  spin_lock(&snapshot->lock);
  KASSERT(snapshot->owners);
  bool last = --snapshot->owners == 0;
  spin_unlock(&snapshot->lock);
  if (last) {
    /* Closed captures cannot admit a new cell reference. The immutable array
     * remains storage for admitted calls, but no longer owns its cells. */
    for (size_t i = 0; i < snapshot->info.count; ++i) {
      cell_release(snapshot->cells[i]);
    }
  }
}

void environment_snapshot_release(struct kernel_object *object)
{
  environment_snapshot_grant_release(object);
  object_release(object);
}

static void destroy_snapshot(struct kernel_object *object)
{
  struct environment_snapshot *snapshot = (struct environment_snapshot *)object;
  KASSERT(!snapshot->owners);
  kfree(snapshot);
}

enum call_status environment_snapshot_capture(struct environment_table *table,
    uint64_t flags, struct kernel_object **result)
{
  KASSERT(arch_cpu_index() == 0);
  *result = NULL;
  if (flags & ~(ENVIRONMENT_SNAPSHOT_INCLUDE_PRIVATE | ENVIRONMENT_SNAPSHOT_TEXT_ONLY)) {
    return CALL_BAD_REQUEST;
  }
  struct environment_snapshot *snapshot = kmalloc(sizeof(*snapshot) +
      ENVIRONMENT_ENTRIES_LIMIT * sizeof(snapshot->cells[0]));
  if (!snapshot) {
    return CALL_NO_MEMORY;
  }
  object_init(&snapshot->object, OBJECT_ENVIRONMENT_SNAPSHOT, destroy_snapshot);
  atomic_init(&snapshot->lock.locked, false);
  snapshot->owners = 1;
  snapshot->info = (struct environment_info_reply){0};
  enum call_status status = CALL_OK;
  spin_lock(&table->lock);
  snapshot->info.generation = table->generation;
  for (size_t i = 0; i < ENVIRONMENT_ENTRIES_LIMIT; ++i) {
    struct environment_cell *cell = table->cells[i];
    if (!cell || (!(flags & ENVIRONMENT_SNAPSHOT_INCLUDE_PRIVATE) &&
        !(cell->flags & ENVIRONMENT_EXPORTED)) ||
        ((flags & ENVIRONMENT_SNAPSHOT_TEXT_ONLY) && cell->kind != ENVIRONMENT_TEXT) ||
        (cell->kind == ENVIRONMENT_CAP &&
         !wifi_grant_delegatable(cell->grant.object, cell->grant.rights))) {
      continue;
    }
    if (!cell_retain(cell)) {
      status = CALL_LIMIT;
      break;
    }
    snapshot->cells[snapshot->info.count++] = cell;
    snapshot->info.bytes += cell_bytes(cell);
  }
  spin_unlock(&table->lock);
  if (status != CALL_OK) {
    environment_snapshot_release(&snapshot->object);
    return status;
  }
  *result = &snapshot->object;
  return CALL_OK;
}

void environment_snapshot_info(struct kernel_object *object,
    struct environment_info_reply *info)
{
  KASSERT(object->type == OBJECT_ENVIRONMENT_SNAPSHOT);
  *info = ((struct environment_snapshot *)object)->info;
}

const struct environment_cell *environment_snapshot_cell(struct kernel_object *object,
    size_t index)
{
  KASSERT(object->type == OBJECT_ENVIRONMENT_SNAPSHOT);
  struct environment_snapshot *snapshot = (struct environment_snapshot *)object;
  spin_lock(&snapshot->lock);
  const struct environment_cell *cell = snapshot->owners &&
      index < snapshot->info.count ? snapshot->cells[index] : NULL;
  spin_unlock(&snapshot->lock);
  return cell;
}

const char *environment_cell_name(const struct environment_cell *cell)
{
  return cell->name;
}

uint64_t environment_cell_kind(const struct environment_cell *cell)
{
  return cell->kind;
}

uint64_t environment_cell_flags(const struct environment_cell *cell)
{
  return cell->flags;
}

uint64_t environment_cell_identity(const struct environment_cell *cell)
{
  return cell->identity;
}

const char *environment_cell_text(const struct environment_cell *cell, size_t *length)
{
  *length = cell->length;
  return cell->kind == ENVIRONMENT_TEXT ? cell->text : NULL;
}

enum capability_result environment_cell_grant(const struct environment_cell *cell,
    struct capability_grant *grant)
{
  *grant = (struct capability_grant){0};
  if (cell->kind != ENVIRONMENT_CAP) {
    return CAP_INVALID;
  }
  return capability_grant_derive(cell->grant.object, cell->grant.rights,
      cell->grant.transport, cell->grant.restrictions, grant);
}

enum call_status environment_table_seed_cell(struct environment_table *table,
    const struct environment_cell *cell)
{
  if (cell->kind == ENVIRONMENT_TEXT) {
    return environment_table_seed_text(table, cell->name, cell->flags,
        cell->text, cell->length);
  }
  struct capability_grant grant;
  enum capability_result result = environment_cell_grant(cell, &grant);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  enum call_status status = environment_table_seed_owned(table, cell->name,
      cell->flags, &grant);
  capability_grant_release(&grant);
  return status;
}

static enum call_status commit_unset(struct environment_request *request)
{
  struct environment_table *table = request->table;
  struct environment_cell *old[ENVIRONMENT_MUTATIONS_LIMIT] = {0};
  size_t indices[ENVIRONMENT_MUTATIONS_LIMIT];
  size_t changes = 0;
  spin_lock(&table->lock);
  for (size_t i = 0; i < request->count; ++i) {
    indices[i] = find_index(table, request->removals[i].name);
    if (indices[i] == ENVIRONMENT_ENTRIES_LIMIT) {
      continue;
    }
    struct environment_cell *cell = table->cells[indices[i]];
    if (request->removals[i].identity && request->removals[i].identity != cell->identity) {
      indices[i] = ENVIRONMENT_ENTRIES_LIMIT;
    } else {
      ++changes;
    }
  }
  if (changes && table->generation == UINT64_MAX) {
    spin_unlock(&table->lock);
    return CALL_LIMIT;
  }
  for (size_t i = 0; i < request->count; ++i) {
    if (indices[i] != ENVIRONMENT_ENTRIES_LIMIT) {
      old[i] = table->cells[indices[i]];
      table->cells[indices[i]] = NULL;
      --table->count;
      table->bytes -= cell_bytes(old[i]);
    }
  }
  if (changes) {
    ++table->generation;
  }
  spin_unlock(&table->lock);
  for (size_t i = 0; i < request->count; ++i) {
    if (old[i]) {
      cell_release(old[i]);
    }
  }
  return CALL_OK;
}

static enum call_status staging_allocate(struct environment_request *request)
{
  struct environment_staging *staging = kmalloc(sizeof(*staging));
  if (!staging) {
    return CALL_NO_MEMORY;
  }
  memset(staging, 0, sizeof(*staging));
  staging->count = request->count;
  for (size_t i = 0; i < request->count; ++i) {
    struct environment_set_entry *entry = &request->entries[i];
    staging->set_flags[i] = entry->set_flags;
    staging->cells[i] = cell_create(entry->name, entry->kind, entry->flags,
        entry->text_length);
    if (!staging->cells[i]) {
      staging_destroy(staging);
      return CALL_NO_MEMORY;
    }
  }
  request->staging = staging;
  return CALL_OK;
}

void environment_execute(struct environment_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  request->result = CALL_OK;
  switch (request->action) {
  case ENVIRONMENT_ALLOCATE:
    request->result = staging_allocate(request);
    break;
  case ENVIRONMENT_COMMIT_SET:
    for (size_t i = 0; i < request->staging->count; ++i) {
      struct environment_cell *cell = request->staging->cells[i];
      if (cell->kind == ENVIRONMENT_CAP) {
        enum capability_result result = forwarder_derive_grant(&cell->grant,
            request->request.operation);
        if (result != CAP_OK) {
          request->result = grant_status(result);
          break;
        }
      }
    }
    if (request->result == CALL_OK) {
      if (!forwarder_operation_start(request->request.operation)) {
        request->result = CALL_DENIED;
      } else {
        request->result = commit_set(request->table, request->staging);
      }
    }
    staging_destroy(request->staging);
    request->staging = NULL;
    break;
  case ENVIRONMENT_COMMIT_UNSET:
    request->result = forwarder_operation_start(request->request.operation) ?
        commit_unset(request) : CALL_DENIED;
    break;
  case ENVIRONMENT_CAPTURE:
    request->result = environment_snapshot_capture(request->table, request->flags,
        &request->snapshot);
    break;
  case ENVIRONMENT_RELEASE_STAGING:
    staging_destroy(request->staging);
    request->staging = NULL;
    break;
  case ENVIRONMENT_CREATE_TEXT_FILE:
    KASSERT(request->cell && request->cell->kind == ENVIRONMENT_TEXT);
    request->file = file_prepare_snapshot(request->cell->length);
    if (!request->file) {
      request->result = CALL_NO_MEMORY;
    } else {
      file_fill_snapshot(request->file, request->cell->text);
    }
    break;
  case ENVIRONMENT_ALLOCATE_COMMAND_LIST:
    request->command_list = command_list_create(request->command_entries,
        request->command_grants, request->count);
    request->result = request->command_list ? CALL_OK : CALL_NO_MEMORY;
    break;
  default:
    panic("unknown environment action");
  }
}

static void release_staging(struct environment_staging *staging)
{
  struct environment_request *request = (struct environment_request *)
      bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
  request->action = ENVIRONMENT_RELEASE_STAGING;
  request->staging = staging;
  bsp_request_submit_and_wait(&request->request);
  bsp_request_release(&request->request);
}

static bool names_equal(const char *left, const char *right)
{
  size_t length = strlen(left);
  return length == strlen(right) && memcmp(left, right, length) == 0;
}

static bool text_valid(const char *text, size_t length)
{
  for (size_t i = 0; i < length; ++i) {
    if (!text[i]) {
      return false;
    }
  }
  return true;
}

static enum call_status set_entries(uintptr_t address, size_t count)
{
  struct environment_set_entry entries[ENVIRONMENT_MUTATIONS_LIMIT];
  if (!count) {
    return CALL_BAD_REQUEST;
  }
  if (count > ENVIRONMENT_MUTATIONS_LIMIT) {
    return CALL_LIMIT;
  }
  if (!copy_from_user(entries, address, count * sizeof(entries[0]))) {
    return CALL_BAD_BUFFER;
  }
  size_t bytes = 0;
  for (size_t i = 0; i < count; ++i) {
    struct environment_set_entry *entry = &entries[i];
    if (!padded_name_valid(entry->name) || (entry->flags & ~ENVIRONMENT_EXPORTED) ||
        (entry->set_flags != 0 && entry->set_flags != ENVIRONMENT_SET_NO_REPLACE &&
        entry->set_flags != ENVIRONMENT_SET_REQUIRE_ABSENT) ||
        (entry->kind != ENVIRONMENT_CAP && entry->kind != ENVIRONMENT_TEXT) ||
        (entry->kind == ENVIRONMENT_CAP && (entry->text_address || entry->text_length)) ||
        (entry->kind == ENVIRONMENT_TEXT && (entry->handle || entry->rights || entry->transport))) {
      return CALL_BAD_REQUEST;
    }
    if (entry->text_length > ENVIRONMENT_BYTES_LIMIT) {
      return CALL_LIMIT;
    }
    for (size_t j = 0; j < i; ++j) {
      if (names_equal(entry->name, entries[j].name)) {
        return CALL_BAD_REQUEST;
      }
    }
    bytes += strlen(entry->name) + 1;
    if (entry->kind == ENVIRONMENT_TEXT) {
      bytes += entry->text_length + 1;
    }
    if (bytes > ENVIRONMENT_BYTES_LIMIT) {
      return CALL_LIMIT;
    }
  }
  struct environment_request *request = (struct environment_request *)
      bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
  request->action = ENVIRONMENT_ALLOCATE;
  request->count = count;
  memcpy(request->entries, entries, count * sizeof(entries[0]));
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  struct environment_staging *staging = request->staging;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return status;
  }
  for (size_t i = 0; i < count; ++i) {
    struct environment_cell *cell = staging->cells[i];
    if (cell->kind == ENVIRONMENT_TEXT) {
      if (!copy_from_user(cell->text, entries[i].text_address, cell->length)) {
        status = CALL_BAD_BUFFER;
        break;
      }
      if (!text_valid(cell->text, cell->length)) {
        status = CALL_BAD_REQUEST;
        break;
      }
    } else {
      enum capability_result result = capability_grant_acquire(
          &process_current()->capabilities, entries[i].handle,
          entries[i].rights, entries[i].transport, &cell->grant);
      if (result != CAP_OK) {
        status = grant_status(result);
        break;
      }
    }
  }
  if (status != CALL_OK || task_stop_requested()) {
    release_staging(staging);
    return status == CALL_OK ? CALL_ENDPOINT_CLOSED : status;
  }
  request = (struct environment_request *)bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
  request->action = ENVIRONMENT_COMMIT_SET;
  request->table = process_current()->environment;
  request->staging = staging;
  bsp_request_submit_and_wait(&request->request);
  status = request->result;
  bsp_request_release(&request->request);
  return status;
}

static enum call_status unset_entries(uintptr_t address, size_t count)
{
  struct environment_unset_entry entries[ENVIRONMENT_MUTATIONS_LIMIT];
  if (!count) {
    return CALL_BAD_REQUEST;
  }
  if (count > ENVIRONMENT_MUTATIONS_LIMIT) {
    return CALL_LIMIT;
  }
  if (!copy_from_user(entries, address, count * sizeof(entries[0]))) {
    return CALL_BAD_BUFFER;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!padded_name_valid(entries[i].name)) {
      return CALL_BAD_REQUEST;
    }
    for (size_t j = 0; j < i; ++j) {
      if (names_equal(entries[i].name, entries[j].name)) {
        return CALL_BAD_REQUEST;
      }
    }
  }
  if (task_stop_requested()) {
    return CALL_ENDPOINT_CLOSED;
  }
  struct environment_request *request = (struct environment_request *)
      bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
  request->action = ENVIRONMENT_COMMIT_UNSET;
  request->table = process_current()->environment;
  request->count = count;
  memcpy(request->removals, entries, count * sizeof(entries[0]));
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  bsp_request_release(&request->request);
  return status;
}

static enum call_status capture_cell(struct kernel_object *object, const char *name,
    size_t index, bool enumerate, struct environment_cell **result, uint64_t *generation)
{
  *result = NULL;
  if (object->type == OBJECT_ENVIRONMENT_SNAPSHOT) {
    struct environment_snapshot *snapshot = (struct environment_snapshot *)object;
    *generation = snapshot->info.generation;
    enum call_status status = CALL_NOT_FOUND;
    spin_lock(&snapshot->lock);
    if (!snapshot->owners) {
      status = CALL_ENDPOINT_CLOSED;
    } else {
      for (size_t i = 0; i < snapshot->info.count; ++i) {
        struct environment_cell *cell = snapshot->cells[i];
        if ((enumerate && i == index) || (!enumerate && names_equal(cell->name, name))) {
          if (cell_retain(cell)) {
            *result = cell;
            status = CALL_OK;
          } else {
            status = CALL_LIMIT;
          }
          break;
        }
      }
    }
    spin_unlock(&snapshot->lock);
    return status;
  }
  struct environment_table *table = process_current()->environment;
  spin_lock(&table->lock);
  *generation = table->generation;
  size_t found = find_index(table, name);
  enum call_status status = CALL_NOT_FOUND;
  if (found != ENVIRONMENT_ENTRIES_LIMIT) {
    if (cell_retain(table->cells[found])) {
      *result = table->cells[found];
      status = CALL_OK;
    } else {
      status = CALL_LIMIT;
    }
  }
  spin_unlock(&table->lock);
  return status;
}

static struct syscall_result return_cell(struct environment_cell *cell,
    uint64_t generation, uintptr_t text_address, size_t text_capacity,
    uintptr_t reply_address, bool lookup_file)
{
  if (cell->kind == ENVIRONMENT_CAP &&
      !wifi_grant_delegatable(cell->grant.object, cell->grant.rights) &&
      !wifi_policy_owned(cell->grant.object, process_current())) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct environment_entry_reply reply = {
    .kind = cell->kind, .flags = cell->flags, .identity = cell->identity,
    .generation = generation, .text_length = cell->length,
    .rights = cell->grant.rights, .transport = cell->grant.transport,
  };
  memcpy(reply.name, cell->name, sizeof(reply.name));
  if (cell->kind == ENVIRONMENT_TEXT && !lookup_file) {
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (text_capacity < cell->length + 1) {
      return copy_to_user(reply_address, &reply, sizeof(reply)) ?
          (struct syscall_result){CALL_BUFFER_TOO_SMALL, sizeof(reply)} :
          (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return copy_reply_to_user(reply_address, &reply, sizeof(reply),
        text_address, cell->text, cell->length + 1) ?
        (struct syscall_result){CALL_OK, sizeof(reply)} :
        (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result result = capability_request_reservation(1, &reservation, &slot);
  if (result != CAP_OK) {
    return (struct syscall_result){grant_status(result), 0};
  }
  struct capability_grant grant = {0};
  if (cell->kind == ENVIRONMENT_TEXT) {
    struct environment_request *request = (struct environment_request *)
        bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
    request->action = ENVIRONMENT_CREATE_TEXT_FILE;
    request->cell = cell;
    bsp_request_submit_and_wait(&request->request);
    enum call_status status = request->result;
    struct file_object *file = request->file;
    bsp_request_release(&request->request);
    if (status != CALL_OK) {
      capability_reservation_release(&reservation, &slot);
      return (struct syscall_result){status, 0};
    }
    reply.rights = FILE_RIGHT_READ;
    reply.transport = 0;
    result = capability_grant_retain(&file->object, reply.rights, reply.transport, &grant);
    object_release(&file->object);
  } else {
    result = environment_cell_grant(cell, &grant);
  }
  if (result == CAP_OK) {
    result = forwarder_request_derivation(&grant);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(reservation.table, &grant, 1);
  }
  if (result == CAP_OK) {
    capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
  }
  capability_grant_release(&grant);
  capability_reservation_release(&reservation, &slot);
  if (result != CAP_OK) {
    return (struct syscall_result){grant_status(result), 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result snapshot_call(uint64_t flags, uintptr_t reply_address)
{
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  enum capability_result result = capability_request_reservation(1, &reservation, &slot);
  if (result != CAP_OK) {
    return (struct syscall_result){grant_status(result), 0};
  }
  struct environment_request *request = (struct environment_request *)
      bsp_request_prepare(BSP_SERVICE_ENVIRONMENT);
  request->action = ENVIRONMENT_CAPTURE;
  request->table = process_current()->environment;
  request->flags = flags;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  struct kernel_object *object = request->snapshot;
  bsp_request_release(&request->request);
  struct environment_snapshot_reply reply = {0};
  struct capability_grant grant = {0};
  if (status == CALL_OK) {
    environment_snapshot_info(object, &reply.info);
    result = capability_grant_retain(object, ENVIRONMENT_RIGHT_READ, 0, &grant);
    if (result == CAP_OK) {
      result = forwarder_request_derivation(&grant);
    }
    if (result == CAP_OK && !task_operation_start()) {
      result = CAP_DENIED;
    }
    if (result == CAP_OK) {
      result = capability_validate_grants(reservation.table, &grant, 1);
    }
    if (result == CAP_OK) {
      capability_install_reserved(&reservation, &slot, &grant, 1, &reply.handle);
    }
    status = grant_status(result);
  }
  capability_grant_release(&grant);
  if (object) {
    environment_snapshot_release(object);
  }
  capability_reservation_release(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  return copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.handle, 1) ?
      (struct syscall_result){CALL_OK, sizeof(reply)} :
      (struct syscall_result){CALL_BAD_BUFFER, 0};
}

struct syscall_result environment_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  bool snapshot = object->type == OBJECT_ENVIRONMENT_SNAPSHOT;
  bool create_list = !snapshot && operation == ENVIRONMENT_CREATE_COMMAND_LIST;
  bool write = !snapshot && (operation == ENVIRONMENT_SET || operation == ENVIRONMENT_UNSET ||
      create_list);
  if (!(rights & (write ? ENVIRONMENT_RIGHT_WRITE : ENVIRONMENT_RIGHT_READ))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == ENVIRONMENT_INFO) {
    struct environment_info_reply reply;
    if (request_size || reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (snapshot) {
      environment_snapshot_info(object, &reply);
    } else {
      struct environment_table *table = process_current()->environment;
      spin_lock(&table->lock);
      reply = (struct environment_info_reply){table->generation, table->count, table->bytes};
      spin_unlock(&table->lock);
    }
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return copy_to_user(reply_address, &reply, sizeof(reply)) ?
        (struct syscall_result){CALL_OK, sizeof(reply)} :
        (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (create_list) {
    struct { uint64_t address; uint64_t count; } message;
    if (request_size != sizeof(message) || reply_capacity < sizeof(struct command_list_create_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&message, request_address, sizeof(message)) ||
        !user_buffer_check(reply_address, sizeof(struct command_list_create_reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return command_list_create_call(message.address, message.count, reply_address);
  }
  if (write) {
    struct { uint64_t address; uint64_t count; } message;
    if (request_size != sizeof(message)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&message, request_address, sizeof(message))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    enum call_status status = operation == ENVIRONMENT_SET ?
        set_entries(message.address, message.count) : unset_entries(message.address, message.count);
    return (struct syscall_result){status, 0};
  }
  if (!snapshot && operation == ENVIRONMENT_SNAPSHOT) {
    uint64_t flags;
    if (request_size != sizeof(flags) || reply_capacity < sizeof(struct environment_snapshot_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&flags, request_address, sizeof(flags))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (flags & ~(ENVIRONMENT_SNAPSHOT_INCLUDE_PRIVATE | ENVIRONMENT_SNAPSHOT_TEXT_ONLY)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(struct environment_snapshot_reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return snapshot_call(flags, reply_address);
  }
  bool enumerate = snapshot && operation == ENVIRONMENT_ENUMERATE;
  bool lookup_file = operation == ENVIRONMENT_LOOKUP_FILE;
  if (operation != ENVIRONMENT_LOOKUP && !lookup_file && !enumerate) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  char name[ENVIRONMENT_NAME_MAX + 1] = {0};
  uint64_t index = 0, text_address, text_capacity;
  if (enumerate) {
    struct { uint64_t index; uint64_t address; uint64_t capacity; } message;
    if (request_size != sizeof(message)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&message, request_address, sizeof(message))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    index = message.index;
    text_address = message.address;
    text_capacity = message.capacity;
  } else {
    struct { char name[ENVIRONMENT_NAME_MAX + 1]; uint64_t address; uint64_t capacity; } message;
    if (request_size != sizeof(message)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&message, request_address, sizeof(message))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!padded_name_valid(message.name)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    memcpy(name, message.name, sizeof(name));
    text_address = message.address;
    text_capacity = message.capacity;
  }
  if (lookup_file && (text_address || text_capacity)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_capacity < sizeof(struct environment_entry_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct environment_entry_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct environment_cell *cell;
  uint64_t generation;
  enum call_status status = capture_cell(object, name, index, enumerate, &cell, &generation);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct syscall_result result = return_cell(cell, generation, text_address, text_capacity,
      reply_address, lookup_file);
  cell_release(cell);
  return result;
}

#include <abi/console.h>
#include <abi/readiness_set.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/net/interface.h>
#include <kernel/net/tcp.h>
#include <kernel/object/console.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/process.h>
#include <kernel/object/clock.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/pipe.h>
#include <kernel/object/terminal.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user/readiness.h>
#include <kernel/user/readiness_set.h>
#include <kernel/user_memory.h>

#define READINESS_BUDGET (16 * 1024 * 1024)
#define READINESS_WORK_BUDGET 64
#define READINESS_INITIAL_BUCKETS 16

struct readiness_account {
  struct process *owner;
  struct readiness_set *sets, *close_cursor;
  struct readiness_account *close_next;
  struct execution_group *cleanup;
  size_t bytes, references;
  bool closing, close_queued;
};

struct readiness_registration {
  struct readiness_set *set;
  struct readiness_registration *hash_next;
  struct readiness_registration *next, **previous;
  struct readiness_registration *source_next, **source_previous;
  struct readiness_registration *ready_next, **ready_previous;
  struct readiness_registration *delivery_next;
  struct readiness_registration *invalid_next, **invalid_previous;
  struct readiness_source *source;
  struct kernel_object *target;
  struct forwarder_operation lineage;
  uint64_t identity, generation, tag, allowed, active, observed, ready;
  uint64_t pending_active, pending_observed;
  enum call_status status;
  bool network, provisional, loaned, denied, terminal_sent, delivering, delivered_terminal;
};

struct readiness_set {
  struct kernel_object object;
  struct readiness_account *account;
  struct readiness_set *owner_next, **owner_previous;
  struct readiness_set *work_next, **work_previous;
  struct readiness_set *timer_next, **timer_previous;
  struct bsp_request *commands, **commands_tail;
  struct bsp_request *deferred, **deferred_tail;
  struct readiness_set_request *collector;
  struct readiness_registration **buckets, **old_buckets;
  size_t bucket_count, old_count, grow_cursor, count, controls;
  struct readiness_registration *first;
  struct readiness_registration *ready_first, **ready_tail;
  struct readiness_registration *delivery, *invalid;
  uint64_t next_identity;
  bool delivered_wake;
  size_t scan_remaining;
  bool network, closing, wake, collecting, attached, collector_turn;
};

/* IF=0. Forwarder -> this guard -> scheduler; never resolve/release lineage,
 * allocate, query an actor or wait under this guard. Only fixed, lease-validated
 * publication replies may be copied while held. */
static struct spinlock sets_lock;
static struct readiness_set *work_heads[2];
static struct readiness_set **work_tails[2] = {&work_heads[0], &work_heads[1]};
static struct readiness_source *source_heads[2];
static struct readiness_source **source_tails[2] = {&source_heads[0], &source_heads[1]};
static struct readiness_set *timers[2];
static struct readiness_account *closing_accounts;

static uint64_t lock_sets(void)
{
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&sets_lock);
  return flags;
}

static void unlock_sets(uint64_t flags)
{
  spin_unlock(&sets_lock);
  cpu_restore_interrupts(flags);
}

static void notify_workers(void)
{
  uint64_t flags = cpu_save_interrupts();
  readiness_notify();
  cpu_restore_interrupts(flags);
}

static void schedule_set(struct readiness_set *set)
{
  if (set->work_previous || !set->attached) {
    return;
  }
  size_t domain = set->network;
  set->work_previous = work_tails[domain];
  *work_tails[domain] = set;
  work_tails[domain] = &set->work_next;
}

static void unqueue_set(struct readiness_set *set)
{
  if (!set->work_previous) {
    return;
  }
  *set->work_previous = set->work_next;
  if (set->work_next) {
    set->work_next->work_previous = set->work_previous;
  } else {
    work_tails[set->network] = set->work_previous;
  }
  set->work_next = NULL;
  set->work_previous = NULL;
}

/* sets_lock held. FINISH or closure makes blocked controls runnable again. */
static void resume_deferred(struct readiness_set *set)
{
  if (set->deferred) {
    *set->commands_tail = set->deferred;
    set->commands_tail = set->deferred_tail;
    set->deferred = NULL;
    set->deferred_tail = &set->deferred;
  }
}

static void unqueue_source(struct readiness_source *source, size_t domain)
{
  if (!source->previous[domain]) {
    return;
  }
  *source->previous[domain] = source->next[domain];
  if (source->next[domain]) {
    source->next[domain]->previous[domain] = source->previous[domain];
  } else {
    source_tails[domain] = source->previous[domain];
  }
  source->previous[domain] = NULL;
  source->next[domain] = NULL;
  source->cursor[domain] = NULL;
  source->again[domain] = false;
}

static void dirty_source(struct readiness_source *source, size_t domain)
{
  if (!source->domains[domain]) {
    return;
  }
  source->again[domain] = true;
  if (!source->previous[domain]) {
    source->previous[domain] = source_tails[domain];
    *source_tails[domain] = source;
    source_tails[domain] = &source->next[domain];
    source->cursor[domain] = source->first;
  }
}

void readiness_source_notify(struct readiness_source *source)
{
  if (!source || !atomic_load_explicit(&source->watchers, memory_order_acquire)) {
    return;
  }
  uint64_t flags = lock_sets();
  dirty_source(source, 0);
  dirty_source(source, 1);
  unlock_sets(flags);
  notify_workers();
}

static void ready_remove(struct readiness_registration *entry)
{
  if (!entry->ready_previous) {
    return;
  }
  *entry->ready_previous = entry->ready_next;
  if (entry->ready_next) {
    entry->ready_next->ready_previous = entry->ready_previous;
  } else {
    entry->set->ready_tail = entry->ready_previous;
  }
  entry->ready_next = NULL;
  entry->ready_previous = NULL;
}

static void ready_insert(struct readiness_registration *entry)
{
  if (entry->ready_previous || entry->provisional || entry->delivering ||
      (entry->status != CALL_OK && entry->terminal_sent)) {
    return;
  }
  struct readiness_set *set = entry->set;
  entry->ready_previous = set->ready_tail;
  *set->ready_tail = entry;
  set->ready_tail = &entry->ready_next;
  schedule_set(set);
}

void readiness_registration_revoked(struct forwarder_operation *operation)
{
  struct readiness_registration *entry = (struct readiness_registration *)
      ((uint8_t *)operation - offsetof(struct readiness_registration, lineage));
  uint64_t flags = lock_sets();
  entry->denied = true;
  entry->status = CALL_DENIED;
  if (!entry->invalid_previous) {
    entry->invalid_next = entry->set->invalid;
    entry->invalid_previous = &entry->set->invalid;
    if (entry->invalid_next) {
      entry->invalid_next->invalid_previous = &entry->invalid_next;
    }
    entry->set->invalid = entry;
  }
  ready_insert(entry);
  schedule_set(entry->set);
  unlock_sets(flags);
}

static void source_attach(struct readiness_registration *entry,
    struct readiness_source *source)
{
  entry->source = source;
  entry->source_next = source->first;
  entry->source_previous = &source->first;
  if (source->first) {
    source->first->source_previous = &entry->source_next;
  }
  source->first = entry;
  ++source->domains[entry->network];
  atomic_fetch_add_explicit(&source->watchers, 1, memory_order_release);
}

static void source_detach(struct readiness_registration *entry)
{
  struct readiness_source *source = entry->source;
  if (!source) {
    return;
  }
  for (size_t domain = 0; domain < 2; ++domain) {
    if (source->cursor[domain] == entry) {
      source->cursor[domain] = entry->source_next;
    }
  }
  *entry->source_previous = entry->source_next;
  if (entry->source_next) {
    entry->source_next->source_previous = entry->source_previous;
  }
  --source->domains[entry->network];
  atomic_fetch_sub_explicit(&source->watchers, 1, memory_order_release);
  for (size_t domain = 0; domain < 2; ++domain) {
    if (!source->domains[domain]) {
      unqueue_source(source, domain);
    }
  }
  entry->source = NULL;
  entry->source_next = NULL;
  entry->source_previous = NULL;
}

static struct readiness_source *object_source(struct kernel_object *object)
{
  switch (object->type) {
  case OBJECT_PROCESS_INTERRUPT:
    return process_interrupt_readiness_source(object);
  case OBJECT_ENDPOINT_RECEIVER:
    return endpoint_readiness_source(object);
  case OBJECT_PIPE:
    return &((struct pipe_end *)object)->readiness;
  case OBJECT_CONSOLE:
    return &((struct console_object *)object)->readiness;
  case OBJECT_TERMINAL_INPUT:
  case OBJECT_TERMINAL_OUTPUT:
    return terminal_readiness_source(object);
  case OBJECT_TCP:
  case OBJECT_TCP_LISTENER:
    return tcp_readiness_source(object);
  default:
    return NULL;
  }
}

static uint64_t source_events(struct readiness_registration *entry)
{
  struct kernel_object *target = entry->target;
  if (!target) {
    return 0;
  }
  /* Disarming suppresses level conditions, not whole-source failure. */
  uint64_t events = entry->active ? entry->active : entry->allowed;
  uint64_t ready;
  switch (target->type) {
  case OBJECT_PROCESS_INTERRUPT:
    ready = process_interrupt_ready(target);
    break;
  case OBJECT_ENDPOINT_RECEIVER:
    ready = endpoint_receiver_ready((struct endpoint *)target, events);
    break;
  case OBJECT_PIPE:
    ready = pipe_ready((struct pipe_end *)target, events);
    break;
  case OBJECT_CONSOLE:
    return console_ready((struct console_object *)target, entry->active, entry->observed,
        entry->set->account->owner);
  case OBJECT_TERMINAL_INPUT:
  case OBJECT_TERMINAL_OUTPUT:
    ready = terminal_application_ready(target, events, entry->observed,
        entry->set->account->owner);
    break;
  case OBJECT_TCP:
  case OBJECT_TCP_LISTENER: {
    struct readiness_interest interest = {.object = target, .events = events};
    ready = tcp_readiness_events(&interest);
    break;
  }
  default:
    KASSERT(false);
    return WAIT_ERROR;
  }
  return entry->active ? ready : ready & WAIT_ERROR;
}

static void account_uncharge(struct readiness_account *account, size_t bytes)
{
  uint64_t flags = lock_sets();
  KASSERT(bytes <= account->bytes);
  account->bytes -= bytes;
  unlock_sets(flags);
}

static void *account_alloc(struct readiness_account *account, size_t bytes,
    enum call_status *status)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = lock_sets();
  if (bytes > READINESS_BUDGET - account->bytes) {
    *status = CALL_LIMIT;
    unlock_sets(flags);
    return NULL;
  }
  account->bytes += bytes;
  unlock_sets(flags);
  flags = cpu_save_interrupts();
  void *allocation = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!allocation) {
    account_uncharge(account, bytes);
    *status = CALL_NO_MEMORY;
  }
  return allocation;
}

static void account_free(struct readiness_account *account, void *allocation,
    size_t bytes)
{
  if (allocation) {
    uint64_t flags = cpu_save_interrupts();
    kfree(allocation);
    cpu_restore_interrupts(flags);
    account_uncharge(account, bytes);
  }
}

static void account_put(struct readiness_account *account)
{
  uint64_t flags = lock_sets();
  KASSERT(account->references);
  bool last = !--account->references;
  unlock_sets(flags);
  if (last) {
    KASSERT(!account->owner && !account->sets && !account->close_queued &&
        !account->cleanup && account->bytes == sizeof(*account));
    flags = cpu_save_interrupts();
    kfree(account);
    cpu_restore_interrupts(flags);
  }
}

static void finish_account_cleanup(struct readiness_account *account)
{
  uint64_t flags = lock_sets();
  struct execution_group *cleanup = NULL;
  if (account->closing && !account->sets) {
    cleanup = account->cleanup;
    account->cleanup = NULL;
  }
  unlock_sets(flags);
  if (cleanup) {
    flags = cpu_save_interrupts();
    execution_group_cleanup_end(cleanup);
    cpu_restore_interrupts(flags);
  }
}

static void destroy_set(struct kernel_object *object)
{
  struct readiness_set *set = (struct readiness_set *)object;
  struct readiness_account *account = set->account;
  KASSERT(!set->attached && !set->count && !set->collector &&
      !set->commands && !set->deferred && !set->collecting && !set->first && !set->buckets);
  account_free(account, set, sizeof(*set));
  account_put(account);
}

static void complete_request(struct readiness_set_request *request,
    enum call_status status)
{
  uint64_t flags = cpu_save_interrupts();
  request->status = status;
  bsp_request_complete(&request->request);
  cpu_restore_interrupts(flags);
}

static enum call_status create_set(struct readiness_set_request *request)
{
  struct process *process = request->caller;
  struct readiness_account *account = process->readiness_account;
  if (!account) {
    uint64_t allocation_flags = cpu_save_interrupts();
    account = kmalloc(sizeof(*account));
    cpu_restore_interrupts(allocation_flags);
    if (!account) {
      return CALL_NO_MEMORY;
    }
    *account = (struct readiness_account){.owner = process,
        .references = 1, .bytes = sizeof(*account), .cleanup = object_cleanup_defer()};
    uint64_t flags = lock_sets();
    process->readiness_account = account;
    unlock_sets(flags);
  }
  if (account->closing || task_wait_stop_requested(request->request.wait)) {
    return CALL_ENDPOINT_CLOSED;
  }
  enum call_status status = CALL_OK;
  struct readiness_set *set = account_alloc(account, sizeof(*set), &status);
  if (!set) {
    return status;
  }
  *set = (struct readiness_set){.account = account,
      .network = net_worker_available(), .next_identity = 1};
  object_init(&set->object, OBJECT_READINESS_SET, destroy_set);
  set->ready_tail = &set->ready_first;
  set->commands_tail = &set->commands;
  set->deferred_tail = &set->deferred;
  uint64_t flags = lock_sets();
  if (account->closing || task_wait_stop_requested(request->request.wait)) {
    unlock_sets(flags);
    account_free(account, set, sizeof(*set));
    return CALL_ENDPOINT_CLOSED;
  }
  ++account->references;
  KASSERT(object_retain(&set->object)); /* Account's active-set reference. */
  set->owner_next = account->sets;
  set->owner_previous = &account->sets;
  if (account->sets) {
    account->sets->owner_previous = &set->owner_next;
  }
  account->sets = set;
  set->attached = true;
  unlock_sets(flags);
  request->set = set; /* Initial reference passes to the syscall. */
  request->created = &set->object;
  return CALL_OK;
}

bool readiness_set_owned(const struct kernel_object *object, struct process *caller)
{
  const struct readiness_set *set = (const struct readiness_set *)object;
  uint64_t flags = lock_sets();
  bool owned = set->account->owner == caller && !set->account->closing;
  unlock_sets(flags);
  return owned;
}

bool readiness_set_grant_retain(struct kernel_object *object, uint64_t rights)
{
  struct readiness_set *set = (struct readiness_set *)object;
  uint64_t flags = lock_sets();
  bool result = !(rights & READINESS_SET_RIGHT_CONTROL) ||
      (!set->closing && !set->account->closing && set->controls != SIZE_MAX);
  if (result && (rights & READINESS_SET_RIGHT_CONTROL)) {
    ++set->controls;
  }
  unlock_sets(flags);
  return result;
}

void readiness_set_grant_release(struct kernel_object *object, uint64_t rights)
{
  if (!(rights & READINESS_SET_RIGHT_CONTROL)) {
    return;
  }
  struct readiness_set *set = (struct readiness_set *)object;
  uint64_t flags = lock_sets();
  KASSERT(set->controls);
  if (!--set->controls) {
    set->closing = true;
    schedule_set(set);
  }
  unlock_sets(flags);
  notify_workers();
}

uint64_t readiness_set_ready(struct kernel_object *object, struct process *caller)
{
  struct readiness_set *set = (struct readiness_set *)object;
  uint64_t flags = lock_sets();
  uint64_t ready = set->closing || set->account->closing ? WAIT_CLOSED :
      set->account->owner != caller ? WAIT_ERROR :
      set->ready_first || set->wake ? WAIT_READABLE : 0;
  unlock_sets(flags);
  return ready;
}

void readiness_set_process_stop(struct process *process)
{
  uint64_t flags = lock_sets();
  struct readiness_account *account = process->readiness_account;
  if (account && !account->closing) {
    account->closing = true;
    account->owner = NULL;
    account->close_cursor = account->sets;
    account->close_queued = true;
    ++account->references;
    account->close_next = closing_accounts;
    closing_accounts = account;
  }
  unlock_sets(flags);
  notify_workers();
}

void readiness_set_process_exit(struct process *process)
{
  readiness_set_process_stop(process);
  uint64_t flags = lock_sets();
  struct readiness_account *account = process->readiness_account;
  process->readiness_account = NULL;
  unlock_sets(flags);
  if (account) {
    account_put(account);
  }
}

static size_t registration_bucket(uint64_t identity, size_t count)
{
  identity ^= identity >> 30;
  identity *= UINT64_C(0xbf58476d1ce4e5b9);
  identity ^= identity >> 27;
  return identity & (count - 1);
}

/* The owning BSP worker alone changes indices; AP publication only changes a
 * loaned record's masks under sets_lock. Index zeroing/copying is preemptible. */
static void clear_index(struct readiness_registration **index, size_t count)
{
  for (size_t offset = 0; offset < count;) {
    size_t chunk = count - offset;
    if (chunk > PAGE_SIZE / sizeof(*index)) {
      chunk = PAGE_SIZE / sizeof(*index);
    }
    memset(index + offset, 0, chunk * sizeof(*index));
    offset += chunk;
    kernel_task_yield_if_runnable();
  }
}

static enum call_status grow_index(struct readiness_set *set)
{
  if (set->bucket_count && set->count < set->bucket_count * 2) {
    return CALL_OK;
  }
  KASSERT(!set->old_buckets);
  size_t count = set->bucket_count ? set->bucket_count * 2 : READINESS_INITIAL_BUCKETS;
  enum call_status status = CALL_OK;
  struct readiness_registration **buckets = account_alloc(set->account,
      count * sizeof(*buckets), &status);
  if (!buckets) {
    return status;
  }
  clear_index(buckets, count);
  set->old_buckets = set->buckets;
  set->old_count = set->bucket_count;
  set->grow_cursor = 0;
  set->buckets = buckets;
  set->bucket_count = count;
  return CALL_OK;
}

static void rehash_step(struct readiness_set *set)
{
  size_t work = READINESS_WORK_BUDGET;
  while (set->old_buckets && work--) {
    struct readiness_registration **head = &set->old_buckets[set->grow_cursor];
    struct readiness_registration *entry = *head;
    if (entry) {
      *head = entry->hash_next;
      size_t bucket = registration_bucket(entry->identity, set->bucket_count);
      entry->hash_next = set->buckets[bucket];
      set->buckets[bucket] = entry;
    } else if (++set->grow_cursor == set->old_count) {
      account_free(set->account, set->old_buckets,
          set->old_count * sizeof(*set->old_buckets));
      set->old_buckets = NULL;
      set->old_count = set->grow_cursor = 0;
    }
  }
}

static struct readiness_registration *find_registration(struct readiness_set *set,
    struct readiness_set_registration key)
{
  struct readiness_registration **indices[2] = {set->buckets, set->old_buckets};
  size_t counts[2] = {set->bucket_count, set->old_count};
  for (size_t i = 0; i < 2; ++i) {
    if (!counts[i]) {
      continue;
    }
    size_t bucket = registration_bucket(key.identity, counts[i]);
    for (struct readiness_registration *entry = indices[i][bucket]; entry;
         entry = entry->hash_next) {
      if (entry->identity == key.identity && entry->generation == key.generation &&
          !entry->provisional) {
        return entry;
      }
    }
  }
  return NULL;
}

static void remove_index(struct readiness_set *set, struct readiness_registration *entry)
{
  struct readiness_registration **indices[2] = {set->buckets, set->old_buckets};
  size_t counts[2] = {set->bucket_count, set->old_count};
  for (size_t i = 0; i < 2; ++i) {
    if (!counts[i]) {
      continue;
    }
    struct readiness_registration **link = &indices[i][registration_bucket(
        entry->identity, counts[i])];
    while (*link && *link != entry) {
      link = &(*link)->hash_next;
    }
    if (*link) {
      *link = entry->hash_next;
      return;
    }
  }
  KASSERT(false);
}

/* Joins CUT before releasing lineage/storage. Source cursors are detached under
 * the same guard as producer publication; all actor borrows stay on this worker. */
static void release_source(struct readiness_registration *entry)
{
  uint64_t flags = cpu_save_interrupts();
  forwarder_operation_readiness(&entry->lineage, false);
  uint64_t guard = lock_sets();
  source_detach(entry);
  struct kernel_object *target = entry->target;
  entry->target = NULL;
  unlock_sets(guard);
  struct execution_group *previous = object_cleanup_enter(entry->set->account->cleanup);
  forwarder_operation_release(&entry->lineage);
  if (target) {
    object_release(target);
  }
  object_cleanup_leave(previous);
  cpu_restore_interrupts(flags);
}

static void remove_invalidation(struct readiness_registration *entry)
{
  if (!entry->invalid_previous) {
    return;
  }
  *entry->invalid_previous = entry->invalid_next;
  if (entry->invalid_next) {
    entry->invalid_next->invalid_previous = entry->invalid_previous;
  }
  entry->invalid_next = NULL;
  entry->invalid_previous = NULL;
}

static void invalidate_step(struct readiness_set *set)
{
  uint64_t flags = lock_sets();
  struct readiness_registration *entry = set->invalid;
  KASSERT(entry);
  remove_invalidation(entry);
  unlock_sets(flags);
  release_source(entry);
}

static void destroy_registration(struct readiness_registration *entry)
{
  struct readiness_set *set = entry->set;
  KASSERT(!entry->loaned && !entry->delivering);
  release_source(entry);
  uint64_t flags = lock_sets();
  ready_remove(entry);
  remove_invalidation(entry);
  *entry->previous = entry->next;
  if (entry->next) {
    entry->next->previous = entry->previous;
  }
  --set->count;
  unlock_sets(flags);
  remove_index(set, entry);
  account_free(set->account, entry, sizeof(*entry));
}

static void evaluate_registration(struct readiness_registration *entry)
{
  uint64_t flags = lock_sets();
  bool eligible = !entry->provisional && !entry->loaned && !entry->delivering &&
      !entry->set->closing && !entry->set->account->closing;
  bool denied = entry->denied;
  bool terminal = entry->status != CALL_OK;
  unlock_sets(flags);
  if (!eligible) {
    return;
  }
  uint64_t events = denied || terminal ? 0 : source_events(entry);
  enum call_status status = CALL_OK;
  if (events & WAIT_ERROR) {
    switch (entry->target->type) {
    case OBJECT_TCP:
    case OBJECT_TCP_LISTENER:
      status = tcp_readiness_status(entry->target);
      break;
    case OBJECT_CONSOLE:
      /* Overflow and unavailable keyboards recover after a read or reattach. */
      break;
    default:
      status = CALL_ENDPOINT_CLOSED;
      break;
    }
  }
  flags = lock_sets();
  if (entry->denied) {
    entry->status = CALL_DENIED;
    events = 0;
  } else if (status != CALL_OK) {
    entry->status = status;
  }
  entry->ready = events;
  if (entry->status != CALL_OK || events) {
    ready_insert(entry);
  } else {
    ready_remove(entry);
  }
  terminal = entry->status != CALL_OK;
  unlock_sets(flags);
  if (terminal && entry->target) {
    release_source(entry);
  }
}

static enum call_status prepare_registration(struct readiness_set_request *request)
{
  struct readiness_set *set = request->set;
  if (!set->next_identity) {
    return CALL_LIMIT;
  }
  enum call_status status = CALL_OK;
  struct readiness_registration *entry = account_alloc(set->account, sizeof(*entry), &status);
  if (!entry) {
    return status;
  }
  *entry = (struct readiness_registration){.set = set, .network = set->network,
      .identity = set->next_identity, .generation = 1, .tag = request->tag,
      .allowed = request->allowed_events, .pending_active = request->active_events,
      .pending_observed = request->observed_generation, .provisional = true, .loaned = true};
  forwarder_operation_init(&entry->lineage);
  uint64_t flags = cpu_save_interrupts();
  enum capability_result result = capability_reference_resolve_for(&request->target,
      &entry->lineage);
  cpu_restore_interrupts(flags);
  if (result != CAP_OK) {
    status = result == CAP_DENIED ? CALL_DENIED : result == CAP_NO_MEMORY ?
        CALL_NO_MEMORY : result == CAP_LIMIT ? CALL_LIMIT : CALL_BAD_HANDLE;
  } else {
    status = readiness_interest_authority(request->target.object, request->caller,
        request->target.rights, request->target.transport, request->allowed_events);
  }
  struct readiness_source *source = status == CALL_OK ? object_source(request->target.object) : NULL;
  if (status == CALL_OK && !source) {
    status = CALL_BAD_REQUEST;
  }
  if (status == CALL_OK && !set->network &&
      (request->target.object->type == OBJECT_TCP || request->target.object->type == OBJECT_TCP_LISTENER)) {
    status = CALL_UNAVAILABLE;
  }
  if (status == CALL_OK) {
    status = grow_index(set);
  }
  if (status != CALL_OK) {
    flags = cpu_save_interrupts();
    forwarder_operation_release(&entry->lineage);
    cpu_restore_interrupts(flags);
    account_free(set->account, entry, sizeof(*entry));
    return status;
  }
  flags = lock_sets();
  entry->target = request->target.object;
  request->target.object = NULL;
  entry->next = set->first;
  entry->previous = &set->first;
  if (entry->next) {
    entry->next->previous = &entry->next;
  }
  set->first = entry;
  ++set->count;
  ++set->next_identity; /* Zero permanently refuses the next admission. */
  source_attach(entry, source);
  unlock_sets(flags);
  size_t bucket = registration_bucket(entry->identity, set->bucket_count);
  entry->hash_next = set->buckets[bucket];
  set->buckets[bucket] = entry;
  flags = cpu_save_interrupts();
  forwarder_operation_readiness(&entry->lineage, true);
  cpu_restore_interrupts(flags);
  request->prepared = entry;
  request->registration_reply = (struct readiness_set_registration){entry->identity, 1};
  return CALL_OK;
}

/* Called after task_operation_start and output validation under a live current
 * mapping lease. Fixed publication cannot allocate, query actors or wait. */
enum call_status readiness_set_publish(struct readiness_set_request *request,
    const struct user_buffer_lease *lease, uintptr_t address, size_t capacity)
{
  if (request->operation == READINESS_SET_WAIT) {
    if (capacity < sizeof(request->wait_reply) ||
        !user_buffer_check_leased(lease, address, sizeof(request->wait_reply), USER_BUFFER_WRITE) ||
        !user_buffer_check_leased(lease, request->events_address,
            request->wait_reply.count * sizeof(request->events[0]), USER_BUFFER_WRITE)) {
      return CALL_BAD_BUFFER;
    }
    uint64_t flags = lock_sets();
    struct readiness_set *set = request->set;
    enum call_status status = set->closing || set->account->closing ? CALL_ENDPOINT_CLOSED : CALL_OK;
    if (status == CALL_OK) {
      size_t count = 0;
      for (struct readiness_registration *entry = set->delivery; entry; entry = entry->delivery_next) {
        entry->delivered_terminal = entry->status != CALL_OK;
        request->events[count++] = (struct readiness_set_event){
            .registration = {entry->identity, entry->generation}, .tag = entry->tag,
            .events = entry->status == CALL_OK ? entry->ready : 0, .status = entry->status};
      }
      KASSERT(count == request->wait_reply.count);
      memcpy((void *)request->events_address, request->events, count * sizeof(request->events[0]));
      memcpy((void *)address, &request->wait_reply, sizeof(request->wait_reply));
    }
    unlock_sets(flags);
    return status;
  }
  KASSERT(capacity >= sizeof(request->registration_reply));
  struct readiness_registration *entry = request->prepared;
  KASSERT(entry && entry->loaned);
  uint64_t flags = lock_sets();
  enum call_status status = entry->set->closing || entry->set->account->closing ?
      CALL_ENDPOINT_CLOSED : entry->denied ? CALL_DENIED : CALL_OK;
  if (status == CALL_OK) {
    memcpy((void *)address, &request->registration_reply, sizeof(request->registration_reply));
    entry->active = entry->pending_active;
    entry->observed = entry->pending_observed;
    entry->generation = request->registration_reply.generation;
    entry->provisional = false;
  }
  unlock_sets(flags);
  return status;
}

void readiness_set_abandon(struct kernel_object *object)
{
  struct readiness_set *set = (struct readiness_set *)object;
  uint64_t flags = lock_sets();
  set->closing = true;
  schedule_set(set);
  unlock_sets(flags);
  notify_workers();
}

static void remove_timer(struct readiness_set *set)
{
  if (!set->timer_previous) {
    return;
  }
  *set->timer_previous = set->timer_next;
  if (set->timer_next) {
    set->timer_next->timer_previous = set->timer_previous;
  }
  set->timer_next = NULL;
  set->timer_previous = NULL;
}

/* Each list has one BSP worker owner. Traversal yields without a guard; only
 * the final link publication and the constant-time earliest lookup need IF=0. */
static void insert_timer(struct readiness_set *set)
{
  KASSERT(set->collector && set->collector->deadline && !set->timer_previous);
  struct readiness_set **link = &timers[set->network];
  size_t work = 0;
  while (*link && (*link)->collector->deadline <= set->collector->deadline) {
    link = &(*link)->timer_next;
    if (++work == READINESS_WORK_BUDGET) {
      kernel_task_yield_if_runnable();
      work = 0;
    }
  }
  uint64_t flags = lock_sets();
  set->timer_next = *link;
  set->timer_previous = link;
  if (*link) {
    (*link)->timer_previous = &set->timer_next;
  }
  *link = set;
  unlock_sets(flags);
}

static void finish_collection(struct readiness_set *set, bool copied)
{
  uint64_t flags = lock_sets();
  KASSERT(set->collecting && !set->collector);
  struct readiness_registration *entry = set->delivery;
  set->delivery = NULL;
  while (entry) {
    struct readiness_registration *next = entry->delivery_next;
    entry->delivery_next = NULL;
    entry->delivering = false;
    if (copied && entry->delivered_terminal) {
      entry->terminal_sent = true;
    }
    entry->delivered_terminal = false;
    if (entry->status != CALL_OK || entry->ready) {
      ready_insert(entry);
    }
    entry = next;
  }
  if (!copied && set->delivered_wake) {
    set->wake = true;
  }
  set->delivered_wake = 0;
  set->collecting = false;
  schedule_set(set);
  unlock_sets(flags);
}

static bool finish_operation(struct readiness_set_request *request)
{
  struct readiness_set *set = request->set;
  if (request->finish_operation == READINESS_SET_WAIT) {
    finish_collection(set, request->copied);
  } else {
    struct readiness_registration *entry = request->prepared;
    KASSERT(entry && entry->set == set && entry->loaned);
    uint64_t flags = lock_sets();
    entry->loaned = false;
    bool discard = request->finish_operation == READINESS_SET_REGISTER && !request->copied;
    unlock_sets(flags);
    if (discard) {
      destroy_registration(entry);
    } else {
      evaluate_registration(entry);
    }
  }
  uint64_t flags = lock_sets();
  resume_deferred(set);
  schedule_set(set);
  unlock_sets(flags);
  complete_request(request, CALL_OK);
  notify_workers();
  return true;
}

static bool handle_command(struct readiness_set_request *request)
{
  struct readiness_set *set = request->set;
  if (request->operation == READINESS_FINISH) {
    return finish_operation(request);
  }
  uint64_t flags = cpu_save_interrupts();
  bool denied = forwarder_operation_denied(request->request.operation);
  cpu_restore_interrupts(flags);
  flags = lock_sets();
  enum call_status status = denied ? CALL_DENIED : set->account->owner != request->caller ?
      CALL_DENIED : set->closing || set->account->closing ||
      task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_OK;
  unlock_sets(flags);
  if (status != CALL_OK) {
    complete_request(request, status);
    return true;
  }
  switch (request->operation) {
  case READINESS_SET_REGISTER:
    status = prepare_registration(request);
    break;
  case READINESS_SET_UPDATE:
  case READINESS_SET_REMOVE: {
    flags = lock_sets();
    struct readiness_registration *entry = find_registration(set, request->key);
    unlock_sets(flags);
    if (!entry) {
      status = CALL_NOT_FOUND;
    } else if (entry->loaned || entry->delivering) {
      return false; /* FINISH joins the bounded AP publication before this command. */
    } else if (request->operation == READINESS_SET_REMOVE) {
      destroy_registration(entry);
    } else if ((request->active_events & ~entry->allowed) || entry->status != CALL_OK) {
      status = entry->status != CALL_OK ? entry->status : CALL_BAD_REQUEST;
    } else if (entry->generation == UINT64_MAX) {
      status = CALL_LIMIT;
    } else {
      flags = lock_sets();
      entry->loaned = true;
      entry->pending_active = request->active_events;
      entry->pending_observed = request->observed_generation;
      request->prepared = entry;
      request->registration_reply = (struct readiness_set_registration){
          entry->identity, entry->generation + 1};
      unlock_sets(flags);
    }
    break;
  }
  case READINESS_SET_WAIT:
    flags = lock_sets();
    if (set->collecting) {
      status = CALL_BUSY;
    } else {
      set->collecting = true;
      set->collector = request;
      set->scan_remaining = set->count;
      schedule_set(set);
    }
    unlock_sets(flags);
    if (status == CALL_OK) {
      return true; /* Owned by the collector until a staged batch or timeout. */
    }
    break;
  case READINESS_SET_WAKE:
    flags = lock_sets();
    set->wake = true;
    schedule_set(set);
    unlock_sets(flags);
    notify_workers();
    break;
  case READINESS_SET_INFO:
    flags = lock_sets();
    request->info_reply = (struct readiness_set_info){READINESS_BUDGET,
        set->account->bytes, set->count};
    unlock_sets(flags);
    break;
  default:
    status = CALL_BAD_OPERATION;
    break;
  }
  complete_request(request, status);
  return true;
}

static bool service_source(size_t domain)
{
  uint64_t flags = lock_sets();
  struct readiness_source *source = source_heads[domain];
  if (!source) {
    unlock_sets(flags);
    return false;
  }
  if (!source->cursor[domain]) {
    if (!source->again[domain]) {
      unqueue_source(source, domain);
      unlock_sets(flags);
      return true;
    }
    source->cursor[domain] = source->first;
    source->again[domain] = false;
  }
  struct readiness_registration *entry = source->cursor[domain];
  if (entry) {
    source->cursor[domain] = entry->source_next;
  }
  bool evaluate = entry && entry->network == domain;
  if (source->next[domain]) {
    /* Keep the cursor while rotating: a hot source cannot starve other sources. */
    *source->previous[domain] = source->next[domain];
    source->next[domain]->previous[domain] = source->previous[domain];
    source->next[domain] = NULL;
    source->previous[domain] = source_tails[domain];
    *source_tails[domain] = source;
    source_tails[domain] = &source->next[domain];
  }
  unlock_sets(flags);
  if (evaluate) {
    evaluate_registration(entry);
  }
  return true;
}

/* At most the caller's batch window is borrowed between collection and ACK.
 * Level records rotate to the tail only after the reply is actually copied. */
static bool collect_step(struct readiness_set *set)
{
  struct readiness_set_request *request = set->collector;
  KASSERT(request && set->collecting);
  size_t work = READINESS_WORK_BUDGET;
  while (work--) {
    uint64_t flags = lock_sets();
    struct readiness_registration *entry = set->ready_first;
    if (!entry || !set->scan_remaining || request->wait_reply.count == request->capacity) {
      unlock_sets(flags);
      break;
    }
    --set->scan_remaining;
    ready_remove(entry);
    bool available = !entry->loaned && !entry->provisional;
    unlock_sets(flags);
    if (!available) {
      continue;
    }
    evaluate_registration(entry);
    flags = lock_sets();
    bool ready = entry->ready || (entry->status != CALL_OK && !entry->terminal_sent);
    if (ready) {
      ready_remove(entry);
      entry->delivering = true;
      entry->delivery_next = set->delivery;
      set->delivery = entry;
      struct readiness_set_event *event = &request->events[request->wait_reply.count++];
      *event = (struct readiness_set_event){.registration = {entry->identity, entry->generation},
          .tag = entry->tag, .events = entry->ready, .status = entry->status};
    }
    unlock_sets(flags);
  }
  uint64_t flags = lock_sets();
  bool remaining = set->scan_remaining && set->ready_first &&
      request->wait_reply.count < request->capacity;
  if (remaining) {
    schedule_set(set);
    unlock_sets(flags);
    return true;
  }
  bool woken = set->wake;
  bool closed = set->closing || set->account->closing;
  bool expired = request->deadline && clock_coarse_ns() >= request->deadline;
  if (!closed && !request->wait_reply.count && !woken && request->deadline && !expired) {
    set->scan_remaining = set->count;
    unlock_sets(flags);
    if (!set->timer_previous) {
      insert_timer(set);
    }
    return false;
  }
  remove_timer(set);
  set->collector = NULL;
  if (!closed && !request->wait_reply.count && !woken && expired) {
    set->collecting = false;
  } else {
    set->delivered_wake = woken;
    set->wake = false;
    request->wait_reply.flags = woken ? READINESS_SET_WOKEN : 0;
  }
  unlock_sets(flags);
  if (closed) {
    finish_collection(set, false);
  }
  complete_request(request, closed ? CALL_ENDPOINT_CLOSED :
      request->wait_reply.count || woken || !request->deadline ? CALL_OK : CALL_TIMED_OUT);
  return true;
}

static bool close_step(struct readiness_set *set)
{
  uint64_t flags = lock_sets();
  struct readiness_registration *entry = set->first;
  if (entry && (entry->loaned || entry->delivering)) {
    unlock_sets(flags);
    return false;
  }
  bool blocked = set->commands || set->deferred || set->collector || set->collecting;
  unlock_sets(flags);
  if (entry) {
    destroy_registration(entry);
    flags = lock_sets();
    schedule_set(set);
    unlock_sets(flags);
    return true;
  }
  if (blocked) {
    return false;
  }
  account_free(set->account, set->buckets, set->bucket_count * sizeof(*set->buckets));
  account_free(set->account, set->old_buckets, set->old_count * sizeof(*set->old_buckets));
  set->buckets = set->old_buckets = NULL;
  set->bucket_count = set->old_count = 0;
  flags = lock_sets();
  remove_timer(set);
  KASSERT(!set->work_previous && set->attached);
  struct readiness_account *account = set->account;
  if (account->close_cursor == set) {
    account->close_cursor = set->owner_next;
  }
  *set->owner_previous = set->owner_next;
  if (set->owner_next) {
    set->owner_next->owner_previous = set->owner_previous;
  }
  set->owner_previous = NULL;
  set->owner_next = NULL;
  set->attached = false;
  unlock_sets(flags);
  finish_account_cleanup(account);
  flags = cpu_save_interrupts();
  object_release(&set->object); /* Drop account active-set reference last. */
  cpu_restore_interrupts(flags);
  return true;
}

static bool service_account(void)
{
  uint64_t flags = lock_sets();
  struct readiness_account *account = closing_accounts;
  if (!account) {
    unlock_sets(flags);
    return false;
  }
  struct readiness_set *set = account->close_cursor;
  if (set) {
    account->close_cursor = set->owner_next;
    set->closing = true;
    schedule_set(set);
  } else {
    closing_accounts = account->close_next;
    account->close_next = NULL;
    account->close_queued = false;
  }
  unlock_sets(flags);
  if (!set) {
    finish_account_cleanup(account);
    account_put(account);
  }
  return true;
}

void readiness_set_submit(struct readiness_set_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->operation == READINESS_CREATE) {
    struct execution_group *previous = object_cleanup_enter(request->request.cleanup_group);
    enum call_status status = create_set(request);
    object_cleanup_leave(previous);
    complete_request(request, status);
    return;
  }
  uint64_t flags = lock_sets();
  struct readiness_set *set = request->set;
  if (!set->attached) {
    unlock_sets(flags);
    KASSERT(request->operation != READINESS_FINISH);
    complete_request(request, CALL_ENDPOINT_CLOSED);
    return;
  }
  *set->commands_tail = &request->request;
  set->commands_tail = &request->request.next;
  schedule_set(set);
  unlock_sets(flags);
  notify_workers();
}

bool readiness_sets_service(bool network)
{
  KASSERT(arch_cpu_index() == 0);
  size_t domain = network;
  bool worked = false;
  for (size_t work = 0; work < READINESS_WORK_BUDGET; ++work) {
    if ((work % 8 == 0 && service_account()) || (work % 2 && service_source(domain))) {
      worked = true;
      continue;
    }
    uint64_t flags = lock_sets();
    if (timers[domain] && timers[domain]->collector->deadline <= clock_coarse_ns()) {
      struct readiness_set *expired = timers[domain];
      remove_timer(expired);
      schedule_set(expired);
    }
    struct readiness_set *set = work_heads[domain];
    if (set) {
      KASSERT(object_retain(&set->object)); /* Worker borrow through possible detach. */
      unqueue_set(set);
      if (set->closing || set->account->closing) {
        resume_deferred(set);
      }
    }
    bool actor_turn = set && (set->invalid || (set->collector && set->collector_turn));
    if (set && set->collector) {
      set->collector_turn = !set->collector_turn;
    }
    struct readiness_set_request *request = set && !actor_turn ? (void *)set->commands : NULL;
    if (request) {
      set->commands = request->request.next;
      request->request.next = NULL;
      if (!set->commands) {
        set->commands_tail = &set->commands;
      }
    }
    unlock_sets(flags);
    if (set) {
      bool deferred = false;
      bool invalidated = false;
      rehash_step(set);
      if (request) {
        if (!handle_command(request)) {
          deferred = true;
          flags = lock_sets();
          *set->deferred_tail = &request->request;
          set->deferred_tail = &request->request.next;
          unlock_sets(flags);
        }
      } else if (set->invalid) {
        invalidate_step(set);
        invalidated = true;
      } else if (set->collector) {
        collect_step(set);
      } else if (set->closing || set->account->closing) {
        close_step(set);
      }
      flags = lock_sets();
      if (set->attached && (set->commands || set->invalid || set->old_buckets ||
          ((request || invalidated) && !deferred && (set->closing || set->account->closing)) ||
          (set->collector && (set->ready_first || set->wake || set->closing || set->account->closing)))) {
        schedule_set(set);
      }
      unlock_sets(flags);
      flags = cpu_save_interrupts();
      object_release(&set->object);
      cpu_restore_interrupts(flags);
      worked = true;
    } else if (service_source(domain) || service_account()) {
      worked = true;
    } else {
      break;
    }
  }
  if (worked) {
    notify_workers();
  }
  return worked;
}

bool readiness_sets_pending(bool network)
{
  uint64_t flags = lock_sets();
  bool pending = work_heads[network] || source_heads[network] || closing_accounts;
  unlock_sets(flags);
  return pending;
}

bool readiness_sets_deadline(bool network, uint64_t *deadline)
{
  uint64_t flags = lock_sets();
  bool pending = work_heads[network] || source_heads[network] || closing_accounts;
  bool found = pending || timers[network];
  if (found) {
    *deadline = pending ? 0 : timers[network]->collector->deadline;
  }
  unlock_sets(flags);
  return found;
}

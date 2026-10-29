#include <kernel/object/wifi.h>
#include <kernel/mm/shared_view.h>
#include <kernel/process_vm.h>
#include <kernel/wait.h>
#include <kernel/object/wifi_auth.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <abi/console.h>
#include <abi/execution_group.h>
#include <abi/terminal.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/endpoint.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/user/wait.h>
#include <kernel/user/readiness_set.h>
#include <kernel/wait.h>
#include <kernel/net/interface.h>
#include <kernel/bluetooth.h>

/* Bounded admission walk: forwarder, controller and leaf storage per node. */
#define FORWARDER_TARGET_GRAPH_MAX (3 * FORWARDER_LINEAGE_MAX + 1)

struct forwarder {
  struct kernel_object object;
  struct forwarder *next;
  struct forwarder **previous;
  struct forwarder *cleanup_next;
  struct capability_grant target;
  struct capability_grant detached;
  struct kernel_object *detached_storage;
  struct forwarder *ancestors[FORWARDER_LINEAGE_MAX];
  size_t ancestor_count, holders, controllers;
  size_t mapping_pending;
  struct task_wait_link *mapping_waiters;
  enum object_type type;
  uint64_t protocol, rights, transport, kind;
  bool cut, target_owned, reserved, cleaning, mapping_unknown;
};

struct forwarder_controller {
  struct kernel_object object;
  struct forwarder *forwarder;
};

/* IF=0. May nest atomic storage retains and scheduler wakeup, or the readiness
 * and shared-view queue guards for bounded callbacks. No VM/lifetime work is
 * permitted in shared-view callbacks. Readiness workers wake after unlocking.
 * Never table/source guards, allocation, user copies or logical grant callbacks.
 * Neither callback queue guard may nest this guard. */
static struct spinlock forwarders_lock;
static struct forwarder *forwarders;
static struct forwarder_operation *operations;
static struct forwarder_mapping *mappings;

static void mapping_notify(struct forwarder *node)
{
  while (node->mapping_waiters) {
    struct task_wait_link *link = node->mapping_waiters;
    node->mapping_waiters = link->next;
    struct task_wait *wait = link->wait;
    link->next = NULL;
    link->wait = NULL;
    task_wait_wake(wait);
  }
}

static bool mapping_contains(const struct forwarder_mapping *mapping,
    const struct forwarder *node)
{
  for (size_t i = 0; i < mapping->count; ++i) {
    if (mapping->nodes[i] == node) {
      return true;
    }
  }
  return false;
}

static bool mapping_cut(const struct forwarder_mapping *mapping)
{
  for (size_t i = 0; i < mapping->count; ++i) {
    if (mapping->nodes[i]->cut) {
      return true;
    }
  }
  return false;
}

static void mapping_mark_unknown(struct forwarder_mapping *mapping)
{
  for (size_t i = 0; i < mapping->count; ++i) {
    struct forwarder *node = mapping->nodes[i];
    if (!node->mapping_unknown) {
      node->mapping_unknown = true;
      mapping_notify(node);
    }
  }
}

/* Forwarder guard held. Substitution checks a proposed edge without publishing
 * it; every reachable chain stays bounded, including existing incoming slots. */
static bool target_depth_valid(const struct forwarder *root,
    const struct kernel_object *replacement, const struct kernel_object *target)
{
  size_t depth = 0;
  while (target && (target->type == OBJECT_FORWARDER ||
      target->type == OBJECT_FORWARDER_CONTROLLER)) {
    if (++depth > FORWARDER_LINEAGE_MAX) {
      return false;
    }
    if (target->type == OBJECT_FORWARDER_CONTROLLER) {
      target = &((const struct forwarder_controller *)target)->forwarder->object;
    } else {
      const struct forwarder *forwarder = (const struct forwarder *)target;
      target = forwarder == root ? replacement : forwarder->target.object;
    }
  }
  return true;
}

static bool retarget_depth_valid(const struct forwarder *root,
    const struct kernel_object *replacement)
{
  for (struct forwarder *forwarder = forwarders; forwarder; forwarder = forwarder->next) {
    if (!target_depth_valid(root, replacement, &forwarder->object)) {
      return false;
    }
  }
  return true;
}

static enum capability_result cap_status(enum call_status status)
{
  switch (status) {
  case CALL_OK: return CAP_OK;
  case CALL_LIMIT: return CAP_LIMIT;
  case CALL_NO_MEMORY: return CAP_NO_MEMORY;
  default: return CAP_DENIED;
  }
}

static enum call_status call_status(enum capability_result status)
{
  switch (status) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

static bool lineage_cut(const struct forwarder *forwarder)
{
  if (forwarder->cut) {
    return true;
  }
  for (size_t i = 0; i < forwarder->ancestor_count; ++i) {
    if (forwarder->ancestors[i]->cut) {
      return true;
    }
  }
  return false;
}

static bool operation_cut(const struct forwarder_operation *operation)
{
  for (size_t i = 0; i < operation->count; ++i) {
    if (operation->nodes[i]->cut) {
      return true;
    }
  }
  return false;
}

static void unlink_forwarder(struct forwarder *forwarder)
{
  KASSERT(forwarder->reserved && forwarder->previous);
  *forwarder->previous = forwarder->next;
  if (forwarder->next) {
    forwarder->next->previous = forwarder->previous;
  }
  forwarder->previous = NULL;
  forwarder->next = NULL;
  forwarder->reserved = false;
}

bool forwarder_storage_release(struct kernel_object *object)
{
  struct forwarder *forwarder = (struct forwarder *)object;
  spin_lock(&forwarders_lock);
  size_t previous = atomic_fetch_sub_explicit(&object->references, 1, memory_order_acq_rel);
  KASSERT(previous);
  bool final = previous == 1;
  if (previous == 2 && forwarder->reserved && !forwarder->cleaning) {
    /* The registry's reserved storage reference protects CUT's detached
     * cleanup without a fallible retain. It is not logical holder authority. */
    unlink_forwarder(forwarder);
    KASSERT(atomic_fetch_sub_explicit(&object->references, 1, memory_order_acq_rel) == 1);
    final = true;
  }
  spin_unlock(&forwarders_lock);
  return final;
}

static void destroy_forwarder(struct kernel_object *object)
{
  struct forwarder *forwarder = (struct forwarder *)object;
  KASSERT(!forwarder->reserved && !forwarder->cleaning && !forwarder->holders &&
      !forwarder->controllers && !forwarder->previous &&
      !forwarder->mapping_pending && !forwarder->mapping_waiters);
  if (forwarder->target.object) {
    if (forwarder->target_owned) {
      object_grant_release(forwarder->target.object, forwarder->target.rights);
    }
    object_caretaker_release(forwarder->target.object);
  }
  for (size_t i = 0; i < forwarder->ancestor_count; ++i) {
    object_grant_release(&forwarder->ancestors[i]->object, 0);
  }
  kfree(forwarder);
}

static void destroy_controller(struct kernel_object *object)
{
  struct forwarder_controller *controller = (struct forwarder_controller *)object;
  object_release(&controller->forwarder->object);
  kfree(controller);
}

static void cut_forwarder(struct forwarder *root)
{
  struct forwarder *cleanup = NULL;
  struct forwarder_operation *notifications = NULL;
  spin_lock(&forwarders_lock);
  for (struct forwarder *forwarder = forwarders; forwarder; forwarder = forwarder->next) {
    bool descendant = forwarder == root;
    for (size_t i = 0; !descendant && i < forwarder->ancestor_count; ++i) {
      descendant = forwarder->ancestors[i] == root;
    }
    if (!descendant || forwarder->cut) {
      continue;
    }
    forwarder->cut = true;
    if (forwarder->target.object) {
      KASSERT(!forwarder->cleaning);
      forwarder->cleaning = true;
      if (forwarder->target_owned) {
        forwarder->detached = forwarder->target;
      }
      forwarder->detached_storage = forwarder->target.object;
      forwarder->target = (struct capability_grant){0};
      forwarder->target_owned = false;
      forwarder->cleanup_next = cleanup;
      cleanup = forwarder;
    }
  }
  for (struct forwarder_operation *operation = operations; operation; operation = operation->next) {
    if (operation->state != FORWARDER_PENDING || !operation_cut(operation)) {
      continue;
    }
    operation->state = FORWARDER_DENIED;
    if (operation->readiness_observer) {
      readiness_registration_revoked(operation);
    }
    if (operation->wait) {
      task_wait_wake(operation->wait);
    }
    if (operation->notification_owner && object_retain(operation->notification_owner)) {
      operation->notification_next = notifications;
      notifications = operation;
    }
  }
  for (struct forwarder_mapping *mapping = mappings; mapping; mapping = mapping->next) {
    if (!atomic_load_explicit(&mapping->revoked, memory_order_relaxed) &&
        mapping_cut(mapping)) {
      atomic_store_explicit(&mapping->revoked, true, memory_order_release);
      shared_view_request_withdrawal(mapping->owner, false);
    }
  }
  spin_unlock(&forwarders_lock);

  while (notifications) {
    struct forwarder_operation *operation = notifications;
    notifications = operation->notification_next;
    struct kernel_object *owner = operation->notification_owner;
    endpoint_forwarder_cancel(operation);
    object_release(owner);
  }
  while (cleanup) {
    struct forwarder *forwarder = cleanup;
    cleanup = forwarder->cleanup_next;
    capability_grant_release(&forwarder->detached);
    object_caretaker_release(forwarder->detached_storage);
    forwarder->detached_storage = NULL;
    spin_lock(&forwarders_lock);
    forwarder->cleaning = false;
    bool final = atomic_load_explicit(&forwarder->object.references, memory_order_relaxed) == 1;
    if (final) {
      unlink_forwarder(forwarder);
    }
    spin_unlock(&forwarders_lock);
    if (final) {
      object_release(&forwarder->object);
    }
  }
  readiness_notify();
  net_worker_notify();
  bluetooth_hci_revocation_notify();
  wifi_auth_revocation_notify();
}

enum object_type forwarder_type(const struct kernel_object *object)
{
  return ((const struct forwarder *)object)->type;
}

uint64_t forwarder_protocol(const struct kernel_object *object)
{
  return ((const struct forwarder *)object)->protocol;
}

bool forwarder_authority_valid(const struct kernel_object *object, uint64_t rights,
    uint64_t transport)
{
  if (object->type == OBJECT_FORWARDER_CONTROLLER) {
    const struct forwarder *value = ((const struct forwarder_controller *)object)->forwarder;
    uint64_t allowed = FORWARDER_RIGHT_CUT | (value->kind == FORWARDER_SLOT ? FORWARDER_RIGHT_RETARGET : 0);
    return !transport && !(rights & ~allowed);
  }
  const struct forwarder *forwarder = (const struct forwarder *)object;
  return !(rights & ~forwarder->rights) && !(transport & ~forwarder->transport);
}

bool forwarder_available(const struct kernel_object *object)
{
  return forwarder_status(object) == CALL_OK;
}

enum call_status forwarder_status(const struct kernel_object *object)
{
  if (object->type != OBJECT_FORWARDER) {
    return object->type != OBJECT_ENDPOINT_EXPORT || endpoint_export_available((struct kernel_object *)object) ?
        CALL_OK : CALL_ENDPOINT_CLOSED;
  }
  spin_lock(&forwarders_lock);
  enum call_status status = CALL_OK;
  size_t depth = 0;
  while (object && object->type == OBJECT_FORWARDER) {
    const struct forwarder *forwarder = (const struct forwarder *)object;
    if (++depth > FORWARDER_LINEAGE_MAX) {
      status = CALL_LIMIT;
      break;
    }
    if (lineage_cut(forwarder)) {
      status = CALL_DENIED;
      break;
    }
    object = forwarder->target.object;
  }
  if (status == CALL_OK && !object) {
    status = CALL_NOT_FOUND;
  }
  struct kernel_object *target = status == CALL_OK ? (struct kernel_object *)object : NULL;
  if (target && !object_retain(target)) {
    status = CALL_LIMIT;
    target = NULL;
  }
  spin_unlock(&forwarders_lock);
  if (target) {
    status = target->type != OBJECT_ENDPOINT_EXPORT || endpoint_export_available(target) ?
        CALL_OK : CALL_ENDPOINT_CLOSED;
    object_release(target);
  }
  return status;
}

static bool holder_keeps_target(const struct forwarder *forwarder, uint64_t rights)
{
  /* Storage-only observers must not preserve the native close-bearing grant.
   * Exported clients and pipes count even zero-right grants. Native console
   * ARMED/PASSTHROUGH handles carry that right alone, never alongside READ. */
  if (forwarder->type == OBJECT_ENDPOINT_EXPORT) {
    return true;
  }
  if (forwarder->protocol == PROTOCOL_CONSOLE) {
    return rights & (CONSOLE_RIGHT_READ | CONSOLE_RIGHT_WRITE |
        CONSOLE_RIGHT_ARMED | CONSOLE_RIGHT_PASSTHROUGH);
  }
  switch (forwarder->type) {
  case OBJECT_FORWARDER_CONTROLLER:
    return rights & FORWARDER_RIGHTS;
  case OBJECT_WIFI_LINK:
    return rights & WIFI_LINK_RIGHT_CONTROL;
  case OBJECT_WIFI_AUTH:
    return rights & WIFI_AUTH_RIGHT_RUN;
  case OBJECT_EXECUTION_GROUP:
    return rights & EXECUTION_GROUP_RIGHT_CONTROL;
  case OBJECT_TERMINAL_ATTACHMENT:
    return rights & TERMINAL_RIGHT_HANGUP;
  case OBJECT_TERMINAL_EVENTS:
    return rights & TERMINAL_EVENTS_RIGHT_EMIT;
  default:
    return true;
  }
}

static bool grant_retain_depth(struct kernel_object *object, uint64_t rights,
    size_t depth, size_t *budget)
{
  if (depth == FORWARDER_LINEAGE_MAX) {
    return false;
  }
  struct forwarder *forwarder = object->type == OBJECT_FORWARDER ? (struct forwarder *)object :
      ((struct forwarder_controller *)object)->forwarder;
  bool controller = object->type == OBJECT_FORWARDER_CONTROLLER && (rights & FORWARDER_RIGHTS);
  while (*budget) {
    --*budget;
    spin_lock(&forwarders_lock);
    size_t *count = object->type == OBJECT_FORWARDER ? &forwarder->holders : &forwarder->controllers;
    bool holder = object->type == OBJECT_FORWARDER && holder_keeps_target(forwarder, rights);
    bool counted = holder || controller;
    if (counted && *count == SIZE_MAX) {
      spin_unlock(&forwarders_lock);
      return false;
    }
    if (!holder || forwarder->target_owned ||
        !forwarder->target.object || lineage_cut(forwarder)) {
      if (counted) {
        ++*count;
      }
      spin_unlock(&forwarders_lock);
      return true;
    }
    /* A delivered OPEN may create a descendant after the original holder
     * closed. Restore logical client ownership from the admitted live target;
     * caretaker storage alone must neither retire nor resurrect a provider. */
    struct capability_grant snapshot = forwarder->target;
    bool pinned = object_retain(snapshot.object);
    spin_unlock(&forwarders_lock);
    if (!pinned) {
      return false;
    }
    struct capability_grant prospective = {0};
    enum capability_result result;
    if (snapshot.object->type == OBJECT_FORWARDER ||
        snapshot.object->type == OBJECT_FORWARDER_CONTROLLER) {
      bool retained = object_retain(snapshot.object);
      if (retained && !grant_retain_depth(snapshot.object, snapshot.rights, depth + 1, budget)) {
        object_release(snapshot.object);
        retained = false;
      }
      result = retained ? CAP_OK : CAP_LIMIT;
      if (retained) {
        prospective = snapshot;
      }
    } else {
      result = capability_grant_derive(snapshot.object, snapshot.rights,
          snapshot.transport, snapshot.restrictions, &prospective);
    }
    object_release(snapshot.object);
    if (result != CAP_OK) {
      return false;
    }
    spin_lock(&forwarders_lock);
    bool restored = false;
    if (!forwarder->target_owned && !lineage_cut(forwarder) &&
        forwarder->holders != SIZE_MAX &&
        forwarder->target.object == snapshot.object &&
        forwarder->target.rights == snapshot.rights &&
        forwarder->target.transport == snapshot.transport &&
        forwarder->target.restrictions == snapshot.restrictions) {
      forwarder->target_owned = true;
      ++forwarder->holders;
      prospective = (struct capability_grant){0};
      restored = true;
    }
    spin_unlock(&forwarders_lock);
    capability_grant_release(&prospective);
    if (restored) {
      return true;
    }
  }
  return false;
}

bool forwarder_grant_retain(struct kernel_object *object, uint64_t rights)
{
  size_t budget = FORWARDER_LINEAGE_MAX;
  return grant_retain_depth(object, rights, 0, &budget);
}

void forwarder_grant_release(struct kernel_object *object, uint64_t rights)
{
  struct forwarder *forwarder = object->type == OBJECT_FORWARDER ? (struct forwarder *)object :
      ((struct forwarder_controller *)object)->forwarder;
  if (object->type == OBJECT_FORWARDER && !holder_keeps_target(forwarder, rights)) {
    return;
  }
  struct capability_grant detached = {0};
  bool cut = false;
  spin_lock(&forwarders_lock);
  if (object->type == OBJECT_FORWARDER) {
    KASSERT(forwarder->holders);
    if (!--forwarder->holders && forwarder->target_owned) {
      detached = forwarder->target;
      forwarder->target_owned = false;
    }
  } else if (rights & FORWARDER_RIGHTS) {
    KASSERT(forwarder->controllers);
    cut = !--forwarder->controllers && forwarder->kind == FORWARDER_REVOCABLE;
  }
  spin_unlock(&forwarders_lock);
  capability_grant_release(&detached);
  if (cut) {
    cut_forwarder(forwarder);
  }
}

void forwarder_operation_init(struct forwarder_operation *operation)
{
  operation->next = NULL;
  operation->previous = NULL;
  operation->count = 0;
  operation->state = FORWARDER_PENDING;
  operation->wait = NULL;
  operation->notification_owner = NULL;
  operation->notification_next = NULL;
  operation->readiness_observer = false;
}

static enum call_status add_node(struct forwarder_operation *operation, struct forwarder *node)
{
  for (size_t i = 0; i < operation->count; ++i) {
    if (operation->nodes[i] == node) {
      return CALL_OK;
    }
  }
  if (operation->count == FORWARDER_LINEAGE_MAX || !object_retain(&node->object)) {
    return CALL_LIMIT;
  }
  operation->nodes[operation->count++] = node;
  return CALL_OK;
}

static void register_operation(struct forwarder_operation *operation)
{
  if (!operation->count || operation->previous) {
    return;
  }
  operation->next = operations;
  operation->previous = &operations;
  if (operations) {
    operations->previous = &operation->next;
  }
  operations = operation;
}

enum call_status forwarder_resolve(struct kernel_object *object, uint64_t rights,
    uint64_t transport, struct forwarder_operation *operation, struct capability_reference *target)
{
  *target = (struct capability_reference){0};
  KASSERT(operation);
  spin_lock(&forwarders_lock);
  enum call_status status = CALL_OK;
  uint64_t restrictions = 0;
  size_t hops = 0;
  while (object && object->type == OBJECT_FORWARDER) {
    struct forwarder *forwarder = (struct forwarder *)object;
    if (++hops > FORWARDER_LINEAGE_MAX) {
      status = CALL_LIMIT;
      break;
    }
    if (lineage_cut(forwarder)) {
      status = CALL_DENIED;
      break;
    }
    status = add_node(operation, forwarder);
    for (size_t i = 0; status == CALL_OK && i < forwarder->ancestor_count; ++i) {
      status = add_node(operation, forwarder->ancestors[i]);
    }
    if (status != CALL_OK) {
      break;
    }
    rights &= forwarder->target.rights;
    transport &= forwarder->target.transport;
    restrictions |= forwarder->target.restrictions;
    object = forwarder->target.object;
  }
  register_operation(operation);
  if (status == CALL_OK) {
    if (!object) {
      status = CALL_NOT_FOUND;
    } else if (!object_retain(object)) {
      status = CALL_LIMIT;
    } else {
      *target = (struct capability_reference){.object = object, .rights = rights,
        .transport = transport, .restrictions = restrictions};
    }
  }
  spin_unlock(&forwarders_lock);
  return status;
}

enum capability_result forwarder_snapshot(struct kernel_object *object,
    struct kernel_object **target, uint64_t *restrictions)
{
  *target = NULL;
  uint64_t lineage = 0;
  spin_lock(&forwarders_lock);
  enum capability_result result = CAP_OK;
  size_t depth = 0;
  while (object && object->type == OBJECT_FORWARDER) {
    struct forwarder *forwarder = (struct forwarder *)object;
    if (++depth > FORWARDER_LINEAGE_MAX) {
      result = CAP_LIMIT;
      break;
    }
    if (lineage_cut(forwarder)) {
      result = CAP_DENIED;
      break;
    }
    lineage |= forwarder->target.restrictions;
    object = forwarder->target.object;
  }
  if (result == CAP_OK && object) {
    if (object_retain(object)) {
      *target = object;
    } else {
      result = CAP_LIMIT;
    }
  }
  spin_unlock(&forwarders_lock);
  if (restrictions) {
    *restrictions = lineage;
  }
  return result;
}

enum call_status forwarder_operation_clone(struct forwarder_operation *destination,
    const struct forwarder_operation *source)
{
  forwarder_operation_init(destination);
  if (!source || !source->count) {
    return CALL_OK;
  }
  spin_lock(&forwarders_lock);
  enum call_status status = source->state == FORWARDER_DENIED || operation_cut(source) ? CALL_DENIED : CALL_OK;
  for (size_t i = 0; status == CALL_OK && i < source->count; ++i) {
    status = add_node(destination, source->nodes[i]);
  }
  register_operation(destination);
  spin_unlock(&forwarders_lock);
  if (status != CALL_OK) {
    forwarder_operation_release(destination);
  }
  return status;
}

void forwarder_operation_release(struct forwarder_operation *operation)
{
  if (!operation || (!operation->count && !operation->readiness_observer)) {
    return;
  }
  spin_lock(&forwarders_lock);
  if (operation->previous) {
    *operation->previous = operation->next;
    if (operation->next) {
      operation->next->previous = operation->previous;
    }
  }
  operation->previous = NULL;
  operation->next = NULL;
  operation->wait = NULL;
  operation->notification_owner = NULL;
  operation->readiness_observer = false;
  spin_unlock(&forwarders_lock);
  for (size_t i = 0; i < operation->count; ++i) {
    object_release(&operation->nodes[i]->object);
  }
  forwarder_operation_init(operation);
}

void forwarder_operation_readiness(struct forwarder_operation *operation, bool enabled)
{
  KASSERT(operation);
  spin_lock(&forwarders_lock);
  KASSERT(!enabled || operation->state != FORWARDER_STARTED);
  bool notify = enabled && !operation->readiness_observer &&
      operation->state == FORWARDER_DENIED;
  operation->readiness_observer = enabled;
  if (notify) {
    readiness_registration_revoked(operation);
  }
  spin_unlock(&forwarders_lock);
}

enum call_status forwarder_mapping_prepare(struct forwarder_mapping *mapping,
    const struct forwarder_operation *source, struct shared_view *owner)
{
  KASSERT(!cpu_interrupts_enabled() && mapping && owner);
  *mapping = (struct forwarder_mapping){.owner = owner};
  atomic_init(&mapping->revoked, false);
  if (!source || !source->count) {
    return CALL_OK;
  }
  spin_lock(&forwarders_lock);
  bool denied = source->state == FORWARDER_DENIED || operation_cut(source);
  spin_unlock(&forwarders_lock);
  if (denied) {
    return CALL_DENIED;
  }
  /* The retained operation keeps each source node live across logical grant
   * callbacks. Match derived grants' zero-right ancestor holder semantics. */
  for (size_t i = 0; i < source->count; ++i) {
    struct forwarder *node = source->nodes[i];
    if (!object_grant_retain(&node->object, 0)) {
      forwarder_mapping_complete(mapping, false);
      return CALL_LIMIT;
    }
    mapping->nodes[mapping->count++] = node;
  }
  return CALL_OK;
}

enum call_status forwarder_mapping_enroll(struct forwarder_mapping *mapping)
{
  KASSERT(!cpu_interrupts_enabled() && mapping && mapping->owner);
  spin_lock(&forwarders_lock);
  KASSERT(!mapping->enrolled && !mapping->previous);
  enum call_status status = mapping_cut(mapping) ? CALL_DENIED : CALL_OK;
  for (size_t i = 0; status == CALL_OK && i < mapping->count; ++i) {
    if (mapping->nodes[i]->mapping_pending == SIZE_MAX) {
      status = CALL_LIMIT;
    }
  }
  if (status == CALL_OK) {
    mapping->enrolled = true;
    if (mapping->count) {
      mapping->next = mappings;
      mapping->previous = &mappings;
      if (mappings) {
        mappings->previous = &mapping->next;
      }
      mappings = mapping;
      for (size_t i = 0; i < mapping->count; ++i) {
        ++mapping->nodes[i]->mapping_pending;
        mapping_notify(mapping->nodes[i]);
      }
    }
  }
  spin_unlock(&forwarders_lock);
  return status;
}

bool forwarder_mapping_complete(struct forwarder_mapping *mapping, bool uncertain)
{
  KASSERT(!cpu_interrupts_enabled() && mapping);
  spin_lock(&forwarders_lock);
  if (mapping->enrolled) {
    uncertain |= shared_view_withdrawal_uncertain(mapping->owner);
    if (mapping->previous) {
      *mapping->previous = mapping->next;
      if (mapping->next) {
        mapping->next->previous = mapping->previous;
      }
    }
    if (uncertain) {
      mapping_mark_unknown(mapping);
    }
    for (size_t i = 0; i < mapping->count; ++i) {
      struct forwarder *node = mapping->nodes[i];
      KASSERT(node->mapping_pending);
      --node->mapping_pending;
      mapping_notify(node);
    }
  }
  mapping->enrolled = false;
  mapping->previous = NULL;
  mapping->next = NULL;
  size_t count = mapping->count;
  mapping->count = 0;
  mapping->owner = NULL;
  spin_unlock(&forwarders_lock);
  for (size_t i = 0; i < count; ++i) {
    struct forwarder *node = mapping->nodes[i];
    mapping->nodes[i] = NULL;
    object_grant_release(&node->object, 0);
  }
  return uncertain;
}

bool forwarder_mapping_revoked(const struct forwarder_mapping *mapping)
{
  return atomic_load_explicit(&mapping->revoked, memory_order_acquire);
}

static struct forwarder *mapping_controller(struct kernel_object *controller)
{
  KASSERT(!cpu_interrupts_enabled() && controller &&
      controller->type == OBJECT_FORWARDER_CONTROLLER);
  return ((struct forwarder_controller *)controller)->forwarder;
}

static enum call_status mapping_status(const struct forwarder *node)
{
  return node->mapping_unknown ? CALL_OUTCOME_UNKNOWN :
      node->mapping_pending ? CALL_BUSY : CALL_OK;
}

enum call_status forwarder_mapping_status(struct kernel_object *controller)
{
  struct forwarder *node = mapping_controller(controller);
  spin_lock(&forwarders_lock);
  enum call_status status = mapping_status(node);
  spin_unlock(&forwarders_lock);
  return status;
}

bool forwarder_mapping_target_pending(struct kernel_object *controller,
    struct process *process)
{
  struct forwarder *node = mapping_controller(controller);
  spin_lock(&forwarders_lock);
  bool pending = false;
  for (struct forwarder_mapping *mapping = mappings; mapping; mapping = mapping->next) {
    if (mapping_contains(mapping, node) && shared_view_targets(mapping->owner, process)) {
      pending = true;
      break;
    }
  }
  spin_unlock(&forwarders_lock);
  return pending;
}

void forwarder_mapping_timeout(struct kernel_object *controller)
{
  struct forwarder *node = mapping_controller(controller);
  spin_lock(&forwarders_lock);
  for (struct forwarder_mapping *mapping = mappings; mapping; mapping = mapping->next) {
    if (mapping_contains(mapping, node) && !mapping->uncertain) {
      mapping->uncertain = true;
      atomic_store_explicit(&mapping->revoked, true, memory_order_release);
      mapping_mark_unknown(mapping);
      shared_view_request_withdrawal(mapping->owner, true);
    }
  }
  spin_unlock(&forwarders_lock);
}

enum call_status forwarder_mapping_wait(struct kernel_object *controller,
    struct task_wait_link *link)
{
  struct forwarder *node = mapping_controller(controller);
  KASSERT(link && link->wait && !link->next);
  spin_lock(&forwarders_lock);
  enum call_status status = mapping_status(node);
  if (status == CALL_BUSY) {
    link->next = node->mapping_waiters;
    node->mapping_waiters = link;
  } else {
    struct task_wait *wait = link->wait;
    link->wait = NULL;
    task_wait_wake(wait);
  }
  spin_unlock(&forwarders_lock);
  return status;
}

void forwarder_mapping_unwait(struct kernel_object *controller,
    struct task_wait_link *link)
{
  struct forwarder *node = mapping_controller(controller);
  KASSERT(link);
  spin_lock(&forwarders_lock);
  if (link->wait) {
    struct task_wait_link **cursor = &node->mapping_waiters;
    while (*cursor && *cursor != link) {
      cursor = &(*cursor)->next;
    }
    KASSERT(*cursor == link);
    *cursor = link->next;
    link->next = NULL;
    link->wait = NULL;
  }
  spin_unlock(&forwarders_lock);
}

bool forwarder_operation_start(struct forwarder_operation *operation)
{
  return forwarder_operation_start_pair(operation, NULL);
}

bool forwarder_operation_start_pair(struct forwarder_operation *first,
    struct forwarder_operation *second)
{
  struct forwarder_operation *operation = first;
  if (second && second->count && (!first || !first->count)) {
    operation = second;
    second = NULL;
  }
  if (!operation || !operation->count) {
    return true;
  }
  spin_lock(&forwarders_lock);
  if (operation->state == FORWARDER_PENDING) {
    if (operation_cut(operation)) {
      operation->state = FORWARDER_DENIED;
    }
  }
  if (second && second->count && second->state == FORWARDER_PENDING && operation_cut(second)) {
    second->state = FORWARDER_DENIED;
  }
  bool started = operation->state != FORWARDER_DENIED &&
      (!second || !second->count || second->state != FORWARDER_DENIED);
  if (started) {
    operation->state = FORWARDER_STARTED;
    if (second && second->count) {
      second->state = FORWARDER_STARTED;
    }
  }
  spin_unlock(&forwarders_lock);
  return started;
}

bool forwarder_operation_denied(const struct forwarder_operation *operation)
{
  if (!operation || !operation->count) {
    return false;
  }
  spin_lock(&forwarders_lock);
  bool denied = operation->state == FORWARDER_DENIED;
  spin_unlock(&forwarders_lock);
  return denied;
}

void forwarder_operation_wait(struct forwarder_operation *operation, struct task_wait *wait)
{
  if (!operation || !operation->count) {
    return;
  }
  spin_lock(&forwarders_lock);
  KASSERT(!operation->wait || operation->wait == wait);
  operation->wait = wait;
  if (operation->state == FORWARDER_DENIED) {
    task_wait_wake(wait);
  }
  spin_unlock(&forwarders_lock);
}

void forwarder_operation_unwait(struct forwarder_operation *operation, struct task_wait *wait)
{
  if (!operation || !operation->count) {
    return;
  }
  spin_lock(&forwarders_lock);
  if (operation->wait == wait) {
    operation->wait = NULL;
  }
  spin_unlock(&forwarders_lock);
}

void forwarder_operation_delivery_owner(struct forwarder_operation *operation,
    struct kernel_object *receipt)
{
  if (!operation || !operation->count) {
    return;
  }
  spin_lock(&forwarders_lock);
  operation->notification_owner = receipt;
  spin_unlock(&forwarders_lock);
}

/* Forwarder guard held. Target/controller storage and immutable ancestry remain
 * owned throughout the walk. Grant containers cannot become mutable targets:
 * their members could otherwise retain this slot through derived proxies. */
static enum call_status target_admission_status(struct forwarder *root,
    struct kernel_object *target)
{
  struct kernel_object *objects[FORWARDER_TARGET_GRAPH_MAX];
  size_t count = target ? 1 : 0;
  objects[0] = target;
  for (size_t index = 0; index < count; ++index) {
    struct kernel_object *object = objects[index];
    if (root && object == &root->object) {
      return CALL_BAD_REQUEST;
    }
    if (object->type == OBJECT_ENVIRONMENT_SNAPSHOT ||
        object->type == OBJECT_COMMAND_LIST ||
        object->type == OBJECT_OUTPUT_COLLECTOR || object->type == OBJECT_WIFI_SCAN ||
        object->type == OBJECT_WIFI_CONNECT) {
      return CALL_WRONG_TYPE;
    }
    struct forwarder *forwarder;
    if (object->type == OBJECT_FORWARDER_CONTROLLER) {
      forwarder = ((struct forwarder_controller *)object)->forwarder;
    } else if (object->type == OBJECT_FORWARDER) {
      forwarder = (struct forwarder *)object;
    } else {
      continue;
    }
    bool controller = object->type == OBJECT_FORWARDER_CONTROLLER;
    size_t children = controller ? 1 : forwarder->ancestor_count + 1;
    for (size_t child = 0; child < children; ++child) {
      struct kernel_object *next;
      if (controller) {
        next = &forwarder->object;
      } else if (child) {
        next = &forwarder->ancestors[child - 1]->object;
      } else {
        next = forwarder->target.object;
      }
      if (!next) {
        continue;
      }
      size_t seen = 0;
      while (seen < count && objects[seen] != next) {
        ++seen;
      }
      if (seen < count) {
        continue;
      }
      if (count == sizeof(objects) / sizeof(objects[0])) {
        return CALL_LIMIT;
      }
      objects[count++] = next;
    }
  }
  return CALL_OK;
}

static enum call_status allocate_forwarder(struct capability_grant *target,
    uint64_t kind, const struct forwarder_operation *lineage, struct forwarder **out)
{
  KASSERT(!cpu_interrupts_enabled() && target->object);
  if (!wifi_grant_delegatable(target->object, target->rights)) {
    return CALL_DENIED;
  }
  struct forwarder *forwarder = kmalloc(sizeof(*forwarder));
  if (!forwarder) {
    return CALL_NO_MEMORY;
  }
  memset(forwarder, 0, sizeof(*forwarder));
  forwarder->type = object_effective_type(target->object);
  forwarder->protocol = object_protocol(target->object);
  forwarder->rights = target->rights;
  forwarder->transport = target->transport;
  forwarder->kind = kind;
  object_init(&forwarder->object, OBJECT_FORWARDER, destroy_forwarder);
  if (!object_caretaker_retain(target->object)) {
    kfree(forwarder);
    return CALL_LIMIT;
  }
  enum call_status status = CALL_OK;
  struct forwarder_operation ancestry;
  bool already_cut = false;
  forwarder_operation_init(&ancestry);
  spin_lock(&forwarders_lock);
  if (lineage) {
    already_cut = operation_cut(lineage);
    for (size_t i = 0; status == CALL_OK && i < lineage->count; ++i) {
      status = add_node(&ancestry, lineage->nodes[i]);
    }
  }
  if (target->object->type == OBJECT_FORWARDER) {
    struct forwarder *parent = (struct forwarder *)target->object;
    if (lineage_cut(parent) && kind != 0) {
      status = CALL_DENIED;
    }
    if (status == CALL_OK && kind != FORWARDER_SLOT) {
      status = add_node(&ancestry, parent);
    }
    for (size_t i = 0; kind != FORWARDER_SLOT && status == CALL_OK &&
        i < parent->ancestor_count; ++i) {
      status = add_node(&ancestry, parent->ancestors[i]);
    }
  }
  spin_unlock(&forwarders_lock);
  size_t logical_count = 0;
  for (; status == CALL_OK && logical_count < ancestry.count; ++logical_count) {
    struct kernel_object *ancestor = &ancestry.nodes[logical_count]->object;
    if (!object_grant_retain(ancestor, 0)) {
      status = CALL_LIMIT;
      break;
    }
    object_release(ancestor);
  }
  spin_lock(&forwarders_lock);
  already_cut = (lineage && operation_cut(lineage)) ||
      (target->object->type == OBJECT_FORWARDER && lineage_cut((struct forwarder *)target->object));
  if (status == CALL_OK && already_cut && kind) {
    status = CALL_DENIED;
  }
  if (status == CALL_OK && kind) {
    status = target_admission_status(NULL, target->object);
  }
  if (status == CALL_OK && !target_depth_valid(forwarder, target->object, &forwarder->object)) {
    status = CALL_LIMIT;
  }
  if (status == CALL_OK) {
    /* Descendants keep the provider's logical client alive across namespace
     * replacement. This owns holder authority, never controller authority;
     * explicit CUT still detaches every target before any callback. */
    forwarder->ancestor_count = ancestry.count;
    memcpy(forwarder->ancestors, ancestry.nodes, ancestry.count * sizeof(ancestry.nodes[0]));
    ancestry.count = 0;
    forwarder->target = *target;
    forwarder->target_owned = true;
    *target = (struct capability_grant){0};
    forwarder->reserved = true;
    KASSERT(object_retain(&forwarder->object));
    forwarder->next = forwarders;
    forwarder->previous = &forwarders;
    if (forwarders) {
      forwarders->previous = &forwarder->next;
    }
    forwarders = forwarder;
    *out = forwarder;
  }
  spin_unlock(&forwarders_lock);
  if (status != CALL_OK) {
    for (size_t i = 0; i < ancestry.count; ++i) {
      struct kernel_object *ancestor = &ancestry.nodes[i]->object;
      if (i < logical_count) {
        object_grant_release(ancestor, 0);
      } else {
        object_release(ancestor);
      }
    }
    object_caretaker_release(target->object);
    kfree(forwarder);
  } else if (already_cut) {
    cut_forwarder(forwarder);
  }
  return status;
}

enum call_status forwarder_create_owned(struct capability_grant *target, uint64_t kind,
    struct capability_grant *holder, struct capability_grant *control)
{
  *holder = *control = (struct capability_grant){0};
  spin_lock(&forwarders_lock);
  enum call_status status = target_admission_status(NULL, target->object);
  spin_unlock(&forwarders_lock);
  if (status != CALL_OK) {
    return status;
  }
  struct forwarder_controller *controller = kmalloc(sizeof(*controller));
  if (!controller) {
    return CALL_NO_MEMORY;
  }
  uint64_t restrictions = target->restrictions;
  struct forwarder *forwarder;
  status = allocate_forwarder(target, kind, NULL, &forwarder);
  if (status != CALL_OK) {
    kfree(controller);
    return status;
  }
  controller->forwarder = forwarder;
  object_init(&controller->object, OBJECT_FORWARDER_CONTROLLER, destroy_controller);
  KASSERT(object_retain(&forwarder->object));
  enum capability_result result = capability_grant_retain(&controller->object,
      FORWARDER_RIGHT_CUT | (kind == FORWARDER_SLOT ? FORWARDER_RIGHT_RETARGET : 0), 0, control);
  if (result == CAP_OK) {
    result = capability_grant_derive(&forwarder->object, forwarder->rights,
        forwarder->transport, restrictions, holder);
  }
  object_release(&controller->object);
  object_release(&forwarder->object);
  if (result != CAP_OK) {
    capability_grant_release(holder);
    capability_grant_release(control);
  }
  return call_status(result);
}

size_t forwarder_derived_storage_size(const struct forwarder_operation *lineage)
{
  return lineage && lineage->count ? sizeof(struct forwarder) : 0;
}

enum capability_result forwarder_derive_grant(struct capability_grant *grant,
    const struct forwarder_operation *lineage)
{
  if (!lineage || !lineage->count) {
    return CAP_OK;
  }
  uint64_t restrictions = grant->restrictions;
  struct forwarder *forwarder;
  enum call_status status = allocate_forwarder(grant, 0, lineage, &forwarder);
  if (status != CALL_OK) {
    return cap_status(status);
  }
  enum capability_result result = capability_grant_derive(&forwarder->object,
      forwarder->rights, forwarder->transport, restrictions, grant);
  object_release(&forwarder->object);
  return result;
}

enum capability_result forwarder_request_derivation(struct capability_grant *grant)
{
  return forwarder_request_derivation_from(grant, task_forwarder_operation());
}

enum capability_result forwarder_request_derivation_from(struct capability_grant *grant,
    const struct forwarder_operation *lineage)
{
  if (!lineage || !lineage->count) {
    return CAP_OK;
  }
  if (arch_cpu_index() == 0 && !task_current_user()) {
    return forwarder_derive_grant(grant, lineage);
  }
  struct forwarder_request *request = (struct forwarder_request *)bsp_request_prepare(BSP_SERVICE_FORWARDER);
  request->derive = true;
  request->lineage = lineage;
  request->target = *grant;
  *grant = (struct capability_grant){0};
  bsp_request_submit_and_wait(&request->request);
  enum capability_result result = cap_status(request->result);
  *grant = request->target;
  request->target = (struct capability_grant){0};
  bsp_request_release(&request->request);
  return result;
}

void forwarder_execute(struct forwarder_request *request)
{
  if (request->derive) {
    request->result = call_status(forwarder_derive_grant(&request->target, request->lineage));
    return;
  }
  struct capability_grant grants[2];
  request->result = forwarder_create_owned(&request->target, request->kind, &grants[0], &grants[1]);
  if (request->result == CALL_OK) {
    enum capability_result result = capability_validate_grants(request->reservation.table, grants, 2);
    request->result = call_status(result);
    if (result == CAP_OK) {
      handle_t handles[2];
      capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
      request->reply = (struct forwarder_reply){handles[0], handles[1]};
    }
    capability_grant_release(&grants[0]);
    capability_grant_release(&grants[1]);
  }
  capability_grant_release(&request->target);
  capability_reservation_release(&request->reservation, request->slots);
}

struct syscall_result forwarder_create_call(uintptr_t input_address, uintptr_t reply_address,
    uint64_t capacity)
{
  if (capacity < sizeof(struct forwarder_reply)) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  struct forwarder_create input;
  if (!copy_from_user(&input, input_address, sizeof(input)) ||
      !user_buffer_check(reply_address, sizeof(struct forwarder_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if ((input.kind != FORWARDER_REVOCABLE && input.kind != FORWARDER_SLOT) ||
      input.flags & ~FORWARDER_CREATE_SAME_RIGHTS ||
      ((input.flags & FORWARDER_CREATE_SAME_RIGHTS) && (input.rights || input.transport))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_grant target;
  enum capability_result result = input.flags & FORWARDER_CREATE_SAME_RIGHTS ?
      capability_grant_acquire_same(&process_current()->capabilities, input.target, &target) :
      capability_grant_acquire(&process_current()->capabilities, input.target, input.rights, input.transport, &target);
  if (result != CAP_OK) {
    return (struct syscall_result){call_status(result), 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  result = capability_request_reservation(2, &reservation, slots);
  if (result != CAP_OK) {
    capability_grant_release(&target);
    return (struct syscall_result){call_status(result), 0};
  }
  if (task_stop_requested()) {
    capability_grant_release(&target);
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct forwarder_request *request = (struct forwarder_request *)bsp_request_prepare(BSP_SERVICE_FORWARDER);
  request->target = target;
  request->kind = input.kind;
  request->reservation = reservation;
  memcpy(request->slots, slots, sizeof(slots));
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  struct forwarder_reply reply = request->reply;
  bsp_request_release(&request->request);
  if (status == CALL_OK) {
    handle_t handles[2] = {reply.holder, reply.controller};
    if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), handles, 2)) {
      status = CALL_BAD_BUFFER;
    }
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}

static enum call_status match_slot_holder(struct forwarder *slot, handle_t handle)
{
  struct capability_reference holder;
  enum capability_result acquired = capability_acquire_metadata(
      &process_current()->capabilities, handle, &holder);
  if (acquired != CAP_OK) {
    return call_status(acquired);
  }
  if (!task_operation_start()) {
    capability_release(&holder);
    return CALL_DENIED;
  }
  spin_lock(&forwarders_lock);
  enum call_status status = lineage_cut(slot) ? CALL_DENIED : CALL_WRONG_TYPE;
  struct kernel_object *candidate = holder.object;
  for (size_t depth = 0; status != CALL_DENIED && candidate &&
      candidate->type == OBJECT_FORWARDER; ++depth) {
    if (depth == FORWARDER_LINEAGE_MAX) {
      status = CALL_LIMIT;
      break;
    }
    struct forwarder *proxy = (struct forwarder *)candidate;
    if (lineage_cut(proxy)) {
      status = CALL_DENIED;
      break;
    }
    if (proxy == slot && slot->kind == FORWARDER_SLOT) {
      status = CALL_OK;
      break;
    }
    /* Immutable wrappers preserve the underlying slot association. A distinct
     * mutable slot must not match merely because it currently targets this one. */
    if (proxy->kind == FORWARDER_SLOT) {
      break;
    }
    candidate = proxy->target.object;
  }
  spin_unlock(&forwarders_lock);
  capability_release(&holder);
  return status;
}

struct syscall_result forwarder_controller_call(struct kernel_object *object,
    uint64_t rights, uint64_t operation, uintptr_t address, size_t size)
{
  struct forwarder *forwarder = ((struct forwarder_controller *)object)->forwarder;
  if (operation == FORWARDER_CUT) {
    if (size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!(rights & FORWARDER_RIGHT_CUT)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    uint64_t deadline = task_deadline_after_ms(1000);
    cut_forwarder(forwarder);
    enum call_status status = forwarder_mapping_status(object);
    if (status == CALL_OK) {
      return (struct syscall_result){CALL_OK, 0};
    }
    /* A self-targeting revoker must leave its root before withdrawal can drain.
     * No user reply loan is granted while its selected aliases remain live. */
    task_vm_kernel_enter();
    while (status == CALL_BUSY && !task_stop_requested()) {
      if (task_deadline_expired(deadline)) {
        break;
      }
      struct task_wait_link *link = task_wait_link_prepare();
      struct task_wait *wait = link->wait;
      status = forwarder_mapping_wait(object, link);
      if (status == CALL_BUSY) {
        task_wait_sleep_until_interruptible(wait, deadline);
      }
      forwarder_mapping_unwait(object, link);
      status = forwarder_mapping_status(object);
    }
    if (status == CALL_BUSY) {
      forwarder_mapping_timeout(object);
      status = CALL_OUTCOME_UNKNOWN;
    }
    if (status == CALL_OUTCOME_UNKNOWN &&
        forwarder_mapping_target_pending(object, process_current())) {
      process_request_stop(process_current());
    }
    if (!task_stop_requested()) {
      task_vm_kernel_leave();
    }
    /* A stopped self-revoker returns through kernel-only grant cleanup to
     * task_syscall_leave(), which terminates it without reactivating its root. */
    return (struct syscall_result){status, 0};
  }
  if (operation != FORWARDER_RETARGET && operation != FORWARDER_MATCH) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & FORWARDER_RIGHT_RETARGET)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  handle_t target_handle;
  if (size != sizeof(target_handle)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&target_handle, address, sizeof(target_handle))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == FORWARDER_MATCH) {
    return (struct syscall_result){match_slot_holder(forwarder, target_handle), 0};
  }
  struct capability_grant target = {0};
  if (target_handle) {
    enum capability_result result = capability_grant_acquire_same(&process_current()->capabilities,
        target_handle, &target);
    if (result != CAP_OK) {
      return (struct syscall_result){call_status(result), 0};
    }
    bool exported = object_effective_type(target.object) == OBJECT_ENDPOINT_EXPORT;
    bool slot_exported = forwarder->type == OBJECT_ENDPOINT_EXPORT;
    if (exported != slot_exported || object_protocol(target.object) != forwarder->protocol) {
      capability_grant_release(&target);
      return (struct syscall_result){CALL_WRONG_TYPE, 0};
    }
    if ((target.rights & forwarder->rights) != forwarder->rights ||
        (target.transport & forwarder->transport) != forwarder->transport) {
      capability_grant_release(&target);
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (!object_caretaker_retain(target.object)) {
      capability_grant_release(&target);
      return (struct syscall_result){CALL_LIMIT, 0};
    }
  }
  struct kernel_object *target_storage = target.object;
  if (!task_operation_start()) {
    capability_grant_release(&target);
    if (target_storage) {
      object_caretaker_release(target_storage);
    }
    return (struct syscall_result){CALL_DENIED, 0};
  }
  spin_lock(&forwarders_lock);
  enum call_status status = lineage_cut(forwarder) ? CALL_DENIED :
      target_admission_status(forwarder, target.object);
  if (status == CALL_OK && !retarget_depth_valid(forwarder, target.object)) {
    status = CALL_LIMIT;
  }
  struct capability_grant previous = {0};
  bool previous_owned = false, retain_target = false;
  if (status == CALL_OK) {
    previous = forwarder->target;
    previous_owned = forwarder->target_owned;
    forwarder->target = target;
    retain_target = forwarder->holders != 0;
    forwarder->target_owned = target.object && retain_target;
  }
  spin_unlock(&forwarders_lock);
  if (status != CALL_OK || !retain_target) {
    capability_grant_release(&target);
  }
  if (status != CALL_OK && target_storage) {
    object_caretaker_release(target_storage);
  }
  if (previous.object) {
    if (previous_owned) {
      object_grant_release(previous.object, previous.rights);
    }
    object_caretaker_release(previous.object);
  }
  return (struct syscall_result){status, 0};
}

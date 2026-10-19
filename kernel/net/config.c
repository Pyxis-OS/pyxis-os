#include <arch/clock.h>
#include <kernel/object/forwarder.h>
#include <arch/cpu.h>
#include <kernel/net/config.h>
#include <kernel/net/driver.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/registry.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/string.h>
#include <stdatomic.h>

#define CONFIG_PENDING_LIMIT 8

enum config_state { CONFIG_FREE, CONFIG_QUEUED, CONFIG_RUNNING, CONFIG_DONE };
struct config_call {
  enum config_state state;
  struct net_control control;
  enum call_status status;
  struct task_wait *wait;
  struct forwarder_operation *context;
  bool cancelled;
};

static struct config_call pending[CONFIG_PENDING_LIMIT];
static atomic_bool pending_locked;

static void lock_pending(void)
{
  while (atomic_exchange_explicit(&pending_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_pending(void)
{
  atomic_store_explicit(&pending_locked, false, memory_order_release);
}

enum call_status net_config_exchange(struct net_control *control)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }
  lock_pending();
  struct config_call *call = NULL;
  for (size_t i = 0; i < CONFIG_PENDING_LIMIT; ++i) {
    if (pending[i].state == CONFIG_FREE) {
      call = &pending[i];
      break;
    }
  }
  if (!call) {
    unlock_pending();
    return CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct config_call){
    .state = CONFIG_QUEUED, .control = *control, .wait = wait,
    .context = task_forwarder_operation(),
  };
  unlock_pending();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_pending();
  while (call->state != CONFIG_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_pending();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_pending();
  }
  KASSERT(!call->wait);
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  *control = call->control;
  call->state = CONFIG_FREE;
  unlock_pending();
  return status;
}

bool net_control_valid_server(uint32_t address)
{
  return (address >> 24) && address < UINT32_C(0xe0000000);
}

void net_interface_snapshot(const struct net_interface *interface,
    struct net_interface_reply *reply)
{
  *reply = (struct net_interface_reply){.flags = NET_INTERFACE_INVENTORY_COMPLETE};
  if (!interface) {
    return;
  }
  reply->interface_id = interface->id;
  reply->interface_class = interface->interface_class;
  reply->address_epoch = interface->ipv4.address_epoch;
  reply->route_revision = interface->route_revision;
  reply->mtu = interface->mtu;
  reply->flags |= NET_INTERFACE_DEFINED;
  memcpy(reply->name, interface->name, strlen(interface->name));
  if (interface->ipv4.address) {
    reply->flags |= NET_INTERFACE_ADDRESS_VALID;
    reply->address = interface->ipv4.address;
    reply->prefix = interface->ipv4.prefix;
    reply->gateway = interface->ipv4.gateway;
  }
  if (interface->dns_server) {
    reply->flags |= NET_INTERFACE_DNS_VALID;
    reply->dhcp_dns_server = interface->dns_server;
  }
  if (interface->dhcp_report_valid) {
    reply->flags |= NET_INTERFACE_DHCP_REPORT_VALID;
    reply->dhcp_phase = interface->dhcp_phase;
    reply->dhcp_report_time_ns = interface->dhcp_report_time_ns;
  }
  net_driver_interface_snapshot(interface, reply);
}

static enum call_status bind_interfaces(struct net_control *control)
{
  for (size_t i = 0; i < control->count; ++i) {
    struct net_config_entry *entry = &control->entries[i];
    entry->interface = net_interface_for_id(entry->binding.interface_id);
    if (!entry->interface) {
      return CALL_NOT_FOUND;
    }
    for (size_t j = 0; j < i; ++j) {
      if (control->entries[j].interface == entry->interface) {
        return CALL_BAD_REQUEST;
      }
    }
    enum call_status status = net_driver_resolve(entry->interface,
        &entry->binding.selector, &entry->controller);
    if (status != CALL_OK) {
      return status;
    }
    for (size_t j = 0; j < i; ++j) {
      if (net_driver_same_controller(control->entries[j].controller, entry->controller)) {
        return CALL_BUSY;
      }
    }
    entry->activate = entry->interface->driver.kind == NET_DRIVER_KIND_NONE;
  }
  /* Publish the whole validated batch before any device activation. Activation
   * failure preserves these retained associations and never chooses a fallback. */
  for (size_t i = 0; i < control->count; ++i) {
    if (control->entries[i].activate) {
      control->entries[i].interface->driver = control->entries[i].controller;
    }
  }
  const struct net_interface *selected_log = net_policy_log_interface();
  net_driver_panic_select(selected_log && selected_log->driver.kind != NET_DRIVER_KIND_NONE ?
      selected_log : NULL);
  for (size_t i = 0; i < control->count; ++i) {
    if (control->entries[i].activate) {
      net_driver_activate(control->entries[i].interface);
    }
  }
  return CALL_OK;
}

static enum call_status interface_configure(struct net_control *control)
{
  struct net_interface *interface = control->interface;
  switch (control->operation) {
  case NET_INTERFACE_QUERY:
    net_interface_snapshot(interface, &control->interface_reply);
    return CALL_OK;
  case NET_INTERFACE_REPORT_DHCP:
    if (control->report.reserved || control->report.phase > NET_DHCP_FAILED) {
      return CALL_BAD_REQUEST;
    }
    if (control->report.address_epoch != interface->ipv4.address_epoch) {
      return CALL_UNAVAILABLE;
    }
    interface->dhcp_phase = control->report.phase;
    interface->dhcp_report_time_ns = arch_monotonic_ns();
    interface->dhcp_report_valid = true;
    return CALL_OK;
  case NET_INTERFACE_REPLACE: {
    if (control->replace.dns_server && !net_control_valid_server(control->replace.dns_server)) {
      return CALL_BAD_REQUEST;
    }
    if (interface->route_revision == UINT64_MAX ||
        (interface->ipv4.address != control->replace.address &&
         interface->ipv4.address_epoch == UINT64_MAX)) {
      return CALL_LIMIT;
    }
    const struct net_interface *owner = net_interface_for_address(control->replace.address);
    if (owner && owner != interface) {
      return CALL_ALREADY_EXISTS;
    }
    enum net_result result = net_ipv4_configure(interface, control->replace.address,
        control->replace.prefix, control->replace.gateway);
    if (result == NET_INVALID) {
      return CALL_BAD_REQUEST;
    }
    if (result != NET_OK) {
      return CALL_UNAVAILABLE;
    }
    interface->dns_server = control->replace.dns_server;
    return CALL_OK;
  }
  case NET_INTERFACE_CLEAR:
    if (interface->route_revision == UINT64_MAX ||
        (interface->ipv4.address && interface->ipv4.address_epoch == UINT64_MAX)) {
      return CALL_LIMIT;
    }
    net_ipv4_clear(interface);
    interface->dns_server = 0;
    return CALL_OK;
  default: return CALL_BAD_OPERATION;
  }
}

static enum call_status configure(struct net_control *control)
{
  if (control->protocol == PROTOCOL_NET_INTERFACE) {
    return interface_configure(control);
  }
  if (control->protocol == PROTOCOL_NET_REGISTRY) {
    switch (control->operation) {
    case NET_REGISTRY_NEXT_CONTROLLER:
      net_driver_next_controller(control->after_id, &control->controller_reply);
      return CALL_OK;
    case NET_REGISTRY_NEXT_INTERFACE: {
      const struct net_interface *selected = NULL;
      for (const struct net_interface *interface = net_interface_first(); interface;
          interface = interface->registry_next) {
        if (interface->id > control->after_id && (!selected || interface->id < selected->id)) {
          selected = interface;
        }
      }
      net_interface_snapshot(selected, &control->interface_reply);
      return CALL_OK;
    }
    case NET_REGISTRY_LOOKUP:
      control->interface = net_interface_for_id(control->after_id);
      return control->interface ? CALL_OK : CALL_NOT_FOUND;
    case NET_REGISTRY_POLICY:
      net_policy_snapshot(&control->policy_reply);
      return CALL_OK;
    default: return CALL_BAD_OPERATION;
    }
  }
  switch (control->operation) {
  case NET_POLICY_DEFINE: return net_registry_define(control);
  case NET_POLICY_BIND: return bind_interfaces(control);
  case NET_POLICY_REPLACE: return net_policy_replace(&control->policy);
  case NET_POLICY_INTERFACE:
    control->interface = net_interface_for_id(control->after_id);
    if (!control->interface) {
      return CALL_NOT_FOUND;
    }
    if (control->interface == &net_loopback && (control->rights & NET_INTERFACE_RIGHT_WRITE)) {
      return CALL_DENIED;
    }
    return CALL_OK;
  case NET_POLICY_FACTORY:
    for (size_t i = 0; i < control->count; ++i) {
      /* Resolve the copied ID set only on its registry owner. */
      uint64_t id = control->entries[i].binding.interface_id;
      struct net_interface *interface = net_interface_for_id(id);
      if (!interface) {
        return CALL_NOT_FOUND;
      }
      for (size_t j = 0; j < i; ++j) {
        if (control->interfaces[j] == interface) {
          return CALL_BAD_REQUEST;
        }
      }
      control->interfaces[i] = interface;
    }
    return CALL_OK;
  default: return CALL_BAD_OPERATION;
  }
}

bool net_config_service(void)
{
  bool worked = false;
  for (size_t i = 0; i < CONFIG_PENDING_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_pending();
    struct config_call *call = &pending[i];
    if (call->state != CONFIG_QUEUED) {
      unlock_pending();
      cpu_restore_interrupts(flags);
      continue;
    }
    bool cancelled = call->cancelled;
    call->state = CONFIG_RUNNING;
    unlock_pending();
    cpu_restore_interrupts(flags);
    flags = cpu_save_interrupts();
    bool started = forwarder_operation_start(call->context);
    cpu_restore_interrupts(flags);
    enum call_status status = !started ? CALL_DENIED :
        cancelled ? CALL_ENDPOINT_CLOSED : configure(&call->control);
    flags = cpu_save_interrupts();
    lock_pending();
    call->status = status;
    call->state = CONFIG_DONE;
    struct task_wait *wait = call->wait;
    call->wait = NULL;
    task_wait_wake(wait);
    unlock_pending();
    cpu_restore_interrupts(flags);
    worked = true;
  }
  return worked;
}

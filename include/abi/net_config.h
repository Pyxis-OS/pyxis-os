#ifndef ABI_NET_CONFIG_H
#define ABI_NET_CONFIG_H

#include <abi/message.h>
#include <abi/net_interface.h>
#include <abi/handle.h>

/* Registry READ observes retained inventory/policy and looks up READ facets.
 * Only policy MANAGE creates definitions, binds controllers, selects policy or
 * mints packet scopes/configuration facets. Interface grants refer to one
 * retained identity; lo can never receive WRITE. No grant includes IPC rights. */
#define NET_REGISTRY_RIGHT_READ UINT64_C(1)
#define NET_REGISTRY_RIGHTS NET_REGISTRY_RIGHT_READ
#define NET_REGISTRY_NEXT_CONTROLLER UINT64_C(1)
#define NET_REGISTRY_NEXT_INTERFACE UINT64_C(2)
#define NET_REGISTRY_LOOKUP UINT64_C(3)
#define NET_REGISTRY_POLICY UINT64_C(4)

#define NET_INTERFACE_RIGHT_READ UINT64_C(1)
#define NET_INTERFACE_RIGHT_WRITE UINT64_C(2)
#define NET_INTERFACE_RIGHTS (NET_INTERFACE_RIGHT_READ | NET_INTERFACE_RIGHT_WRITE)
#define NET_INTERFACE_QUERY UINT64_C(1)
#define NET_INTERFACE_REPLACE UINT64_C(2)
#define NET_INTERFACE_CLEAR UINT64_C(3)
#define NET_INTERFACE_REPORT_DHCP UINT64_C(4)

#define NET_POLICY_RIGHT_MANAGE UINT64_C(1)
#define NET_POLICY_RIGHTS NET_POLICY_RIGHT_MANAGE
#define NET_POLICY_DEFINE UINT64_C(1)
#define NET_POLICY_BIND UINT64_C(2)
#define NET_POLICY_INTERFACE UINT64_C(3)
#define NET_POLICY_FACTORY UINT64_C(4)
#define NET_POLICY_REPLACE UINT64_C(5)

#define NET_DHCP_NONE UINT32_C(0)
#define NET_DHCP_WAIT_LINK UINT32_C(1)
#define NET_DHCP_SELECTING UINT32_C(2)
#define NET_DHCP_BOUND UINT32_C(3)
#define NET_DHCP_RENEWING UINT32_C(4)
#define NET_DHCP_REBINDING UINT32_C(5)
#define NET_DHCP_EXPIRED UINT32_C(6)
#define NET_DHCP_FAILED UINT32_C(7)

#define NET_DNS_NONE UINT32_C(0)
#define NET_DNS_NUMERIC UINT32_C(1)
#define NET_DNS_DHCP UINT32_C(2)
#define NET_ROUTE_PINNED UINT32_C(0)
#define NET_ROUTE_AUTOMATIC UINT32_C(1)

#define NET_POLICY_DNS_VALID UINT32_C(1)

#define NET_SELECT_VIRTIO UINT32_C(1)
#define NET_SELECT_MAC UINT32_C(2)
#define NET_SELECT_LINKED_CONTROLLER UINT32_C(3)
#define NET_SELECT_CONTROLLER UINT32_C(4)

/* Exactly one selector. VIRTIO requires zero MAC bytes; MAC requires a
 * nonzero unicast address. Both require controller_id zero. LINKED_CONTROLLER
 * and CONTROLLER use a nonzero boot-local controller ID and zero MAC.
 * CONTROLLER admits a prepared controller without requiring reported up carrier;
 * LINKED_CONTROLLER additionally checks fresh known/up carrier on first binding.
 * Reserved bytes are zero. */
struct net_selector {
  uint32_t kind;
  uint8_t mac[6];
  uint8_t reserved[2];
  uint32_t controller_id;
};

#define NET_CONTROLLER_INVENTORY_COMPLETE (UINT32_C(1) << 0)
#define NET_CONTROLLER_PREPARED (UINT32_C(1) << 1)
#define NET_CONTROLLER_CARRIER_KNOWN (UINT32_C(1) << 2)
#define NET_CONTROLLER_LINK_UP (UINT32_C(1) << 3)
#define NET_CONTROLLER_BOUND (UINT32_C(1) << 4)
#define NET_CONTROLLER_SPEED_VALID (UINT32_C(1) << 5)

/* NEXT_CONTROLLER requires READ. Returns the first retained supported controller
 * with ID greater than after_id, in ascending ID order. ID zero marks the end;
 * INVENTORY_COMPLETE is reported even at the end. IDs are opaque, stable until
 * reboot and do not depend on MAC uniqueness. Zero MAC means unknown identity.
 * Carrier sampling is bounded and read-only, including before binding. No DMA,
 * delivery, negotiation or transport activation occurs. Without reported carrier,
 * CARRIER_KNOWN and LINK_UP are clear, including VirtIO without STATUS. */
struct net_controller_request {
  struct message_header header;
  uint32_t after_id, reserved;
};

struct net_controller_reply {
  uint32_t controller_id, driver, flags, interface_class;
  uint8_t mac[6];
  uint8_t reserved_mac[2];
  uint64_t tx_bits_per_second, rx_bits_per_second;
};


struct net_lookup_request {
  struct message_header header;
  uint64_t interface_id;
};

struct net_handle_reply {
  handle_t handle;
};

/* Host-order IPv4. DNS zero clears this interface contribution. Clear retires
 * address state and contributions while preserving the controller binding. */
struct net_config_request {
  struct message_header header;
  uint32_t address, prefix, gateway, dns_server;
};

/* Last userspace assertion, not kernel lease verification. The worker stamps
 * time and rejects an address epoch mismatch without changing the report. */
struct net_dhcp_report_request {
  struct message_header header;
  uint64_t address_epoch;
  uint32_t phase, reserved;
};

struct net_interface_definition {
  char name[16];
  uint32_t interface_class, reserved;
};

struct net_interface_binding {
  uint64_t interface_id;
  struct net_selector selector;
};

/* Records are copied before admission; no caller memory reaches the worker. */
struct net_policy_batch_request {
  struct message_header header;
  uint64_t entries, count;
};

struct net_policy_interface_request {
  struct message_header header;
  uint64_t interface_id, rights;
};

/* Explicit nonempty duplicate-free retained IDs. Scope never expands when
 * later definitions appear. Only TCP/UDP/echo operation rights are accepted. */
struct net_policy_factory_request {
  struct message_header header;
  uint64_t protocol, rights, interface_ids, count;
};

/* AUTOMATIC requires a zero default ID and ranks only within each held scope.
 * PINNED preserves the explicit default, including no route when absent/down.
 * Numeric DNS with interface ID zero uses the caller's already-held scope.
 * Management IDs are always explicit and never follow automatic routing. */
struct net_policy_request {
  struct message_header header;
  uint64_t default_interface_id, dns_interface_id, remote_interface_id;
  uint64_t log_interface_id, debug_interface_id;
  uint32_t dns_server, dns_mode;
  uint32_t route_mode, reserved;
};

struct net_policy_reply {
  uint64_t default_interface_id, dns_interface_id, remote_interface_id;
  uint64_t log_interface_id, debug_interface_id, revision;
  uint32_t dns_server, flags, dns_mode, route_mode;
};

_Static_assert(sizeof(struct net_selector) == 16, "network selector");
_Static_assert(sizeof(struct net_controller_request) == 24, "network controller cursor");
_Static_assert(sizeof(struct net_controller_reply) == 40, "network controller snapshot");
_Static_assert(sizeof(struct net_lookup_request) == 24, "network lookup");
_Static_assert(sizeof(struct net_handle_reply) == 8, "network handle");
_Static_assert(sizeof(struct net_config_request) == 32, "network replacement");
_Static_assert(sizeof(struct net_dhcp_report_request) == 32, "network DHCP report");
_Static_assert(sizeof(struct net_interface_definition) == 24, "network definition");
_Static_assert(sizeof(struct net_interface_binding) == 24, "network binding");
_Static_assert(sizeof(struct net_policy_batch_request) == 32, "network batch");
_Static_assert(sizeof(struct net_policy_interface_request) == 32, "network facet");
_Static_assert(sizeof(struct net_policy_factory_request) == 48, "network factory");
_Static_assert(sizeof(struct net_policy_request) == 72, "network policy replacement");
_Static_assert(sizeof(struct net_policy_reply) == 64, "network policy snapshot");

#endif

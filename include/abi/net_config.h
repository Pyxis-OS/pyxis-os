#ifndef ABI_NET_CONFIG_H
#define ABI_NET_CONFIG_H

#include <abi/message.h>

#define NET_CONFIG_RIGHT_READ (UINT64_C(1) << 0)
#define NET_CONFIG_RIGHT_WRITE (UINT64_C(1) << 1)
#define NET_CONFIG_RIGHTS (NET_CONFIG_RIGHT_READ | NET_CONFIG_RIGHT_WRITE)
#define NET_CONFIG_QUERY UINT64_C(1)
#define NET_CONFIG_REPLACE UINT64_C(2)
#define NET_CONFIG_CLEAR UINT64_C(3)
#define NET_CONFIG_BIND UINT64_C(4)
#define NET_CONFIG_LOOKUP UINT64_C(5)
#define NET_CONFIG_SET_DNS UINT64_C(6)

#define NET_SELECT_VIRTIO UINT32_C(1)
#define NET_SELECT_MAC UINT32_C(2)

/* Exactly one selector. VIRTIO requires zero MAC bytes; MAC requires a
 * nonzero unicast address. Reserved bytes are zero. */
struct net_selector {
  uint32_t kind;
  uint8_t mac[6];
  uint8_t reserved[6];
};

struct net_select_request {
  struct message_header header;
  struct net_selector selector;
};

#define NET_CONFIG_PRESENT (UINT32_C(1) << 0)
#define NET_CONFIG_READY (UINT32_C(1) << 1)
#define NET_CONFIG_LINK_UP (UINT32_C(1) << 2)
#define NET_CONFIG_ASSIGNED (UINT32_C(1) << 3)
#define NET_CONFIG_BOUND (UINT32_C(1) << 4)

/* Authority configures the single net0 interface. Addresses are host-order
 * IPv4. Gateway zero means none. Replacement requires an existing binding.
 * DNS is the unicast IPv4 server chosen by userspace, independent of assignment.
 * Replacement validates everything before mutation and publishes IPv4 and DNS
 * together. Clear preserves DNS. Clear/replace cancel
 * outstanding non-loopback echo work and discard ARP state. */
struct net_config_request {
  struct message_header header;
  uint32_t address, prefix, gateway, dns_server;
};

/* SET_DNS requires WRITE; it changes only the chosen server, including while
 * net0 is unbound/unassigned. No resolver, DHCP or precedence policy runs here. */
struct net_dns_request {
  struct message_header header;
  uint32_t dns_server, reserved;
};

/* QUERY is header-only and requires READ; it inspects the binding and succeeds
 * with zero flags/MAC/address when unbound. BIND requires WRITE and LOOKUP READ;
 * both take net_select_request and return this snapshot after a unique match.
 * NOT_FOUND means absent, BUSY means ambiguous or a different existing binding,
 * UNAVAILABLE means discovery/identity was incomplete. Errors mutate nothing.
 * BIND selects once until reboot, activating only that controller. An identical
 * controller match is idempotent. LOOKUP never binds or activates hardware.
 * BOUND means this snapshot describes the bound controller; LOOKUP of an
 * unbound candidate has no assigned address. No automatic fallback/rebinding.
 * REPLACE requires WRITE and returns no bytes; CLEAR is header-only with WRITE.
 * READY means active transport with stable device configuration, independent of
 * carrier. DNS is zero until userspace selects it; QUERY returns it even when
 * unbound. MAC is zero when preparation failed; assigned settings survive link
 * loss. Snapshot fields do not grant packet or configuration authority. */
struct net_config_reply {
  uint32_t flags, address, prefix, gateway, mtu;
  uint8_t mac[6];
  uint8_t reserved[6];
  uint32_t dns_server;
};

/* Synchronous calls use eight shared slots, returning QUEUE_FULL on exhaustion.
 * The network worker performs all reads/mutations. No device/peer wait occurs.
 * Errors return no bytes. Closing a copied grant does not revoke blocked calls. */
_Static_assert(sizeof(struct net_config_request) == 32, "network configuration request");
_Static_assert(sizeof(struct net_selector) == 16, "network selector");
_Static_assert(sizeof(struct net_select_request) == 32, "network selection request");
_Static_assert(sizeof(struct net_dns_request) == 24, "network DNS request");
_Static_assert(sizeof(struct net_config_reply) == 36, "network configuration snapshot");

#endif

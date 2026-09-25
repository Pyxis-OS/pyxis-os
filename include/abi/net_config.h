#ifndef ABI_NET_CONFIG_H
#define ABI_NET_CONFIG_H

#include <abi/message.h>

#define NET_CONFIG_RIGHT_READ (UINT64_C(1) << 0)
#define NET_CONFIG_RIGHT_WRITE (UINT64_C(1) << 1)
#define NET_CONFIG_RIGHTS (NET_CONFIG_RIGHT_READ | NET_CONFIG_RIGHT_WRITE)
#define NET_CONFIG_QUERY UINT64_C(1)
#define NET_CONFIG_REPLACE UINT64_C(2)
#define NET_CONFIG_CLEAR UINT64_C(3)

#define NET_CONFIG_PRESENT (UINT32_C(1) << 0)
#define NET_CONFIG_READY (UINT32_C(1) << 1)
#define NET_CONFIG_LINK_UP (UINT32_C(1) << 2)
#define NET_CONFIG_ASSIGNED (UINT32_C(1) << 3)

/* Authority selects the single net0 interface, not an interface name supplied
 * by the caller. Addresses are host-order IPv4. Gateway zero means none.
 * Replacement validates everything before mutation. Clear/replace cancel
 * outstanding non-loopback echo work and discard ARP state. */
struct net_config_request {
  struct message_header header;
  uint32_t address, prefix, gateway, reserved;
};

/* QUERY is header-only and requires READ; it succeeds even when no NIC exists.
 * REPLACE requires WRITE and returns no bytes; CLEAR is header-only with WRITE.
 * READY means active transport with stable device configuration, independent of
 * carrier. MAC is zero when preparation failed; assigned settings survive link
 * loss. Snapshot fields do not grant packet or configuration authority. */
struct net_config_reply {
  uint32_t flags, address, prefix, gateway, mtu;
  uint8_t mac[6];
  uint8_t reserved[6];
};

/* Synchronous calls use eight shared slots, returning QUEUE_FULL on exhaustion.
 * The network worker performs all reads/mutations. No device/peer wait occurs.
 * Errors return no bytes. Closing a copied grant does not revoke blocked calls. */
_Static_assert(sizeof(struct net_config_request) == 32, "network configuration request");
_Static_assert(sizeof(struct net_config_reply) == 32, "network configuration snapshot");

#endif

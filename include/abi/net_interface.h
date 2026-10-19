#ifndef ABI_NET_INTERFACE_H
#define ABI_NET_INTERFACE_H

#include <abi/message.h>

#define NET_INTERFACE_CLASS_LOOPBACK UINT32_C(1)
#define NET_INTERFACE_CLASS_ETHERNET UINT32_C(2)
#define NET_INTERFACE_CLASS_WIFI UINT32_C(3)

#define NET_DRIVER_VIRTIO UINT32_C(1)
#define NET_DRIVER_RTL8111 UINT32_C(2)
#define NET_DRIVER_CDC_NCM UINT32_C(3)

/* Supported-controller discovery has completed, as in controller inventory.
 * This does not prevent policy from defining more logical interfaces later. */
#define NET_INTERFACE_INVENTORY_COMPLETE (UINT32_C(1) << 0)
#define NET_INTERFACE_DEFINED (UINT32_C(1) << 1)
#define NET_INTERFACE_BOUND (UINT32_C(1) << 2)
#define NET_INTERFACE_PREPARED (UINT32_C(1) << 3)
#define NET_INTERFACE_READY (UINT32_C(1) << 4)
#define NET_INTERFACE_CARRIER_KNOWN (UINT32_C(1) << 5)
#define NET_INTERFACE_LINK_UP (UINT32_C(1) << 6)
#define NET_INTERFACE_AUTHENTICATION_VALID (UINT32_C(1) << 7)
#define NET_INTERFACE_AUTHENTICATED (UINT32_C(1) << 8)
#define NET_INTERFACE_ADDRESS_VALID (UINT32_C(1) << 9)
#define NET_INTERFACE_MAC_VALID (UINT32_C(1) << 10)
#define NET_INTERFACE_DNS_VALID (UINT32_C(1) << 11)
#define NET_INTERFACE_DHCP_REPORT_VALID (UINT32_C(1) << 12)
#define NET_INTERFACE_FLAGS (NET_INTERFACE_INVENTORY_COMPLETE | NET_INTERFACE_DEFINED | \
    NET_INTERFACE_BOUND | NET_INTERFACE_PREPARED | NET_INTERFACE_READY | \
    NET_INTERFACE_CARRIER_KNOWN | NET_INTERFACE_LINK_UP | \
    NET_INTERFACE_AUTHENTICATION_VALID | NET_INTERFACE_AUTHENTICATED | \
    NET_INTERFACE_ADDRESS_VALID | NET_INTERFACE_MAC_VALID | NET_INTERFACE_DNS_VALID | \
    NET_INTERFACE_DHCP_REPORT_VALID)

/* NEXT_INTERFACE requires NET_REGISTRY READ. Return the least boot-local ID
 * greater than after_id. IDs never recycle before reboot. A zero interface_id
 * marks the end; all other fields are zero except optional INVENTORY_COMPLETE.
 * Enumeration is read-only and conveys no packet or configuration authority. */
struct net_interface_request {
  struct message_header header;
  uint64_t after_id;
};

/* IPv4 values are host order. Names are NUL-terminated with zero tails. A clear
 * address/DNS/DHCP/MAC validity flag leaves its associated value fields zero.
 * Address epoch and route revision still identify the current context state.
 * Carrier validity/state are
 * independent of prepared and active transport readiness. IP-only loopback
 * reports no controller, driver, MAC, carrier or authentication state.
 * DHCP fields are userspace reports only when DHCP_REPORT_VALID is set; DNS is
 * an interface contribution only when DNS_VALID is set, not global policy. */
struct net_interface_reply {
  uint64_t interface_id, address_epoch, route_revision, dhcp_report_time_ns;
  uint32_t controller_id, interface_class, driver, flags, mtu;
  uint32_t address, prefix, gateway, dhcp_dns_server, dhcp_phase;
  char name[16];
  uint8_t mac[6];
  uint8_t reserved[2];
};

_Static_assert(sizeof(struct net_interface_request) == 24, "network interface cursor");
_Static_assert(sizeof(struct net_interface_reply) == 96, "network interface snapshot");

#endif

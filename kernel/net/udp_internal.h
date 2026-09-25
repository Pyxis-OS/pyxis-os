#ifndef NET_UDP_INTERNAL_H
#define NET_UDP_INTERNAL_H

#include <kernel/net/udp.h>

#define UDP_HEADER_SIZE 8
#define UDP_RECEIVE_QUEUE_LIMIT 4
#define UDP_RECEIVE_GLOBAL_LIMIT 16

struct udp_datagram {
  struct net_packet *packet; /* Owned UDP header + payload, including empty payloads. */
  uint32_t source;
};

/* Binding and receive queue belong exclusively to the network worker. Calls
 * borrow the object through a live grant, including while parked on another CPU. */
struct udp_endpoint {
  struct kernel_object object;
  struct udp_endpoint *next;
  struct udp_endpoint *retired_next;
  struct udp_endpoint_info local;
  struct udp_datagram received[UDP_RECEIVE_QUEUE_LIMIT];
  size_t receive_head, receive_count;
};

struct udp_endpoint *net_udp_find_endpoint(uint32_t address, uint16_t port);
bool net_udp_service_io(void);
void net_udp_stop_io(struct udp_endpoint *endpoint, enum call_status status);
void net_udp_discard_received(struct udp_endpoint *endpoint);
void net_udp_deliver(uint32_t source, uint32_t destination, uint16_t port,
    const uint8_t *message, size_t length);
enum net_result net_udp_send_packet(struct udp_endpoint *endpoint, uint32_t address,
    uint16_t port, const uint8_t *data, size_t length, uint64_t deadline, uint64_t token);

#endif

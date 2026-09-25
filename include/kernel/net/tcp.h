#ifndef KERNEL_NET_TCP_H
#define KERNEL_NET_TCP_H

#include <kernel/net/interface.h>

#define NET_TCP_CONNECTION_LIMIT 32
#define NET_TCP_RECEIVE_BYTES (16 * 1024)
#define NET_TCP_SEND_BYTES (16 * 1024)
#define NET_TCP_MAX_WAIT_NS UINT64_C(30000000000)

struct tcp_connection;

/* Sole network worker, IF=1, outside lwIP callbacks. Prepare one owned record
 * and an explicitly bound PCB, but send no SYN. Deadline includes preparation;
 * failure leaves *connection unchanged. No private caller buffers are retained.
 * Active open and capability publication are separate from preparation. */
enum net_result net_tcp_prepare(uint32_t destination, uint16_t port, uint64_t deadline,
    struct tcp_connection **connection);
/* Release the one external owner. Abort unless FIN was explicitly queued and
 * all received data consumed; then retain bounded graceful teardown/TIME_WAIT.
 * Handle copies will share that external owner through object references.
 * This call consumes the pointer; actual metadata disposal is worker-deferred. */
void net_tcp_release(struct tcp_connection *connection);

#endif

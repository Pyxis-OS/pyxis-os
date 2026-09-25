#ifndef KERNEL_NET_TCP_H
#define KERNEL_NET_TCP_H

#include <kernel/net/interface.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <abi/tcp.h>

#define NET_TCP_CONNECTION_LIMIT 32
#define NET_TCP_RECEIVE_BYTES (16 * 1024)
#define NET_TCP_SEND_BYTES (16 * 1024)
#define NET_TCP_MAX_WAIT_NS TCP_CONNECT_MAX_WAIT_NS

struct tcp_connection;

/* Sole network worker, IF=1, outside lwIP callbacks. Prepare one owned record
 * and an explicitly bound PCB, but send no SYN. Deadline includes preparation;
 * failure leaves *connection unchanged. No private caller buffers are retained.
 * Active open and capability publication are separate from preparation. */
enum net_result net_tcp_prepare(uint32_t destination, uint16_t port, uint64_t deadline,
    struct tcp_connection **connection);
/* Release the one external owner. Abort unless write shutdown was committed and
 * all received data consumed; then retain bounded graceful teardown/TIME_WAIT.
 * Handle copies will share that external owner through object references.
 * This call consumes the pointer; actual metadata disposal is worker-deferred. */
void net_tcp_release(struct tcp_connection *connection);

/* Caller, IF=0: CONNECT lends the exclusively owned table while parked.
 * Others retain their grant. No stack/reply/user pointers cross to the worker. */
enum call_status net_tcp_connect(struct capability_table *table, uint32_t address,
    uint16_t port, uint64_t deadline, struct tcp_connect_reply *reply);
enum call_status net_tcp_inspect(struct kernel_object *object, struct tcp_connection_info *reply);
enum call_status net_tcp_abort(struct kernel_object *object);
enum call_status net_tcp_shutdown_write(struct kernel_object *object);
enum call_status net_tcp_read(struct kernel_object *object, size_t capacity,
    uint64_t deadline, void *data, struct tcp_read_reply *reply);

enum call_status net_tcp_write(struct kernel_object *object, const void *data,
    size_t length, uint64_t deadline, struct tcp_write_reply *reply);

/* Sole network worker, IF=1, after packet/timer processing. */
bool net_tcp_service(void);
bool net_tcp_next_deadline(uint64_t *deadline);

#endif

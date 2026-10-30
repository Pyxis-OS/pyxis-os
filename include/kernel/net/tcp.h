#ifndef KERNEL_NET_TCP_H
#define KERNEL_NET_TCP_H

#include <kernel/net/interface.h>
#include <kernel/net/factory.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <abi/tcp.h>

/* Backed receive capacity: the full 16-bit window at scale four. */
#define NET_TCP_RECEIVE_SCALE 4
#define NET_TCP_RECEIVE_BYTES (UINT32_C(65535) << NET_TCP_RECEIVE_SCALE)
#define NET_TCP_SEND_BYTES 65535
#define NET_TCP_MAX_WAIT_NS TCP_CONNECT_MAX_WAIT_NS

struct tcp_connection;
struct readiness_source;

/* IF=0, held storage. All native grants count, including zero-right copies. */
bool tcp_grant_retain(struct kernel_object *object, uint64_t rights);
void tcp_grant_release(struct kernel_object *object, uint64_t rights);
struct readiness_source *tcp_readiness_source(struct kernel_object *object);
/* Sole network worker; current native terminal result, without consuming it. */
enum call_status tcp_readiness_status(struct kernel_object *object);

/* Sole network worker, IF=1, outside lwIP callbacks. Prepare one owned record
 * and a pre-SYN PCB from the scope-checked route. The private worker binding
 * step completes explicit binding before active open. Deadline includes all
 * preparation; failure leaves *connection unchanged.
 * No private caller buffers are retained.
 * Active open and capability publication are separate from preparation. */
enum net_result net_tcp_prepare(const struct ipv4_route *route,
    uint32_t destination, uint16_t port, uint64_t deadline,
    struct tcp_connection **connection);
/* Release the one external owner. Abort unless write shutdown was committed and
 * all received data consumed; then retain bounded graceful teardown/TIME_WAIT.
 * Native grants share the external owner; observation retains only storage.
 * This call consumes the pointer; actual metadata disposal is worker-deferred. */
void net_tcp_release(struct tcp_connection *connection);

/* Current user task/table, IF=0, no prepared BSP request. CONNECT/LISTEN/ACCEPT
 * reserve a private slot before parking. Worker preparation owns its grant until
 * successful caller collection moves it into the slot. Failure releases private
 * ownership. Other operations retain admitted storage. No caller pointers cross. */
enum call_status net_tcp_connect(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint32_t address,
    uint16_t port, uint64_t deadline, struct tcp_connect_reply *reply);
enum call_status net_tcp_listen(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint32_t address,
    uint16_t port, struct tcp_listen_reply *reply);
enum call_status net_tcp_accept(struct kernel_object *object, struct capability_table *table,
    uint64_t deadline, bool nonblocking, struct tcp_accept_reply *reply);
enum call_status net_tcp_listener_inspect(struct kernel_object *object,
    struct tcp_listener_info *reply);
enum call_status net_tcp_inspect(struct kernel_object *object, struct tcp_connection_info *reply);
enum call_status net_tcp_abort(struct kernel_object *object);
enum call_status net_tcp_shutdown_write(struct kernel_object *object);
enum call_status net_tcp_read(struct kernel_object *object, size_t capacity,
    uint64_t deadline, bool nonblocking, void *data, struct tcp_read_reply *reply);

enum call_status net_tcp_write(struct kernel_object *object, const void *data,
    size_t length, uint64_t deadline, bool nonblocking, struct tcp_write_reply *reply);

/* Sole network worker, IF=1, after packet/timer processing. */
bool net_tcp_service(void);
bool net_tcp_next_deadline(uint64_t *deadline);

#endif

#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/net/interface.h>
#include <kernel/net/lwip.h>
#include <kernel/task.h>
#include <kernel/random.h>
#include <siphash.h>
#include <stdatomic.h>
#include <caelum_hooks.h>
#include "identity.h"
#include "../wire.h"

#define TCP_SECRET_BYTES 16
#define TCP_SEED_WAIT_MS 5000
#define TCP_ISN_TICK_NS UINT64_C(4000)

enum identity_state { IDENTITY_PENDING, IDENTITY_READY, IDENTITY_UNAVAILABLE };
static atomic_int identity_state;
/* Written before READY publication, immutable afterward, never logged. */
static uint8_t isn_key[TCP_SECRET_BYTES], port_key[TCP_SECRET_BYTES];
static uint64_t port_counter; /* Network worker only; never wraps. */

static uint64_t keyed_hash(const uint8_t *bytes, size_t length, const uint8_t *key)
{
  uint8_t output[8];
  KASSERT(siphash(bytes, length, key, output, sizeof(output)) == 0);
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(output); ++i) {
    value |= (uint64_t)output[i] << (i * 8);
  }
  return value;
}

static void prepare_identity(void *argument)
{
  (void)argument;
  uint8_t secrets[2 * TCP_SECRET_BYTES] = {0};
  uint64_t flags = cpu_save_interrupts();
  enum call_status result = random_read(secrets, sizeof(secrets),
      task_deadline_after_ms(TCP_SEED_WAIT_MS));
  cpu_restore_interrupts(flags);

  if (result == CALL_OK) {
    memcpy(isn_key, secrets, sizeof(isn_key));
    memcpy(port_key, secrets + sizeof(isn_key), sizeof(port_key));
  }
  /* Do not leave an extra key copy in a freed kernel-task stack. */
  volatile uint8_t *wipe = secrets;
  for (size_t i = 0; i < sizeof(secrets); ++i) {
    wipe[i] = 0;
  }
  atomic_store_explicit(&identity_state,
      result == CALL_OK ? IDENTITY_READY : IDENTITY_UNAVAILABLE, memory_order_release);

  flags = cpu_save_interrupts();
  net_worker_notify();
  cpu_restore_interrupts(flags);
  if (result == CALL_OK) {
    kinfo("net: TCP transport identity ready\n");
  } else {
    kerror("net: TCP identity unavailable for this boot (entropy error %u)\n", (unsigned)result);
  }
}

void net_lwip_identity_start(void)
{
  /* net_init calls this before scheduling, with IF=0. Failure leaves other
   * protocols usable; the network worker never waits for this task. */
  enum mm_result result = kernel_task_create(prepare_identity, NULL);
  if (result != MM_OK) {
    atomic_store_explicit(&identity_state, IDENTITY_UNAVAILABLE, memory_order_release);
    kerror("net: cannot create TCP identity task (error %u)\n", (unsigned)result);
  }
}

bool tcp_identity_ready(void)
{
  net_worker_assert_context();
  return atomic_load_explicit(&identity_state, memory_order_acquire) == IDENTITY_READY;
}

u32_t caelum_lwip_isn(const ip4_addr_t *local, u16_t local_port,
    const ip4_addr_t *remote, u16_t remote_port)
{
  net_worker_assert_context();
  KASSERT(tcp_identity_ready());
  uint8_t tuple[12];
  net_write_u32(tuple, lwip_ntohl(ip4_addr_get_u32(local)));
  net_write_u32(tuple + 4, lwip_ntohl(ip4_addr_get_u32(remote)));
  net_write_u16(tuple + 8, local_port);
  net_write_u16(tuple + 10, remote_port);
  /* RFC 9293: a monotonic 4-us clock plus a secret-derived tuple offset,
   * modulo the 32-bit sequence space. Wall-clock changes cannot move it. */
  return (u32_t)(arch_monotonic_ns() / TCP_ISN_TICK_NS) +
      (u32_t)keyed_hash(tuple, sizeof(tuple), isn_key);
}

bool tcp_identity_ports(uint32_t local, uint32_t remote, uint16_t remote_port,
    uint16_t *first)
{
  net_worker_assert_context();
  if (!tcp_identity_ready() || port_counter == UINT64_MAX) {
    return false;
  }
  uint8_t tuple[10];
  net_write_u32(tuple, local);
  net_write_u32(tuple + 4, remote);
  net_write_u16(tuple + 8, remote_port);
  /* RFC 6056 algorithm 3's secret per-destination offset and advancing
   * counter avoid independently random starts reusing a passive-closed tuple.
   * Each pending bind keeps its own contiguous scan across worker turns. */
  uint64_t offset = keyed_hash(tuple, sizeof(tuple), port_key);
  *first = (offset + port_counter++) & (TCP_EPHEMERAL_COUNT - 1);
  return true;
}

bool tcp_identity_port_advance(uint16_t *candidate)
{
  net_worker_assert_context();
  if (port_counter == UINT64_MAX) {
    return false;
  }
  ++port_counter;
  *candidate = (*candidate + 1) & (TCP_EPHEMERAL_COUNT - 1);
  return true;
}

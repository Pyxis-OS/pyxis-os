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
    klog("net: TCP transport identity ready\n");
  } else {
    klog("net: TCP identity unavailable for this boot (entropy error %u)\n", (unsigned)result);
  }
}

void net_lwip_identity_start(void)
{
  /* net_init calls this before scheduling, with IF=0. Failure leaves other
   * protocols usable; the network worker never waits for this task. */
  enum mm_result result = kernel_task_create(prepare_identity, NULL);
  if (result != MM_OK) {
    atomic_store_explicit(&identity_state, IDENTITY_UNAVAILABLE, memory_order_release);
    klog("net: cannot create TCP identity task (error %u)\n", (unsigned)result);
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
    uint16_t *first, uint16_t *stride)
{
  net_worker_assert_context();
  if (!tcp_identity_ready() || port_counter == UINT64_MAX) {
    return false;
  }
  uint8_t input[18];
  net_write_u32(input, local);
  net_write_u32(input + 4, remote);
  net_write_u16(input + 8, remote_port);
  uint64_t counter = ++port_counter;
  for (size_t i = 0; i < sizeof(counter); ++i) {
    input[10 + i] = counter >> (i * 8);
  }
  uint64_t hash = keyed_hash(input, sizeof(input), port_key);
  *first = hash & (TCP_EPHEMERAL_COUNT - 1);
  /* An odd stride visits every offset in this power-of-two range once. */
  *stride = ((hash >> 32) & (TCP_EPHEMERAL_COUNT - 1)) | 1;
  return true;
}

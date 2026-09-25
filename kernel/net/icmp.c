#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/net/icmp.h>
#include <kernel/net/echo.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "wire.h"

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_CODE 0
#define ICMP_REPLY_WAIT_MS 3000

struct icmp_echo_header {
  uint8_t type;
  uint8_t code;
  uint8_t checksum[2];
  uint8_t identifier[2];
  uint8_t sequence[2];
};
static_assert(sizeof(struct icmp_echo_header) == ICMP_ECHO_HEADER_SIZE);

static struct {
  uint64_t requests, replies, malformed, unsupported, replies_sent, reply_failures;
} icmp_stats;

/* All caller bytes are copied before return. IF=0 protects only allocation
 * and release; protocol processing and transport copies run with IF=1. */
static enum net_result send_echo(uint32_t source, uint32_t destination,
    uint8_t type, uint16_t identifier, uint16_t sequence,
    const void *payload, size_t length, uint64_t deadline, uint64_t token)
{
  if (length > ICMP_ECHO_MAX_PAYLOAD || (length && !payload)) {
    return NET_INVALID;
  }
  uint64_t flags = cpu_save_interrupts();
  struct net_packet *packet = net_packet_allocate(
      IPV4_HEADER_SIZE + ICMP_ECHO_HEADER_SIZE + length);
  cpu_restore_interrupts(flags);
  if (!packet) {
    return NET_NO_MEMORY;
  }
  struct icmp_echo_header *header = (void *)(packet->data + IPV4_HEADER_SIZE);
  *header = (struct icmp_echo_header){ .type = type, .code = ICMP_ECHO_CODE };
  net_write_u16(header->identifier, identifier);
  net_write_u16(header->sequence, sequence);
  if (length) {
    memcpy((uint8_t *)header + ICMP_ECHO_HEADER_SIZE, payload, length);
  }
  net_write_u16(header->checksum, net_checksum((const uint8_t *)header,
      ICMP_ECHO_HEADER_SIZE + length));

  enum net_result result = net_ipv4_transmit(packet, source, destination,
      IPV4_PROTOCOL_ICMP, deadline,
      (struct ipv4_completion){token ? IPV4_NOTIFY_ECHO : IPV4_NOTIFY_NONE, token});
  if (result != NET_OK) {
    flags = cpu_save_interrupts();
    net_packet_release(packet);
    cpu_restore_interrupts(flags);
  }
  return result;
}

enum net_result net_icmp_echo_send(uint32_t source, uint32_t destination,
    uint16_t identifier, uint16_t sequence, const void *payload, size_t length,
    uint64_t deadline, uint64_t token)
{
  KASSERT(arch_cpu_index() == 0);
  return send_echo(source, destination, ICMP_ECHO_REQUEST,
      identifier, sequence, payload, length, deadline, token);
}

void net_icmp_receive(uint32_t source, uint32_t destination,
    const uint8_t *message, size_t length)
{
  if (length < ICMP_ECHO_HEADER_SIZE || net_checksum(message, length)) {
    ++icmp_stats.malformed;
    return;
  }

  const struct icmp_echo_header *header = (const void *)message;
  if (header->type != ICMP_ECHO_REQUEST && header->type != ICMP_ECHO_REPLY) {
    ++icmp_stats.unsupported;
    return;
  }
  if (header->code != ICMP_ECHO_CODE) {
    ++icmp_stats.malformed;
    return;
  }
  if (header->type == ICMP_ECHO_REPLY) {
    ++icmp_stats.replies;
    net_echo_receive(source, destination, net_read_u16(header->identifier),
        net_read_u16(header->sequence), message + ICMP_ECHO_HEADER_SIZE,
        length - ICMP_ECHO_HEADER_SIZE);
    return;
  }

  ++icmp_stats.requests;
  enum net_result result = send_echo(destination, source, ICMP_ECHO_REPLY,
      net_read_u16(header->identifier), net_read_u16(header->sequence),
      message + ICMP_ECHO_HEADER_SIZE, length - ICMP_ECHO_HEADER_SIZE,
      task_deadline_after_ms(ICMP_REPLY_WAIT_MS), 0);
  if (result == NET_OK) {
    ++icmp_stats.replies_sent;
  } else {
    ++icmp_stats.reply_failures;
  }
}

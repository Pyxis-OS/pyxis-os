#define _POSIX_C_SOURCE 200809L

#include "rsp.h"
#include "../remote/sha256.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define GDB_PORT 1235
#define DISCOVERY_MS 500
#define HELLO_MS 1000
#define RECORD_COUNT 16
#define TCP_QUEUE_BYTES (DEBUG_PAYLOAD_BYTES * RECORD_COUNT)

enum bridge_phase {
  BRIDGE_DISCOVERING,
  BRIDGE_OFFERED,
  BRIDGE_BINDING,
  BRIDGE_STOPPED,
  BRIDGE_RUNNING,
};

struct record {
  uint8_t bytes[DEBUG_PAYLOAD_BYTES];
  size_t length;
  uint64_t command;
};

struct bridge {
  int udp, listener, client;
  struct sockaddr_in beacon, peer;
  const char *name;
  uint8_t image[DEBUG_IMAGE_BYTES], mac[6], filter_mac[6];
  uint8_t boot[DEBUG_NONCE_BYTES], session[DEBUG_NONCE_BYTES];
  bool filter, ambiguous, terminal, waiting_continue;
  struct record replies[RECORD_COUNT];
  bool reply_retry[RECORD_COUNT];
  size_t reply_head, reply_count;
  enum bridge_phase phase;
  uint64_t generation, next_command, pending_command;
  uint64_t tx_sequence, rx_sequence, last_seen, last_hello, last_heartbeat;
  uint64_t discovery_deadline, last_send;
  struct record pending;
  struct record completed;
  struct record records[RECORD_COUNT];
  size_t record_head, record_count;
  uint8_t transmit[DEBUG_DATAGRAM_BYTES];
  size_t transmit_length;
  uint8_t tcp_queue[TCP_QUEUE_BYTES];
  size_t tcp_length;
  struct rsp_parser parser;
};

static volatile sig_atomic_t interrupted;

static void signal_handler(int number)
{
  (void)number;
  interrupted = 1;
}

static uint64_t monotonic_ms(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
    perror("clock_gettime");
    exit(1);
  }
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void usage(FILE *output)
{
  fprintf(output,
      "Usage: pyxis-gdb-bridge --target NAME --kernel EXACT_ELF [options]\n"
      "  --source MAC          Select a unicast target MAC.\n"
      "  --bind IPv4           Numeric local address (default 0.0.0.0).\n"
      "  --beacon-address IPv4 Discovery broadcast (default 255.255.255.255).\n"
      "  --udp-port PORT       Debugger UDP port (default 2326).\n"
      "GDB connects to 127.0.0.1:1235 with target remote. Use the same exact ELF.\n"
      "The bridge verifies the full ELF SHA-256 before binding. Name, MAC and\n"
      "session filters do not authenticate this unencrypted trusted-LAN link.\n"
      "Running Ctrl+C requires task 4 and is unavailable; continue waits for\n"
      "a later stop in the same boot/image. Panic stops never resume.\n");
}

static int hex_digit(char byte)
{
  if (byte >= '0' && byte <= '9') {
    return byte - '0';
  }
  if (byte >= 'a' && byte <= 'f') {
    return byte - 'a' + 10;
  }
  if (byte >= 'A' && byte <= 'F') {
    return byte - 'A' + 10;
  }
  return -1;
}

static bool parse_mac(const char *text, uint8_t mac[6])
{
  if (strlen(text) != 17) {
    return false;
  }
  for (unsigned i = 0; i < 6; ++i) {
    int high = hex_digit(text[i * 3]);
    int low = hex_digit(text[i * 3 + 1]);
    if (high < 0 || low < 0 || (i != 5 && text[i * 3 + 2] != ':')) {
      return false;
    }
    mac[i] = high * 16 + low;
  }
  const uint8_t zero[6] = {0};
  return !(mac[0] & 1) && memcmp(mac, zero, sizeof(zero));
}

static bool hash_kernel(const char *path, uint8_t image[DEBUG_IMAGE_BYTES])
{
  FILE *file = fopen(path, "rb");
  if (!file) {
    perror(path);
    return false;
  }
  uint8_t bytes[16384];
  size_t size = fread(bytes, 1, sizeof(bytes), file);
  if (size < 4 || memcmp(bytes, "\177ELF", 4)) {
    fprintf(stderr, "Kernel must be an exact ELF file: %s\n", path);
    fclose(file);
    return false;
  }
  struct sha256 hash;
  sha256_init(&hash);
  do {
    sha256_update(&hash, bytes, size);
    size = fread(bytes, 1, sizeof(bytes), file);
  } while (size);
  bool valid = !ferror(file);
  if (!valid) {
    perror(path);
  }
  fclose(file);
  if (valid) {
    sha256_sum(&hash, image);
  }
  return valid;
}

static bool random_session(uint8_t session[DEBUG_NONCE_BYTES])
{
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    perror("/dev/urandom");
    return false;
  }
  size_t offset = 0;
  while (offset < DEBUG_NONCE_BYTES) {
    ssize_t count = read(fd, session + offset, DEBUG_NONCE_BYTES - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      fprintf(stderr, "Could not obtain a complete cryptographic session nonce\n");
      close(fd);
      return false;
    }
    offset += count;
  }
  close(fd);
  return true;
}

static bool nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool open_sockets(struct bridge *bridge, const char *bind_address, unsigned port)
{
  bridge->udp = socket(AF_INET, SOCK_DGRAM, 0);
  bridge->listener = socket(AF_INET, SOCK_STREAM, 0);
  if (bridge->udp < 0 || bridge->listener < 0) {
    perror("socket");
    return false;
  }
  int enabled = 1;
  if (setsockopt(bridge->udp, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0 ||
      setsockopt(bridge->listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
    perror("setsockopt");
    return false;
  }
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
  if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
    fprintf(stderr, "Invalid numeric --bind IPv4\n");
    return false;
  }
  if (bind(bridge->udp, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("UDP bind");
    return false;
  }
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(GDB_PORT);
  if (bind(bridge->listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
      listen(bridge->listener, 1) < 0 || !nonblocking(bridge->udp) ||
      !nonblocking(bridge->listener)) {
    perror("TCP listener");
    return false;
  }
  return true;
}

static void close_client(struct bridge *bridge, const char *reason)
{
  if (bridge->client >= 0) {
    fprintf(stderr, "GDB connection closed: %s\n", reason);
    close(bridge->client);
    bridge->client = -1;
  }
  bridge->phase = BRIDGE_DISCOVERING;
  bridge->ambiguous = false;
  bridge->generation = 0;
  bridge->pending_command = 0;
  bridge->completed.command = 0;
  bridge->waiting_continue = false;
  bridge->reply_head = bridge->reply_count = 0;
  bridge->record_count = bridge->record_head = 0;
  bridge->transmit_length = bridge->tcp_length = 0;
  bridge->tx_sequence = bridge->rx_sequence = 0;
  bridge->last_hello = 0;
  memset(&bridge->parser, 0, sizeof(bridge->parser));
  memset(bridge->boot, 0, sizeof(bridge->boot));
  if (!random_session(bridge->session)) {
    interrupted = 1;
  }
}

static struct debug_packet packet_header(const struct bridge *bridge,
                                         enum debug_packet_kind kind)
{
  struct debug_packet packet = {
    .kind = kind,
    .flags = bridge->client >= 0 ? DEBUG_FLAG_ATTACHED : 0,
    .generation = bridge->generation,
    .ack = bridge->rx_sequence,
  };
  memcpy(packet.boot, bridge->boot, sizeof(packet.boot));
  memcpy(packet.session, bridge->session, sizeof(packet.session));
  return packet;
}

static void send_bytes(struct bridge *bridge, const uint8_t *bytes, size_t length,
                       const struct sockaddr_in *destination)
{
  ssize_t sent = sendto(bridge->udp, bytes, length, 0,
                        (const struct sockaddr *)destination, sizeof(*destination));
  if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
    perror("debugger UDP send");
  }
}

static void send_packet(struct bridge *bridge, const struct debug_packet *packet,
                        const struct sockaddr_in *destination)
{
  uint8_t bytes[DEBUG_DATAGRAM_BYTES];
  size_t length = debug_encode(bytes, packet);
  if (length) {
    send_bytes(bridge, bytes, length, destination);
  }
}

static void send_hello(struct bridge *bridge, uint64_t now)
{
  uint8_t payload[DEBUG_HELLO_FIXED_BYTES + DEBUG_NAME_BYTES] = {0};
  size_t length = strlen(bridge->name);
  memcpy(payload, bridge->image, DEBUG_IMAGE_BYTES);
  payload[32] = length;
  memcpy(payload + DEBUG_HELLO_FIXED_BYTES, bridge->name, length);
  struct debug_packet packet = packet_header(bridge, DEBUG_HELLO);
  packet.payload = payload;
  packet.length = DEBUG_HELLO_FIXED_BYTES + length;
  if (bridge->waiting_continue) {
    packet.flags |= DEBUG_FLAG_RUNNING;
    packet.command = bridge->pending_command;
  }
  send_packet(bridge, &packet, &bridge->beacon);
  bridge->last_hello = now;
}

static void send_bind(struct bridge *bridge, uint64_t now)
{
  struct debug_packet packet = packet_header(bridge, DEBUG_BIND);
  packet.payload = bridge->image;
  packet.length = sizeof(bridge->image);
  packet.command = bridge->pending_command;
  if (bridge->waiting_continue) {
    packet.flags |= DEBUG_FLAG_RUNNING;
  }
  send_packet(bridge, &packet, &bridge->peer);
  bridge->last_send = now;
}

static bool queue_tcp(struct bridge *bridge, const uint8_t *bytes, size_t length)
{
  if (length > sizeof(bridge->tcp_queue) - bridge->tcp_length) {
    return false;
  }
  memcpy(bridge->tcp_queue + bridge->tcp_length, bytes, length);
  bridge->tcp_length += length;
  return true;
}

static bool queue_record(struct bridge *bridge, const uint8_t *bytes, size_t length,
                         uint64_t command)
{
  if (bridge->record_count == RECORD_COUNT) {
    close_client(bridge, "bounded request queue exhausted");
    return false;
  }
  struct record *record = &bridge->records[(bridge->record_head + bridge->record_count) % RECORD_COUNT];
  memcpy(record->bytes, bytes, length);
  record->length = length;
  record->command = command;
  ++bridge->record_count;
  return true;
}

static void start_record(struct bridge *bridge, uint64_t now)
{
  if (bridge->phase != BRIDGE_STOPPED || bridge->transmit_length || !bridge->record_count) {
    return;
  }
  if (bridge->tx_sequence == UINT64_MAX) {
    close_client(bridge, "transport sequence exhausted");
    return;
  }
  struct record *record = &bridge->records[bridge->record_head];
  struct debug_packet packet = packet_header(bridge, DEBUG_PACKET_DATA);
  packet.sequence = ++bridge->tx_sequence;
  packet.command = record->command;
  packet.payload = record->bytes;
  packet.length = record->length;
  bridge->transmit_length = debug_encode(bridge->transmit, &packet);
  bridge->record_head = (bridge->record_head + 1) % RECORD_COUNT;
  --bridge->record_count;
  send_bytes(bridge, bridge->transmit, bridge->transmit_length, &bridge->peer);
  bridge->last_send = now;
}

static void handle_record(struct bridge *bridge, const uint8_t *bytes, size_t length)
{
  if (bridge->phase == BRIDGE_RUNNING) {
    close_client(bridge, bytes[0] == 3 ?
        "running Ctrl+C is unavailable in task 3; target may still be running" :
        "new commands while running are unavailable; waiting for the next stop");
    return;
  }
  uint64_t command = bridge->pending_command;
  if (bytes[0] == '$' || bytes[0] == 3) {
    /* An RSP retry gets a new transport sequence, but keeps execution identity
     * until GDB acknowledges the final reply. Device reads may have effects. */
    if (command) {
      if (bridge->pending.length != length || memcmp(bridge->pending.bytes, bytes, length)) {
        close_client(bridge, "a different request arrived before the prior reply was acknowledged");
        return;
      }
    } else {
      if (bridge->next_command == UINT64_MAX) {
        close_client(bridge, "command identity exhausted");
        return;
      }
      command = bridge->pending_command = ++bridge->next_command;
      memcpy(bridge->pending.bytes, bytes, length);
      bridge->pending.length = length;
    }
  } else if (bytes[0] == '+' && bridge->reply_count) {
    struct record *reply = &bridge->replies[bridge->reply_head];
    bool final = !rsp_console(reply->bytes, reply->length);
    uint64_t reply_command = reply->command;
    if (final && reply_command == bridge->pending_command) {
      bridge->completed = *reply;
      bridge->pending_command = 0;
    }
    bridge->reply_head = (bridge->reply_head + 1) % RECORD_COUNT;
    --bridge->reply_count;
  } else if (bytes[0] == '-' && bridge->reply_count) {
    bridge->reply_retry[bridge->reply_head] = true;
  }
  queue_record(bridge, bytes, length, command);
}

static void receive_tcp(struct bridge *bridge)
{
  uint8_t bytes[2048];
  ssize_t count = recv(bridge->client, bytes, sizeof(bytes), 0);
  if (count <= 0) {
    if (!count || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
      close_client(bridge, "client disconnected; ordinary stop can expire after idle loss");
    }
    return;
  }
  for (ssize_t i = 0; i < count && bridge->client >= 0; ++i) {
    enum rsp_result result = rsp_feed(&bridge->parser, bytes[i]);
    if (result == RSP_RECORD) {
      handle_record(bridge, bridge->parser.bytes, bridge->parser.length);
    } else if (result == RSP_INVALID) {
      const uint8_t nak = '-';
      if (!queue_tcp(bridge, &nak, 1)) {
        close_client(bridge, "bounded TCP queue exhausted");
      }
    } else if (result == RSP_OVERFLOW) {
      close_client(bridge, "RSP record exceeds the 1024-byte encoded limit");
    }
    if (result != RSP_MORE) {
      memset(&bridge->parser, 0, sizeof(bridge->parser));
    }
  }
}

static bool same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
  return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void receive_offer(struct bridge *bridge, const struct debug_packet *packet,
                           const struct sockaddr_in *source, uint64_t now)
{
  size_t name_length = strlen(bridge->name);
  if (packet->length != DEBUG_OFFER_FIXED_BYTES + name_length ||
      packet->payload[10] != name_length || packet->payload[11] ||
      memcmp(packet->payload + DEBUG_OFFER_FIXED_BYTES, bridge->name, name_length) ||
      !(packet->flags & DEBUG_FLAG_TARGET) ||
      memcmp(packet->session, bridge->session, DEBUG_NONCE_BYTES) ||
      !debug_get16(packet->payload + 6) || packet->payload[9] > 1 ||
      !packet->generation || (packet->payload[0] & 1)) {
    return;
  }
  const uint8_t zero[DEBUG_NONCE_BYTES] = {0};
  if (!memcmp(packet->boot, zero, sizeof(zero)) || !memcmp(packet->payload, zero, 6) ||
      (bridge->filter && memcmp(packet->payload, bridge->filter_mac, 6))) {
    return;
  }
  if (bridge->phase != BRIDGE_DISCOVERING && memcmp(packet->payload, bridge->mac, 6)) {
    if (!bridge->ambiguous) {
      fprintf(stderr, "Duplicate target name %s from another MAC; selection requires --source\n", bridge->name);
    }
    bridge->ambiguous = true;
    if (bridge->client >= 0) {
      close_client(bridge, "duplicate target name from another MAC; use --source");
    }
    return;
  }
  if (memcmp(packet->payload + 12, bridge->image, DEBUG_IMAGE_BYTES)) {
    if (bridge->client >= 0) {
      close_client(bridge, "target image differs from --kernel; no commands sent to this image");
    }
    return;
  }
  if (bridge->phase != BRIDGE_DISCOVERING) {
    bool new_boot = memcmp(packet->boot, bridge->boot, DEBUG_NONCE_BYTES);
    bool new_stop = packet->generation != bridge->generation;
    if (bridge->client >= 0 && (new_boot ||
        (new_stop && bridge->phase != BRIDGE_RUNNING))) {
      close_client(bridge, "target boot or stop changed while attached; no automatic session switch");
      return;
    }
    if (!new_boot && !new_stop && bridge->phase != BRIDGE_RUNNING) {
      if (bridge->phase == BRIDGE_OFFERED) {
        bridge->last_seen = now;
      }
      return;
    }
    if (bridge->phase == BRIDGE_RUNNING && (!new_stop || new_boot)) {
      return;
    }
  }
  memcpy(bridge->mac, packet->payload, sizeof(bridge->mac));
  memcpy(bridge->boot, packet->boot, sizeof(bridge->boot));
  bridge->generation = packet->generation;
  bridge->terminal = packet->payload[9];
  bridge->peer = *source;
  bridge->phase = BRIDGE_OFFERED;
  bridge->ambiguous = false;
  bridge->discovery_deadline = now + DISCOVERY_MS;
  bridge->last_seen = now;
  bridge->tx_sequence = bridge->rx_sequence = 0;
  bridge->transmit_length = 0;
  fprintf(stderr, "Found %s at %s, MAC %02x:%02x:%02x:%02x:%02x:%02x, %u CPUs, reason %u%s, generation %" PRIu64 "\n",
      bridge->name, inet_ntoa(source->sin_addr), bridge->mac[0], bridge->mac[1],
      bridge->mac[2], bridge->mac[3], bridge->mac[4], bridge->mac[5],
      debug_get16(packet->payload + 6), packet->payload[8], bridge->terminal ? " (terminal)" : "", bridge->generation);
}

static void receive_udp(struct bridge *bridge, uint64_t now)
{
  uint8_t bytes[DEBUG_DATAGRAM_BYTES + 1];
  struct sockaddr_in source;
  socklen_t source_length = sizeof(source);
  ssize_t size = recvfrom(bridge->udp, bytes, sizeof(bytes), 0,
                          (struct sockaddr *)&source, &source_length);
  struct debug_packet packet;
  if (size < 0 || source_length != sizeof(source) || source.sin_family != AF_INET ||
      !debug_decode(bytes, size, &packet) || packet.flags != DEBUG_FLAG_TARGET) {
    return;
  }
  if (packet.kind == DEBUG_OFFER) {
    if (packet.length >= DEBUG_OFFER_FIXED_BYTES) {
      receive_offer(bridge, &packet, &source, now);
    }
    return;
  }
  if (bridge->client < 0 || !same_peer(&source, &bridge->peer) ||
      memcmp(packet.boot, bridge->boot, DEBUG_NONCE_BYTES) ||
      memcmp(packet.session, bridge->session, DEBUG_NONCE_BYTES) ||
      packet.generation != bridge->generation) {
    return;
  }
  if (packet.kind == DEBUG_BOUND && bridge->phase == BRIDGE_BINDING &&
      !packet.length && !packet.sequence && !packet.ack) {
    bridge->phase = BRIDGE_STOPPED;
    bridge->waiting_continue = false;
    bridge->last_seen = now;
    bridge->last_heartbeat = 0;
    fprintf(stderr, "Image-bound debugger session established\n");
    return;
  }
  if (bridge->phase != BRIDGE_STOPPED && bridge->phase != BRIDGE_RUNNING) {
    return;
  }
  if (packet.ack > bridge->tx_sequence) {
    return;
  }
  bool valid = false;
  size_t reply_offset = packet.length > 1 &&
      (packet.payload[0] == '+' || packet.payload[0] == '-') ? 1 : 0;
  const uint8_t *reply_bytes = packet.payload + reply_offset;
  size_t reply_length = packet.length - reply_offset;
  /* The reply can race its RSP acknowledgement on the independent UDP path.
   * Accept a retained result after local retirement without replaying it to GDB. */
  bool completed_reply = bridge->completed.command &&
      packet.command == bridge->completed.command &&
      ((packet.length == 1 && (packet.payload[0] == '+' || packet.payload[0] == '-')) ||
       (reply_length == bridge->completed.length &&
        !memcmp(reply_bytes, bridge->completed.bytes, reply_length)));
  if (bridge->phase == BRIDGE_STOPPED && packet.kind == DEBUG_PACKET_DATA &&
      packet.sequence && rsp_valid(packet.payload, packet.length) &&
      ((bridge->pending_command && packet.command == bridge->pending_command) || completed_reply)) {
    if (packet.sequence == bridge->rx_sequence + 1 && bridge->rx_sequence != UINT64_MAX) {
      bool new_reply = reply_length && reply_bytes[0] == '$';
      bool forward = !completed_reply;
      size_t retry_index = RECORD_COUNT;
      for (size_t i = 0; new_reply && i < bridge->reply_count; ++i) {
        size_t index = (bridge->reply_head + i) % RECORD_COUNT;
        struct record *reply = &bridge->replies[index];
        if (reply->command == packet.command && reply->length == reply_length &&
            !memcmp(reply->bytes, reply_bytes, reply_length)) {
          new_reply = false;
          forward = bridge->reply_retry[index];
          retry_index = index;
        }
      }
      if (completed_reply) {
        new_reply = false;
      }
      if ((new_reply && bridge->reply_count == RECORD_COUNT) ||
          (forward && !queue_tcp(bridge, packet.payload, packet.length))) {
        return;
      }
      if (retry_index != RECORD_COUNT) {
        bridge->reply_retry[retry_index] = false;
      }
      bridge->rx_sequence = packet.sequence;
      if (new_reply) {
        size_t index = (bridge->reply_head + bridge->reply_count) % RECORD_COUNT;
        struct record *reply = &bridge->replies[index];
        memcpy(reply->bytes, reply_bytes, reply_length);
        reply->length = reply_length;
        reply->command = packet.command;
        bridge->reply_retry[index] = false;
        ++bridge->reply_count;
      }
    } else if (packet.sequence != bridge->rx_sequence) {
      return;
    }
    /* Receipt means that bounded host storage owns the complete record;
     * GDB's later '+' is a separate RSP operation. */
    struct debug_packet ack = packet_header(bridge, DEBUG_ACK);
    send_packet(bridge, &ack, &bridge->peer);
    valid = true;
  } else if (bridge->phase == BRIDGE_STOPPED &&
             (packet.kind == DEBUG_ACK || packet.kind == DEBUG_HEARTBEAT) &&
             !packet.length && !packet.sequence) {
    valid = true;
  } else if (packet.kind == DEBUG_RELEASED && packet.length == 1 && packet.sequence &&
             packet.payload[0] <= DEBUG_RELEASE_IDLE &&
             (packet.payload[0] == DEBUG_RELEASE_IDLE ? !packet.command :
              bridge->pending_command && packet.command == bridge->pending_command) &&
             (packet.sequence == bridge->rx_sequence ||
              (bridge->rx_sequence != UINT64_MAX && packet.sequence == bridge->rx_sequence + 1))) {
    struct debug_packet ack = packet_header(bridge, DEBUG_ACK);
    ack.ack = packet.sequence;
    send_packet(bridge, &ack, &bridge->peer);
    if (packet.sequence == bridge->rx_sequence) {
      return;
    }
    bridge->rx_sequence = packet.sequence;
    if (packet.payload[0] == DEBUG_RELEASE_IDLE) {
      close_client(bridge, "kernel reported idle release");
    } else if (bridge->terminal || !bridge->pending_command) {
      close_client(bridge, "unexpected release of terminal stop or absent continue");
    } else {
      bridge->phase = BRIDGE_RUNNING;
      bridge->waiting_continue = true;
      bridge->transmit_length = 0;
      bridge->record_count = bridge->record_head = 0;
      bridge->last_hello = 0;
      fprintf(stderr, "Kernel continued; waiting for another stop in this boot/image. Running Ctrl+C is unavailable.\n");
    }
    return;
  }
  if (!valid) {
    return;
  }
  bridge->last_seen = now;
  if (bridge->transmit_length && packet.ack == bridge->tx_sequence) {
    bridge->transmit_length = 0;
  }
}

static void accept_client(struct bridge *bridge)
{
  int fd = accept(bridge->listener, NULL, NULL);
  if (fd < 0) {
    return;
  }
  if (bridge->client >= 0 || !nonblocking(fd)) {
    close(fd);
    return;
  }
  bridge->client = fd;
  if (bridge->ambiguous) {
    close_client(bridge, "duplicate target name; restart with --source MAC");
    return;
  }
  bridge->last_hello = 0;
  fprintf(stderr, "GDB connected; waiting for a verified named target\n");
}

static void timers(struct bridge *bridge, uint64_t now)
{
  if (bridge->phase == BRIDGE_OFFERED && bridge->client < 0 &&
      now - bridge->last_seen >= DEBUG_IDLE_MS) {
    close_client(bridge, "discovery offer expired");
  }
  if (bridge->phase == BRIDGE_DISCOVERING || bridge->phase == BRIDGE_OFFERED ||
      bridge->phase == BRIDGE_RUNNING) {
    if (!bridge->last_hello || now - bridge->last_hello >= HELLO_MS) {
      send_hello(bridge, now);
    }
  }
  if (bridge->phase == BRIDGE_OFFERED && bridge->client >= 0 &&
      !bridge->ambiguous && now >= bridge->discovery_deadline) {
    bridge->phase = BRIDGE_BINDING;
    send_bind(bridge, now);
  } else if (bridge->phase == BRIDGE_BINDING && now - bridge->last_send >= DEBUG_RETRY_MS) {
    send_bind(bridge, now);
  }
  if ((bridge->phase == BRIDGE_STOPPED || bridge->phase == BRIDGE_BINDING) &&
      now - bridge->last_seen >= DEBUG_IDLE_MS) {
    close_client(bridge, bridge->terminal ?
        "target link lost; terminal stop remains stopped" :
        "target link lost; ordinary stop may have resumed after idle loss");
    return;
  }
  if (bridge->phase == BRIDGE_STOPPED && bridge->client >= 0) {
    if (!bridge->last_heartbeat || now - bridge->last_heartbeat >= DEBUG_HEARTBEAT_MS) {
      struct debug_packet packet = packet_header(bridge, DEBUG_HEARTBEAT);
      send_packet(bridge, &packet, &bridge->peer);
      bridge->last_heartbeat = now;
    }
    if (bridge->transmit_length && now - bridge->last_send >= DEBUG_RETRY_MS) {
      send_bytes(bridge, bridge->transmit, bridge->transmit_length, &bridge->peer);
      bridge->last_send = now;
    }
    start_record(bridge, now);
  }
}

int main(int argc, char **argv)
{
  static struct bridge bridge = {.udp = -1, .listener = -1, .client = -1};
  const char *kernel = NULL, *bind_address = "0.0.0.0", *beacon = "255.255.255.255";
  unsigned port = DEBUG_PORT;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--help")) {
      usage(stdout);
      return 0;
    }
    if (i + 1 == argc) {
      usage(stderr);
      return 1;
    }
    const char *option = argv[i++], *value = argv[i];
    if (!strcmp(option, "--target")) {
      bridge.name = value;
    } else if (!strcmp(option, "--kernel")) {
      kernel = value;
    } else if (!strcmp(option, "--bind")) {
      bind_address = value;
    } else if (!strcmp(option, "--beacon-address")) {
      beacon = value;
    } else if (!strcmp(option, "--udp-port")) {
      char *end;
      unsigned long number = strtoul(value, &end, 10);
      if (!*value || *end || number < 1 || number > UINT16_MAX) {
        fprintf(stderr, "Invalid --udp-port\n");
        return 1;
      }
      port = number;
    } else if (!strcmp(option, "--source")) {
      if (!parse_mac(value, bridge.filter_mac)) {
        fprintf(stderr, "Invalid unicast --source MAC\n");
        return 1;
      }
      bridge.filter = true;
    } else {
      usage(stderr);
      return 1;
    }
  }
  if (!bridge.name || !kernel || !strlen(bridge.name) || strlen(bridge.name) > DEBUG_NAME_BYTES) {
    usage(stderr);
    return 1;
  }
  for (const char *p = bridge.name; *p; ++p) {
    if ((unsigned char)*p < 33 || (unsigned char)*p > 126) {
      fprintf(stderr, "Target name must contain 1-63 printable non-space ASCII bytes\n");
      return 1;
    }
  }
  bridge.beacon.sin_family = AF_INET;
  bridge.beacon.sin_port = htons(port);
  if (inet_pton(AF_INET, beacon, &bridge.beacon.sin_addr) != 1) {
    fprintf(stderr, "Invalid numeric --beacon-address IPv4\n");
    return 1;
  }
  if (!hash_kernel(kernel, bridge.image) || !random_session(bridge.session) ||
      !open_sockets(&bridge, bind_address, port)) {
    return 1;
  }
  struct sigaction action = {.sa_handler = signal_handler};
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  signal(SIGPIPE, SIG_IGN);
  fprintf(stderr, "Listening for GDB at 127.0.0.1:%u; target %s, exact ELF SHA-256 ", GDB_PORT, bridge.name);
  for (size_t i = 0; i < sizeof(bridge.image); ++i) {
    fprintf(stderr, "%02x", bridge.image[i]);
  }
  fputc('\n', stderr);
  while (!interrupted) {
    timers(&bridge, monotonic_ms());
    struct pollfd fds[3] = {
      {.fd = bridge.udp, .events = POLLIN},
      {.fd = bridge.listener, .events = POLLIN},
      {.fd = bridge.client, .events = POLLIN | (bridge.tcp_length ? POLLOUT : 0)},
    };
    int result = poll(fds, 3, 50);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("poll");
      break;
    }
    if (fds[0].revents & POLLIN) {
      receive_udp(&bridge, monotonic_ms());
    }
    if (fds[1].revents & POLLIN) {
      accept_client(&bridge);
    }
    if (bridge.client >= 0 && bridge.client == fds[2].fd) {
      if (fds[2].revents & POLLIN) {
        receive_tcp(&bridge);
      }
      if (bridge.client >= 0 && fds[2].revents & POLLOUT) {
        ssize_t sent = send(bridge.client, bridge.tcp_queue, bridge.tcp_length, 0);
        if (sent > 0) {
          bridge.tcp_length -= sent;
          memmove(bridge.tcp_queue, bridge.tcp_queue + sent, bridge.tcp_length);
        } else if (sent < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
          close_client(&bridge, "TCP write failed");
        }
      }
      if (bridge.client >= 0 && fds[2].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        close_client(&bridge, "TCP connection lost");
      }
    }
  }
  if (bridge.client >= 0) {
    close(bridge.client);
  }
  close(bridge.udp);
  close(bridge.listener);
  return 0;
}

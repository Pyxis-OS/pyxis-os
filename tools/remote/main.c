#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "buffer.h"
#include "presentation.h"
#include <remote/terminal.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_CAPACITY 65536u
#define INPUT_ESCAPE_CAPACITY 32u
#define INPUT_ESCAPE_TIMEOUT_MS 100
#define CONNECT_TIMEOUT_MS 10000
#define LOCAL_CLOSE_TIMEOUT_MS 5000
/* One CSI erase can touch every row. Leave room for its bounded row commands. */
#define RENDER_BYTE_RESERVE 16384u

static volatile sig_atomic_t interrupted;
static struct termios saved_termios;
static bool terminal_raw;
static bool terminal_screen;
static int stdin_flags = -1;
static int stdout_flags = -1;

struct client {
  int socket;
  bool machine;
  bool ready;
  bool input_ended;
  bool closing;
  int64_t close_deadline;
  bool rejected;
  bool final;
  int result;
  char diagnostic[256];
  struct byte_buffer outgoing;
  struct byte_buffer output;
  struct presentation screen;
  unsigned char incoming[REMOTE_HEADER_SIZE + REMOTE_PAYLOAD_MAX];
  size_t received;
  size_t frame_size;
  size_t presented;
  unsigned char input_escape[INPUT_ESCAPE_CAPACITY];
  size_t input_escape_length;
  int64_t input_escape_deadline;
};

static int64_t monotonic_ms(void)
{
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) {
    return 0;
  }
  return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void signal_handler(int signal_number)
{
  interrupted = signal_number;
}

static void restore_terminal(void)
{
  if (terminal_raw) {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    terminal_raw = false;
  }
  if (terminal_screen) {
    const char restore[] = "\x1b[0m\x1b[r\x1b[?7h\x1b[?25h\x1b[0 q\x1b[?1049l";
    size_t offset = 0;
    /* A full output queue must never prevent restoration of cooked input. */
    if (stdout_flags >= 0 && fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK) == 0) {
      for (unsigned attempt = 0; attempt < 8 && offset < sizeof(restore) - 1; ++attempt) {
        ssize_t count = write(STDOUT_FILENO, restore + offset, sizeof(restore) - 1 - offset);
        if (count > 0) {
          offset += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
          continue;
        } else {
          break;
        }
      }
    }
    terminal_screen = false;
  }
  if (stdout_flags >= 0) {
    fcntl(STDOUT_FILENO, F_SETFL, stdout_flags);
    stdout_flags = -1;
  }
  if (stdin_flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
    stdin_flags = -1;
  }
}

static int nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return -1;
  }
  return flags;
}

static int connect_host(const char *host, const char *port)
{
  struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
  struct addrinfo *addresses;
  int status = getaddrinfo(host, port, &hints, &addresses);
  if (status) {
    fprintf(stderr, "pyxis-remote: resolve: %s\n", gai_strerror(status));
    return -1;
  }
  int connected = -1;
  int saved_error = ECONNREFUSED;
  int64_t deadline = monotonic_ms() + CONNECT_TIMEOUT_MS;
  for (struct addrinfo *address = addresses; address && !interrupted; address = address->ai_next) {
    int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0) {
      saved_error = errno;
      continue;
    }
    if (nonblocking(fd) < 0) {
      saved_error = errno;
      close(fd);
      continue;
    }
    if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
      connected = fd;
      break;
    }
    saved_error = errno;
    while (saved_error == EINPROGRESS && !interrupted) {
      int64_t remaining = deadline - monotonic_ms();
      if (remaining <= 0) {
        saved_error = ETIMEDOUT;
        break;
      }
      struct pollfd interest = {.fd = fd, .events = POLLOUT};
      int count = poll(&interest, 1, (int)remaining);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        saved_error = count == 0 ? ETIMEDOUT : errno;
        break;
      }
      socklen_t length = sizeof(saved_error);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &saved_error, &length) < 0) {
        saved_error = errno;
      }
      if (!saved_error) {
        connected = fd;
      }
      break;
    }
    if (connected >= 0) {
      break;
    }
    close(fd);
  }
  freeaddrinfo(addresses);
  if (connected < 0 && !interrupted) {
    fprintf(stderr, "pyxis-remote: connect: %s\n", strerror(saved_error));
  }
  return connected;
}

static void queue_frame(struct client *client, uint32_t type, const void *payload, size_t length)
{
  unsigned char header[REMOTE_HEADER_SIZE];
  remote_encode_u32(header, type);
  remote_encode_u32(header + 4, (uint32_t)length);
  buffer_append(&client->outgoing, header, sizeof(header));
  if (length) {
    buffer_append(&client->outgoing, payload, length);
  }
}

static void output_text(struct client *client, const char *text)
{
  buffer_append(&client->output, text, strlen(text));
}

static void output_base64(struct client *client, const unsigned char *data, size_t length)
{
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  output_text(client, "{\"type\":\"output\",\"base64\":\"");
  for (size_t i = 0; i < length; i += 3) {
    uint32_t bits = (uint32_t)data[i] << 16;
    if (i + 1 < length) {
      bits |= (uint32_t)data[i + 1] << 8;
    }
    if (i + 2 < length) {
      bits |= data[i + 2];
    }
    char encoded[4] = {alphabet[bits >> 18], alphabet[(bits >> 12) & 63],
                       i + 1 < length ? alphabet[(bits >> 6) & 63] : '=',
                       i + 2 < length ? alphabet[bits & 63] : '='};
    buffer_append(&client->output, encoded, sizeof(encoded));
  }
  output_text(client, "\"}\n");
}

static const char *error_name(uint32_t code)
{
  switch (code) {
  case REMOTE_ERROR_BAD_FRAME: return "bad_frame";
  case REMOTE_ERROR_LAUNCH: return "launch";
  case REMOTE_ERROR_RESOURCE: return "resource";
  case REMOTE_ERROR_INTERNAL: return "internal";
  case REMOTE_ERROR_HELLO_TIMEOUT: return "hello_timeout";
  default: return NULL;
  }
}

static int frame_header(struct client *client)
{
  uint32_t type = remote_decode_u32(client->incoming);
  uint32_t length = remote_decode_u32(client->incoming + 4);
  bool valid = false;
  if (type == REMOTE_READY) {
    valid = !client->ready && !client->rejected && !length;
  } else if (type == REMOTE_ERROR) {
    valid = !client->rejected && length == REMOTE_ERROR_SIZE;
  } else if (client->ready) {
    switch (type) {
    case REMOTE_OUTPUT: valid = length && length <= REMOTE_PAYLOAD_MAX; break;
    case REMOTE_FRESH_LINE: valid = !length; break;
    case REMOTE_TAB_WIDTH: valid = length == REMOTE_TAB_WIDTH_SIZE; break;
    case REMOTE_FINAL: valid = length == REMOTE_FINAL_SIZE; break;
    }
  }
  if (!valid) {
    snprintf(client->diagnostic, sizeof(client->diagnostic),
             "invalid or out-of-order server frame");
    return -1;
  }
  client->frame_size = REMOTE_HEADER_SIZE + length;
  return 0;
}

static int present_frame(struct client *client)
{
  uint32_t type = remote_decode_u32(client->incoming);
  size_t length = client->frame_size - REMOTE_HEADER_SIZE;
  unsigned char *payload = client->incoming + REMOTE_HEADER_SIZE;
  char text[512];
  if (type == REMOTE_OUTPUT && !client->machine) {
    unsigned budget = 128;
    while (client->presented < length && budget-- &&
           buffer_space(&client->output) >= RENDER_BYTE_RESERVE) {
      presentation_data(&client->screen, payload + client->presented, 1);
      ++client->presented;
    }
    if (client->presented < length) {
      return 0;
    }
  } else {
    if (buffer_space(&client->output) < RENDER_BYTE_RESERVE) {
      return 0;
    }
    switch (type) {
    case REMOTE_READY:
      client->ready = true;
      if (client->machine) {
        output_text(client, "{\"type\":\"ready\"}\n");
      }
      break;
    case REMOTE_OUTPUT:
      output_base64(client, payload, length);
      break;
    case REMOTE_FRESH_LINE:
      if (client->machine) {
        output_text(client, "{\"type\":\"fresh_line\"}\n");
      } else {
        presentation_fresh_line(&client->screen);
      }
      break;
    case REMOTE_TAB_WIDTH: {
      uint64_t width = remote_decode_u64(payload);
      if (!width || width > 32) {
        return -1;
      }
      client->screen.tab_width = (unsigned)width;
      if (client->machine) {
        snprintf(text, sizeof(text), "{\"type\":\"tab_width\",\"columns\":%" PRIu64 "}\n", width);
        output_text(client, text);
      }
      break;
    }
    case REMOTE_ERROR: {
      uint32_t code = remote_decode_u32(payload);
      const char *name = error_name(code);
      if (!name) {
        return -1;
      }
      client->rejected = true;
      if (client->machine) {
        snprintf(text, sizeof(text), "{\"type\":\"error\",\"code\":%u,\"name\":\"%s\"}\n", code, name);
        output_text(client, text);
      } else {
        snprintf(client->diagnostic, sizeof(client->diagnostic), "server error: %s", name);
      }
      if (!client->ready) {
        client->final = true;
        client->result = 1;
      }
      break;
    }
    case REMOTE_FINAL: {
      uint32_t cause = remote_decode_u32(payload);
      uint32_t reason = remote_decode_u32(payload + 4);
      int32_t exit_status = (int32_t)remote_decode_u32(payload + 8);
      uint32_t drain = remote_decode_u32(payload + 12);
      if (cause < REMOTE_CAUSE_SHELL_EXIT || cause > REMOTE_CAUSE_SERVER_ERROR ||
          reason > REMOTE_PROCESS_TERMINATED ||
          (cause == REMOTE_CAUSE_SHELL_EXIT && !reason) ||
          (drain != REMOTE_DRAIN_COMPLETE && drain != REMOTE_DRAIN_TIMEOUT)) {
        return -1;
      }
      const char *cause_name = cause == REMOTE_CAUSE_SHELL_EXIT ? "shell_exit" :
                               cause == REMOTE_CAUSE_CLIENT_CLOSE ? "client_close" : "server_error";
      const char *drain_name = drain == REMOTE_DRAIN_COMPLETE ? "complete" : "timeout";
      const char *reason_name = reason == REMOTE_PROCESS_EXITED ? "exited" :
                                reason == REMOTE_PROCESS_FAULTED ? "faulted" :
                                reason == REMOTE_PROCESS_TERMINATED ? "terminated" : "absent";
      if (client->machine) {
        snprintf(text, sizeof(text), "{\"type\":\"final\",\"cause\":\"%s\",\"process_reason\":%u,"
                 "\"process_reason_name\":\"%s\",\"exit_status\":%" PRId32 ",\"drain\":\"%s\"}\n",
                 cause_name, reason, reason_name, exit_status, drain_name);
        output_text(client, text);
      } else if (cause != REMOTE_CAUSE_SHELL_EXIT || reason != REMOTE_PROCESS_EXITED ||
                 exit_status || drain != REMOTE_DRAIN_COMPLETE) {
        snprintf(client->diagnostic, sizeof(client->diagnostic),
                 "%s, process %s (status %" PRId32 "), drain %s",
                 cause_name, reason_name, exit_status, drain_name);
      }
      client->final = true;
      client->result = !client->rejected && cause == REMOTE_CAUSE_SHELL_EXIT &&
                       reason == REMOTE_PROCESS_EXITED && !exit_status &&
                       drain == REMOTE_DRAIN_COMPLETE ? 0 :
                       cause == REMOTE_CAUSE_CLIENT_CLOSE && client->closing &&
                       drain == REMOTE_DRAIN_COMPLETE ? 2 : 1;
      break;
    }
    }
  }
  client->received = 0;
  client->frame_size = REMOTE_HEADER_SIZE;
  client->presented = 0;
  return 0;
}

static void flush_input_escape(struct client *client)
{
  if (client->input_escape_length) {
    queue_frame(client, REMOTE_INPUT, client->input_escape, client->input_escape_length);
    client->input_escape_length = 0;
  }
}

static void input_byte(struct client *client, unsigned char byte)
{
  if (byte == 0x1d) {
    flush_input_escape(client);
    queue_frame(client, REMOTE_CLOSE, NULL, 0);
    client->closing = true;
    client->close_deadline = monotonic_ms() + LOCAL_CLOSE_TIMEOUT_MS;
    return;
  }
  if (byte == '\r') {
    byte = '\n';
  }
  if (!client->input_escape_length) {
    if (byte != 0x1b) {
      queue_frame(client, REMOTE_INPUT, &byte, 1);
      return;
    }
    client->input_escape[client->input_escape_length++] = byte;
    client->input_escape_deadline = monotonic_ms() + INPUT_ESCAPE_TIMEOUT_MS;
    return;
  }
  if (client->input_escape_length == INPUT_ESCAPE_CAPACITY) {
    flush_input_escape(client);
    input_byte(client, byte);
    return;
  }
  client->input_escape[client->input_escape_length++] = byte;
  size_t length = client->input_escape_length;
  unsigned char *escape = client->input_escape;
  if (length == 2 && byte != '[' && byte != 'O') {
    flush_input_escape(client);
  } else if (length >= 3 && byte >= 0x40 && byte <= 0x7e) {
    char command = (char)byte;
    if (escape[1] == 'O' && length == 3 && strchr("ABCDHF", command)) {
      escape[1] = '[';
    } else if (escape[1] == '[' && byte == '~' && length == 4 &&
               (escape[2] == '1' || escape[2] == '7' || escape[2] == '4' || escape[2] == '8')) {
      escape[2] = escape[2] == '1' || escape[2] == '7' ? 'H' : 'F';
      client->input_escape_length = 3;
    } else if (escape[1] == '[' && strchr("ABCDHF", command)) {
      /* Host modifier forms retain their navigation meaning. */
      bool numeric = true;
      for (size_t i = 2; i + 1 < length; ++i) {
        if ((escape[i] < '0' || escape[i] > '9') && escape[i] != ';') {
          numeric = false;
        }
      }
      if (numeric) {
        escape[2] = (unsigned char)command;
        client->input_escape_length = 3;
      }
    }
    flush_input_escape(client);
  }
}

static int read_input(struct client *client)
{
  unsigned char data[REMOTE_PAYLOAD_MAX];
  size_t capacity = client->machine ? sizeof(data) : 512;
  ssize_t count = read(STDIN_FILENO, data, capacity);
  if (count < 0) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
  }
  if (!count) {
    flush_input_escape(client);
    queue_frame(client, REMOTE_END_INPUT, NULL, 0);
    client->input_ended = true;
  } else if (client->machine) {
    queue_frame(client, REMOTE_INPUT, data, (size_t)count);
  } else {
    for (ssize_t i = 0; i < count && !client->closing; ++i) {
      input_byte(client, data[i]);
    }
  }
  return 0;
}

static int write_buffer(int fd, struct byte_buffer *buffer, bool socket_output)
{
  size_t length = buffer->length < REMOTE_PAYLOAD_MAX ? buffer->length : REMOTE_PAYLOAD_MAX;
  ssize_t count = socket_output ? send(fd, buffer->data + buffer->start, length, MSG_NOSIGNAL) :
                                 write(fd, buffer->data + buffer->start, length);
  if (count > 0) {
    buffer_consume(buffer, (size_t)count);
    return 0;
  }
  return count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

static int run_client(struct client *client)
{
  for (;;) {
    if (interrupted) {
      return 128 + interrupted;
    }
    if (client->final && !client->output.length) {
      return client->result;
    }
    if (client->closing && monotonic_ms() >= client->close_deadline) {
      snprintf(client->diagnostic, sizeof(client->diagnostic), "%s",
               client->final ? "local close acknowledged; stdout did not drain before deadline" :
                               "local close not acknowledged before deadline; disconnecting");
      return 1;
    }
    if (client->received == client->frame_size && present_frame(client) < 0) {
      snprintf(client->diagnostic, sizeof(client->diagnostic), "invalid server payload");
      return 1;
    }
    if (client->final && !client->output.length) {
      return client->result;
    }
    if (client->input_escape_length && monotonic_ms() >= client->input_escape_deadline &&
        buffer_space(&client->outgoing) >= INPUT_ESCAPE_CAPACITY + REMOTE_HEADER_SIZE) {
      flush_input_escape(client);
    }
    bool can_read_input = client->ready && !client->rejected && !client->final &&
                          !client->input_ended && !client->closing &&
                          buffer_space(&client->outgoing) >= 8192;
    bool can_read_socket = !client->final && client->received < client->frame_size;
    short socket_events = (can_read_socket ? POLLIN : 0) |
                          (!client->final && client->outgoing.length ? POLLOUT : 0);
    struct pollfd interests[3] = {
      {.fd = socket_events ? client->socket : -1, .events = socket_events},
      {.fd = can_read_input ? STDIN_FILENO : -1, .events = POLLIN},
      {.fd = client->output.length ? STDOUT_FILENO : -1, .events = POLLOUT}
    };
    /* A buffered DATA frame must make progress even with no fresh readiness. */
    int timeout = client->received == client->frame_size &&
                  buffer_space(&client->output) >= RENDER_BYTE_RESERVE ? 0 : -1;
    if (client->input_escape_length &&
        buffer_space(&client->outgoing) >= INPUT_ESCAPE_CAPACITY + REMOTE_HEADER_SIZE) {
      int64_t remaining = client->input_escape_deadline - monotonic_ms();
      int escape_timeout = remaining > 0 ? (int)remaining : 0;
      if (timeout < 0 || escape_timeout < timeout) {
        timeout = escape_timeout;
      }
    }
    if (client->closing) {
      int64_t remaining = client->close_deadline - monotonic_ms();
      int close_timeout = remaining > 0 ? (int)remaining : 0;
      if (timeout < 0 || close_timeout < timeout) {
        timeout = close_timeout;
      }
    }
    int count = poll(interests, 3, timeout);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      snprintf(client->diagnostic, sizeof(client->diagnostic), "poll: %s", strerror(errno));
      return 1;
    }
    if (interests[2].revents && write_buffer(STDOUT_FILENO, &client->output, false) < 0) {
      snprintf(client->diagnostic, sizeof(client->diagnostic), "stdout: %s", strerror(errno));
      return 1;
    }
    if (interests[0].revents & POLLOUT) {
      if (write_buffer(client->socket, &client->outgoing, true) < 0) {
        snprintf(client->diagnostic, sizeof(client->diagnostic), "send: %s", strerror(errno));
        return 1;
      }
    }
    if (interests[1].revents && read_input(client) < 0) {
      snprintf(client->diagnostic, sizeof(client->diagnostic), "stdin: %s", strerror(errno));
      return 1;
    }
    if (can_read_socket && (interests[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      ssize_t received = recv(client->socket, client->incoming + client->received,
                              client->frame_size - client->received, 0);
      if (received > 0) {
        client->received += (size_t)received;
        if (client->received == REMOTE_HEADER_SIZE && frame_header(client) < 0) {
          return 1;
        }
      } else if (!received) {
        snprintf(client->diagnostic, sizeof(client->diagnostic),
                 "disconnected before final acknowledgment");
        client->final = true;
        client->result = 1;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        snprintf(client->diagnostic, sizeof(client->diagnostic), "receive: %s", strerror(errno));
        return 1;
      }
    }
  }
}

static unsigned dimension(const char *text, unsigned maximum)
{
  char *end;
  errno = 0;
  unsigned long value = strtoul(text, &end, 10);
  if (errno || !*text || *end || !value || value > maximum) {
    return 0;
  }
  return (unsigned)value;
}

int main(int argc, char **argv)
{
  bool machine = false;
  unsigned columns = 0;
  unsigned rows = 0;
  int argument = 1;
  for (; argument < argc && argv[argument][0] == '-'; ++argument) {
    if (!strcmp(argv[argument], "--machine")) {
      machine = true;
    } else if ((!strcmp(argv[argument], "--columns") || !strcmp(argv[argument], "--rows")) &&
               argument + 1 < argc) {
      bool column = !strcmp(argv[argument], "--columns");
      unsigned value = dimension(argv[++argument], column ? REMOTE_COLUMNS_MAX : REMOTE_ROWS_MAX);
      if (!value) {
        fprintf(stderr, "pyxis-remote: invalid terminal dimension\n");
        return 1;
      }
      if (column) {
        columns = value;
      } else {
        rows = value;
      }
    } else {
      break;
    }
  }
  if (argc - argument != 2 || !dimension(argv[argument + 1], 65535)) {
    fprintf(stderr, "usage: pyxis-remote [--machine] [--columns N] [--rows N] HOST PORT\n");
    return 1;
  }
  if (!machine) {
    struct winsize size;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) < 0 || !size.ws_col || !size.ws_row) {
      fprintf(stderr, "pyxis-remote: interactive mode requires a terminal; use --machine for pipes\n");
      return 1;
    }
    if (!columns) {
      columns = size.ws_col < REMOTE_COLUMNS_MAX ? size.ws_col : REMOTE_COLUMNS_MAX;
    }
    if (!rows) {
      rows = size.ws_row < REMOTE_ROWS_MAX ? size.ws_row : REMOTE_ROWS_MAX;
    }
    if (columns > size.ws_col || rows > size.ws_row) {
      fprintf(stderr, "pyxis-remote: dimensions exceed the host terminal\n");
      return 1;
    }
  } else {
    columns = columns ? columns : 80;
    rows = rows ? rows : 24;
  }
  struct sigaction action = {.sa_handler = signal_handler};
  sigemptyset(&action.sa_mask);
  const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
  for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
    sigaction(signals[i], &action, NULL);
  }
  signal(SIGPIPE, SIG_IGN);
  atexit(restore_terminal);
  int socket_fd = connect_host(argv[argument], argv[argument + 1]);
  if (socket_fd < 0) {
    return interrupted ? 128 + interrupted : 1;
  }
  if (!machine) {
    if (tcgetattr(STDIN_FILENO, &saved_termios) < 0) {
      perror("pyxis-remote: terminal attributes");
      close(socket_fd);
      return 1;
    }
    struct termios raw = saved_termios;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
      perror("pyxis-remote: raw terminal");
      close(socket_fd);
      return 1;
    }
    terminal_raw = true;
  }
  /* Both descriptors may share one open file description. Save both before
   * changing either, so restoration does not preserve our O_NONBLOCK flag. */
  stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
  stdout_flags = fcntl(STDOUT_FILENO, F_GETFL);
  if (stdin_flags < 0 || stdout_flags < 0 ||
      fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) < 0 ||
      fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK) < 0) {
    int failure = errno;
    restore_terminal();
    errno = failure;
    perror("pyxis-remote: nonblocking standard streams");
    close(socket_fd);
    return 1;
  }
  unsigned char outgoing[BUFFER_CAPACITY];
  unsigned char output[BUFFER_CAPACITY];
  struct client client = {
    .socket = socket_fd, .machine = machine, .frame_size = REMOTE_HEADER_SIZE,
    .outgoing = {.data = outgoing, .capacity = sizeof(outgoing)},
    .output = {.data = output, .capacity = sizeof(output)}
  };
  client.screen.columns = columns;
  client.screen.rows = rows;
  client.screen.output = &client.output;
  if (!machine) {
    presentation_begin(&client.screen);
    terminal_screen = true;
  }
  unsigned char hello[REMOTE_HELLO_SIZE];
  remote_encode_u32(hello, columns);
  remote_encode_u32(hello + 4, rows);
  queue_frame(&client, REMOTE_HELLO, hello, sizeof(hello));
  int result = run_client(&client);
  close(socket_fd);
  restore_terminal();
  if (client.diagnostic[0]) {
    fprintf(stderr, "pyxis-remote: %s\n", client.diagnostic);
  }
  return result;
}

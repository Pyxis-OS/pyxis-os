#define _POSIX_C_SOURCE 200809L

#include <remote/log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOG_SOURCE_MAX 32u
#define LOG_SEQUENCE_WINDOW 256u

struct text_cursor {
  bool present;
  bool valid;
  uint64_t sequence;
  uint64_t line;
  uint64_t offset;
};

struct log_source {
  bool present;
  bool received;
  unsigned char mac[6];
  uint64_t boot;
  uint64_t touched;
  uint64_t highest;
  uint64_t seen[LOG_SEQUENCE_WINDOW];
  bool seen_valid[LOG_SEQUENCE_WINDOW];
  struct text_cursor normal;
  struct text_cursor fatal;
};

static volatile sig_atomic_t interrupted;
static struct log_source sources[LOG_SOURCE_MAX];
static uint64_t receive_count;
static bool multiple_sources;

static void interrupt_handler(int signal_number)
{
  (void)signal_number;
  interrupted = 1;
}

static void usage(FILE *output)
{
  fprintf(output,
      "Usage: pyxis-log [--bind IPv4] [--source MAC]\n"
      "Receive Caelum kernel log UDP broadcasts on port %u.\n"
      "  --bind IPv4   Numeric local IPv4 address (default 0.0.0.0).\n"
      "  --source MAC  Accept only this unicast MAC, e.g. 02:00:00:00:00:01.\n"
      "  --help        Show this help.\n"
      "Text goes to stdout immediately; stream identity, gaps and late-packet\n"
      "notices go to stderr. Control bytes except newline/tab print as \\xHH.\n"
      "Normal and fatal cursors are independent. Packets remain in arrival\n"
      "order; duplicates within the latest %u sequences are suppressed.\n"
      "Older packets may repeat. Up to %u MAC sources are tracked; a changed\n"
      "boot stamp resets that MAC's state. These fields do not authenticate\n"
      "senders, and the sampled boot stamp is not guaranteed unique.\n"
      "Use log.udp=1 on Pyxis and a trusted LAN permitting IPv4 broadcast.\n",
      LOG_UDP_PORT, LOG_SEQUENCE_WINDOW, LOG_SOURCE_MAX);
}

static int hex_digit(unsigned char character)
{
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

static bool parse_mac(const char *text, unsigned char mac[6])
{
  if (strlen(text) != 17) {
    return false;
  }
  for (size_t i = 0; i < 6; ++i) {
    int high = hex_digit((unsigned char)text[i * 3]);
    int low = hex_digit((unsigned char)text[i * 3 + 1]);
    if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != ':')) {
      return false;
    }
    mac[i] = (unsigned char)(high * 16 + low);
  }
  return log_udp_mac_valid(mac);
}

static void source_label(const struct log_source *source)
{
  fprintf(stderr, "pyxis-log [%02x:%02x:%02x:%02x:%02x:%02x boot=%016" PRIx64 "]: ",
      source->mac[0], source->mac[1], source->mac[2], source->mac[3],
      source->mac[4], source->mac[5], source->boot);
}

static struct log_source *find_source(const struct log_udp_fragment *fragment)
{
  struct log_source *selected = NULL;
  for (size_t i = 0; i < LOG_SOURCE_MAX; ++i) {
    if (sources[i].present && !memcmp(sources[i].mac, fragment->mac, 6)) {
      selected = &sources[i];
      break;
    }
  }
  if (selected && selected->boot == fragment->boot) {
    selected->touched = ++receive_count;
    return selected;
  }
  if (selected) {
    source_label(selected);
    fprintf(stderr, "boot stamp changed to %016" PRIx64 "; resetting stream\n",
        fragment->boot);
  } else {
    for (size_t i = 0; i < LOG_SOURCE_MAX; ++i) {
      if (!sources[i].present) {
        selected = &sources[i];
        break;
      }
    }
    if (!selected) {
      selected = &sources[0];
      for (size_t i = 1; i < LOG_SOURCE_MAX; ++i) {
        if (sources[i].touched < selected->touched) {
          selected = &sources[i];
        }
      }
      source_label(selected);
      fprintf(stderr, "source limit reached; evicting duplicate/cursor state\n");
    }
    if (receive_count) {
      multiple_sources = true;
    }
  }
  memset(selected, 0, sizeof(*selected));
  selected->present = true;
  memcpy(selected->mac, fragment->mac, 6);
  selected->boot = fragment->boot;
  selected->touched = ++receive_count;
  source_label(selected);
  fprintf(stderr, "stream begins at packet %" PRIu64 "%s\n", fragment->sequence,
      fragment->flags & LOG_UDP_FLAG_START ? " (start)" : "");
  return selected;
}

static bool accept_sequence(struct log_source *source,
    const struct log_udp_fragment *fragment)
{
  uint64_t sequence = fragment->sequence;
  size_t slot = sequence % LOG_SEQUENCE_WINDOW;
  bool older = source->received && sequence < source->highest;
  bool outside = older && source->highest - sequence >= LOG_SEQUENCE_WINDOW;
  if (!outside && source->seen_valid[slot] && source->seen[slot] == sequence) {
    return false;
  }
  if (older) {
    source_label(source);
    fprintf(stderr, "late packet %" PRIu64 " after %" PRIu64 "%s\n", sequence,
        source->highest, outside ? "; outside duplicate window, may repeat" : "");
  } else {
    uint64_t expected = source->received ? source->highest + 1 : 0;
    if (sequence > expected) {
      source_label(source);
      fprintf(stderr, "packet gap %" PRIu64 "..%" PRIu64
          " (lost or interrupted staging; late arrival remains possible)\n",
          expected, sequence - 1);
    }
    source->highest = sequence;
    source->received = true;
  }
  if (!outside) {
    source->seen[slot] = sequence;
    source->seen_valid[slot] = true;
  }
  return true;
}

static void update_cursor(struct log_source *source,
    const struct log_udp_fragment *fragment, const unsigned char *text)
{
  bool fatal = fragment->flags & LOG_UDP_FLAG_FATAL;
  struct text_cursor *cursor = fatal ? &source->fatal : &source->normal;
  if (cursor->present && fragment->sequence < cursor->sequence) {
    return;
  }
  uint64_t expected_line = cursor->present ? cursor->line : 0;
  uint64_t expected_offset = cursor->present ? cursor->offset : 0;
  if ((!cursor->present || cursor->valid) &&
      (fragment->line != expected_line || fragment->offset != expected_offset)) {
    source_label(source);
    fprintf(stderr, "%s fragment discontinuity: expected %" PRIu64 ":%" PRIu64
        ", received %" PRIu64 ":%" PRIu64 " at packet %" PRIu64 "\n",
        fatal ? "fatal" : "normal", expected_line, expected_offset,
        fragment->line, fragment->offset, fragment->sequence);
  }
  cursor->present = true;
  cursor->valid = true;
  cursor->sequence = fragment->sequence;
  cursor->line = fragment->line;
  cursor->offset = fragment->offset;
  for (size_t i = 0; i < fragment->length; ++i) {
    if (text[i] == '\n') {
      if (cursor->line == UINT64_MAX) {
        cursor->valid = false;
        break;
      }
      ++cursor->line;
      cursor->offset = 0;
    } else {
      if (cursor->offset == UINT64_MAX) {
        cursor->valid = false;
        break;
      }
      ++cursor->offset;
    }
  }
  if (!cursor->valid) {
    source_label(source);
    fprintf(stderr, "%s fragment cursor overflow\n", fatal ? "fatal" : "normal");
  }
}

static bool print_text(const unsigned char *text, size_t length)
{
  for (size_t i = 0; i < length; ++i) {
    unsigned char character = text[i];
    if ((character < 32 && character != '\n' && character != '\t') ||
        (character >= 127 && character <= 159)) {
      if (fprintf(stdout, "\\x%02x", character) < 0) {
        return false;
      }
    } else if (fputc(character, stdout) == EOF) {
      return false;
    }
  }
  return fflush(stdout) != EOF;
}

int main(int argc, char **argv)
{
  const char *bind_address = "0.0.0.0";
  unsigned char filter[6];
  bool filtered = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--help")) {
      usage(stdout);
      return 0;
    }
    if (!strcmp(argv[i], "--bind") && i + 1 < argc) {
      bind_address = argv[++i];
    } else if (!strcmp(argv[i], "--source") && i + 1 < argc) {
      if (!parse_mac(argv[++i], filter)) {
        fprintf(stderr, "pyxis-log: --source requires a nonzero unicast MAC (xx:xx:xx:xx:xx:xx)\n");
        return 2;
      }
      filtered = true;
    } else {
      fprintf(stderr, "pyxis-log: unknown option or missing argument: %s\n", argv[i]);
      usage(stderr);
      return 2;
    }
  }
  struct sockaddr_in local = {
    .sin_family = AF_INET, .sin_port = htons(LOG_UDP_PORT)
  };
  if (inet_pton(AF_INET, bind_address, &local.sin_addr) != 1) {
    fprintf(stderr, "pyxis-log: --bind requires a numeric IPv4 address\n");
    return 2;
  }
  struct sigaction action = {.sa_handler = interrupt_handler};
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, NULL) < 0 ||
      sigaction(SIGTERM, &action, NULL) < 0) {
    perror("pyxis-log: signal setup");
    return 1;
  }
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("pyxis-log: socket");
    return 1;
  }
  if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
    perror("pyxis-log: bind");
    close(fd);
    return 1;
  }
  fprintf(stderr, "pyxis-log: listening on %s:%u\n", bind_address, LOG_UDP_PORT);
  int status = 0;
  while (!interrupted) {
    unsigned char bytes[LOG_UDP_DATAGRAM_MAX];
    struct iovec buffer = {.iov_base = bytes, .iov_len = sizeof(bytes)};
    struct msghdr message = {.msg_iov = &buffer, .msg_iovlen = 1};
    ssize_t size = recvmsg(fd, &message, 0);
    if (size < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("pyxis-log: receive");
      status = 1;
      break;
    }
    struct log_udp_fragment fragment;
    if ((message.msg_flags & MSG_TRUNC) ||
        !log_udp_decode(bytes, (size_t)size, &fragment) ||
        (filtered && memcmp(fragment.mac, filter, sizeof(filter)))) {
      continue;
    }
    struct log_source *source = find_source(&fragment);
    if (!accept_sequence(source, &fragment)) {
      continue;
    }
    if (multiple_sources || (fragment.flags & LOG_UDP_FLAG_FATAL)) {
      source_label(source);
      fprintf(stderr, "packet %" PRIu64 " %s text at %" PRIu64 ":%" PRIu64
          " (%u bytes)\n", fragment.sequence,
          fragment.flags & LOG_UDP_FLAG_FATAL ? "fatal" : "normal",
          fragment.line, fragment.offset, fragment.length);
    }
    const unsigned char *text = bytes + LOG_UDP_HEADER_SIZE;
    update_cursor(source, &fragment, text);
    if (!print_text(text, fragment.length)) {
      perror("pyxis-log: stdout");
      status = 1;
      break;
    }
  }
  close(fd);
  return status;
}

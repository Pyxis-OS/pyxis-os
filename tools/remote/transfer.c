#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "transfer.h"
#include "sha256.h"
#include <remote/terminal.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TRANSFER_LIMIT (16u * 1024u * 1024u)
#define TRANSFER_CHUNK 2048u
#define TRANSFER_WAIT_MS 120000
#define CANCEL_WAIT_MS 5000
#define NOTICE_CAPACITY 18000u
#define PRESENT_RESERVE 16384u

/* A single negotiated session owns its snapshot and, during publication, its
 * staging name. No cleanup operation ever takes ownership of an old name. */
enum phase {
  IDLE, UPLOAD_QUERY, CONFIRM_UPLOAD, CONFIRM_DOWNLOAD,
  DOWNLOAD_FILE, DOWNLOAD_DATA, DOWNLOAD_WRITE, DOWNLOAD_SYNC, DOWNLOAD_RENAME, DOWNLOAD_FINISH,
  UPLOAD_REQUEST, UPLOAD_READY, UPLOAD_ACK, UPLOAD_FINISH,
  CANCELING, FAILED
};

struct file_transfer {
  int download_fd, staging_fd;
  bool fatal;
  const volatile sig_atomic_t *interrupted;
  enum phase phase;
  char id[64], query_id[64], file_id[64];
  char path[4097], name[256], staging[256];
  bool staging_owned;
  unsigned char *bytes;
  size_t size, position, written;
  char digest[65];
  int64_t deadline;
  unsigned char osc[REMOTE_PAYLOAD_MAX];
  size_t osc_length;
  bool osc_overflow;
  char notice[NOTICE_CAPACITY];
  size_t notice_length, notice_position;
};

struct command {
  char *action, *id, *file_id, *name, *status, *data, *hash, *sha_mode;
  char *size, *file_type, *compression, *transmission, *quiet;
};

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t encode(const void *data, size_t size, char *output)
{
  const unsigned char *bytes = data;
  size_t used = 0;
  for (size_t i = 0; i < size; i += 3) {
    unsigned value = (unsigned)bytes[i] << 16;
    if (i + 1 < size) {
      value |= (unsigned)bytes[i + 1] << 8;
    }
    if (i + 2 < size) {
      value |= bytes[i + 2];
    }
    output[used++] = alphabet[value >> 18];
    output[used++] = alphabet[(value >> 12) & 63];
    output[used++] = i + 1 < size ? alphabet[(value >> 6) & 63] : '=';
    output[used++] = i + 2 < size ? alphabet[value & 63] : '=';
  }
  output[used] = '\0';
  return used;
}

static int digit(unsigned char byte)
{
  const char *found = byte ? strchr(alphabet, byte) : NULL;
  return found ? (int)(found - alphabet) : -1;
}

static bool decode(const char *text, void *output, size_t capacity, size_t *size)
{
  *size = 0;
  if (!text || strlen(text) % 4) {
    return false;
  }
  unsigned char *bytes = output;
  for (size_t i = 0; text[i]; i += 4) {
    int a = digit((unsigned char)text[i]);
    int b = digit((unsigned char)text[i + 1]);
    int c = text[i + 2] == '=' ? 0 : digit((unsigned char)text[i + 2]);
    int d = text[i + 3] == '=' ? 0 : digit((unsigned char)text[i + 3]);
    bool pad2 = text[i + 2] == '=';
    bool pad1 = text[i + 3] == '=';
    if (a < 0 || b < 0 || c < 0 || d < 0 ||
        (pad2 && !pad1) || ((pad1 || pad2) && text[i + 4]) ||
        (pad2 && (b & 15)) || (!pad2 && pad1 && (c & 3))) {
      return false;
    }
    size_t count = 3 - pad1 - pad2;
    if (count > capacity - *size) {
      return false;
    }
    unsigned value = (unsigned)a << 18 | (unsigned)b << 12 |
        (unsigned)c << 6 | (unsigned)d;
    bytes[(*size)++] = (unsigned char)(value >> 16);
    if (count > 1) {
      bytes[(*size)++] = (unsigned char)(value >> 8);
    }
    if (count > 2) {
      bytes[(*size)++] = (unsigned char)value;
    }
  }
  return true;
}

static bool utf8_text(const char *input)
{
  const unsigned char *bytes = (const unsigned char *)input;
  while (*bytes) {
    if (*bytes < 0x80) {
      bytes++;
      continue;
    }
    unsigned first = *bytes++;
    unsigned count, value, minimum;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1; value = first & 31; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2; value = first & 15; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3; value = first & 7; minimum = 0x10000;
    } else {
      return false;
    }
    while (count--) {
      unsigned next = *bytes++;
      if ((next & 0xc0) != 0x80) {
        return false;
      }
      value = value << 6 | (next & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
      return false;
    }
  }
  return true;
}


static bool string_value(const char *text, char *output, size_t capacity)
{
  size_t size;
  if (!decode(text, output, capacity - 1, &size) || memchr(output, 0, size)) {
    return false;
  }
  output[size] = '\0';
  return utf8_text(output);
}

static bool safe_id(const char *text)
{
  if (!text || !*text || strlen(text) >= 64) {
    return false;
  }
  return strspn(text, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-") == strlen(text);
}

static bool number(const char *text, size_t *value)
{
  if (!text || !*text || strspn(text, "0123456789") != strlen(text)) {
    return false;
  }
  errno = 0;
  char *end;
  unsigned long long n = strtoull(text, &end, 10);
  if (errno || *end || n > SIZE_MAX) {
    return false;
  }
  *value = (size_t)n;
  return true;
}

static bool hash_value(const char *text, char output[65])
{
  if (!text || strlen(text) != 64 || strspn(text, "0123456789abcdefABCDEF") != 64) {
    return false;
  }
  for (size_t i = 0; i < 64; ++i) {
    output[i] = text[i] >= 'A' && text[i] <= 'F' ? text[i] + ('a' - 'A') : text[i];
  }
  output[64] = '\0';
  return true;
}

static void checksum(const void *bytes, size_t size, char digest[65])
{
  struct sha256 state;
  unsigned char hash[SHA256_DIGEST_LENGTH];
  sha256_init(&state);
  sha256_update(&state, bytes, size);
  sha256_sum(&state, hash);
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(hash); ++i) {
    digest[2 * i] = hex[hash[i] >> 4];
    digest[2 * i + 1] = hex[hash[i] & 15];
  }
  digest[64] = '\0';
}

static void notice(struct file_transfer *transfer, const char *format, ...)
{
  size_t pending = transfer->notice_length - transfer->notice_position;
  memmove(transfer->notice, transfer->notice + transfer->notice_position, pending);
  transfer->notice_position = 0;
  va_list arguments;
  va_start(arguments, format);
  size_t capacity = sizeof(transfer->notice) - pending;
  int length = vsnprintf(transfer->notice + pending, capacity, format, arguments);
  va_end(arguments);
  transfer->notice_length = pending + (length < 0 ? 0 :
      (size_t)length < capacity ? (size_t)length : capacity - 1);
}

/* Paths/status messages never inject host terminal controls into a prompt. */
static void escaped(const char *text, char *output)
{
  static const char hex[] = "0123456789abcdef";
  for (; *text; ++text) {
    unsigned char c = (unsigned char)*text;
    if (c >= 32 && c < 127 && c != '\\' && c != '"') {
      *output++ = (char)c;
    } else {
      *output++ = '\\';
      *output++ = 'x';
      *output++ = hex[c >> 4];
      *output++ = hex[c & 15];
    }
  }
  *output = '\0';
}

static bool packet(struct byte_buffer *outgoing, const char *format, ...)
{
  unsigned char bytes[REMOTE_PAYLOAD_MAX];
  memcpy(bytes, "\x1b]5113;", 7);
  va_list arguments;
  va_start(arguments, format);
  int size = vsnprintf((char *)bytes + 7, sizeof(bytes) - 8, format, arguments);
  va_end(arguments);
  if (size < 0 || (size_t)size > sizeof(bytes) - 9) {
    return false;
  }
  size_t length = 7 + (size_t)size;
  bytes[length++] = 27;
  bytes[length++] = '\\';
  if (buffer_space(outgoing) < REMOTE_HEADER_SIZE + length) {
    return false;
  }
  unsigned char header[REMOTE_HEADER_SIZE];
  remote_encode_u32(header, REMOTE_INPUT);
  remote_encode_u32(header + 4, (uint32_t)length);
  buffer_append(outgoing, header, sizeof(header));
  buffer_append(outgoing, bytes, length);
  return true;
}

static bool status_packet(struct file_transfer *transfer, struct byte_buffer *outgoing,
    const char *file_id, const char *status, size_t size, bool negotiation)
{
  char encoded[1025];
  if (strlen(status) > 768) {
    return false;
  }
  encode(status, strlen(status), encoded);
  return packet(outgoing, "ac=status;id=%s%s%s;st=%s;sz=%zu%s",
      transfer->id, file_id ? ";fid=" : "", file_id ? file_id : "",
      encoded, size, negotiation ? ";px_sha256=1" : "");
}

static void cleanup(struct file_transfer *transfer)
{
  if (transfer->staging_fd >= 0) {
    close(transfer->staging_fd);
    transfer->staging_fd = -1;
  }
  if (transfer->staging_owned) {
    if (unlinkat(transfer->download_fd, transfer->staging, 0) < 0) {
      char safe[1025];
      escaped(transfer->staging, safe);
      notice(transfer, "\nxfer: cannot remove staging file \"%s\": %s\n", safe, strerror(errno));
    }
    transfer->staging_owned = false;
  }
}

static void reset(struct file_transfer *transfer)
{
  cleanup(transfer);
  free(transfer->bytes);
  transfer->bytes = NULL;
  transfer->phase = IDLE;
  transfer->size = transfer->position = 0;
  transfer->deadline = 0;
}

static void fail(struct file_transfer *transfer, struct byte_buffer *outgoing,
    const char *message, int64_t now)
{
  cleanup(transfer);
  char safe[2049];
  escaped(message, safe);
  notice(transfer, "\nxfer: %s\n", safe);
  status_packet(transfer, outgoing, transfer->file_id[0] ? transfer->file_id : NULL,
      message, transfer->position, false);
  transfer->phase = FAILED;
  transfer->deadline = now + CANCEL_WAIT_MS;
}

static bool parse(char *body, struct command *command)
{
  memset(command, 0, sizeof(*command));
  char *next;
  for (char *field = strtok_r(body, ";", &next); field; field = strtok_r(NULL, ";", &next)) {
    char *equal = strchr(field, '=');
    if (!equal) {
      return false;
    }
    *equal++ = '\0';
    char **slot = NULL;
    if (!strcmp(field, "ac")) { slot = &command->action; }
    else if (!strcmp(field, "id")) { slot = &command->id; }
    else if (!strcmp(field, "fid")) { slot = &command->file_id; }
    else if (!strcmp(field, "n")) { slot = &command->name; }
    else if (!strcmp(field, "st")) { slot = &command->status; }
    else if (!strcmp(field, "d")) { slot = &command->data; }
    else if (!strcmp(field, "sha256")) { slot = &command->hash; }
    else if (!strcmp(field, "px_sha256")) { slot = &command->sha_mode; }
    else if (!strcmp(field, "sz")) { slot = &command->size; }
    else if (!strcmp(field, "ft")) { slot = &command->file_type; }
    else if (!strcmp(field, "zip")) { slot = &command->compression; }
    else if (!strcmp(field, "tt")) { slot = &command->transmission; }
    else if (!strcmp(field, "q")) { slot = &command->quiet; }
    if (slot) {
      if (*slot) {
        return false;
      }
      *slot = equal;
    }
  }
  return command->action && safe_id(command->id);
}

static bool regular_metadata(const struct command *command)
{
  return (!command->file_type || !strcmp(command->file_type, "regular")) &&
      (!command->compression || !strcmp(command->compression, "none")) &&
      (!command->transmission || !strcmp(command->transmission, "simple"));
}

static bool basename_value(const char *path, char name[256])
{
  const char *base = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }
  size_t size = strlen(base);
  if (!size || size > 255 || !strcmp(base, ".") || !strcmp(base, "..")) {
    return false;
  }
  memcpy(name, base, size + 1);
  return true;
}

static bool read_source(struct file_transfer *transfer)
{
  const char *query = transfer->path;
  char absolute[4097];
  if (*query != '/') {
    const char *home = getenv("HOME");
    if (!home || !*home) {
      errno = ENOENT;
      return false;
    }
    if (!strncmp(query, "~/", 2)) {
      query += 2;
    }
    int length = snprintf(absolute, sizeof(absolute), "%s/%s", home, query);
    if (length < 0 || (size_t)length >= sizeof(absolute)) {
      errno = ENAMETOOLONG;
      return false;
    }
    query = absolute;
  }
  if (strlen(query) > 1024) {
    errno = ENAMETOOLONG;
    return false;
  }
  if (!utf8_text(query)) {
    errno = EINVAL;
    return false;
  }
  int fd = open(query, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  struct stat info;
  int stat_result = fstat(fd, &info);
  if (stat_result < 0 || !S_ISREG(info.st_mode)) {
    int error = stat_result < 0 ? errno : EINVAL;
    close(fd);
    errno = error;
    return false;
  }
  if (info.st_size < 0 || (uintmax_t)info.st_size > TRANSFER_LIMIT) {
    close(fd);
    errno = EFBIG;
    return false;
  }
  transfer->bytes = malloc(TRANSFER_LIMIT + 1u);
  if (!transfer->bytes) {
    close(fd);
    errno = ENOMEM;
    return false;
  }
  transfer->size = 0;
  while (transfer->size <= TRANSFER_LIMIT) {
    ssize_t count = read(fd, transfer->bytes + transfer->size,
        TRANSFER_LIMIT + 1u - transfer->size);
    if (count < 0) {
      int error = errno;
      close(fd);
      errno = error;
      return false;
    }
    if (!count) {
      break;
    }
    transfer->size += (size_t)count;
  }
  if (close(fd) < 0) {
    return false;
  }
  if (transfer->size > TRANSFER_LIMIT) {
    errno = EFBIG;
    return false;
  }
  if (transfer->size != (size_t)info.st_size) {
    errno = EIO;
    return false;
  }
  if (query != transfer->path) {
    strcpy(transfer->path, query);
  }
  checksum(transfer->bytes, transfer->size, transfer->digest);
  return true;
}

static bool stage(struct file_transfer *transfer)
{
  size_t prefix = strlen(transfer->name);
  if (prefix > 160) {
    prefix = 160;
    while (prefix && ((unsigned char)transfer->name[prefix] & 0xc0) == 0x80) {
      --prefix;
    }
  }
  snprintf(transfer->staging, sizeof(transfer->staging), ".%.*s.xfer-partial-%s",
      (int)prefix, transfer->name, transfer->id);
  transfer->staging_fd = openat(transfer->download_fd, transfer->staging,
      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (transfer->staging_fd < 0) {
    return false;
  }
  transfer->staging_owned = true;
  transfer->written = 0;
  transfer->phase = DOWNLOAD_WRITE;
  return true;
}

static bool publish(struct file_transfer *transfer)
{
#if defined(__linux__)
  if (renameat2(transfer->download_fd, transfer->staging,
      transfer->download_fd, transfer->name, RENAME_NOREPLACE) < 0) {
    return false;
  }
#elif defined(__APPLE__)
  if (renameatx_np(transfer->download_fd, transfer->staging,
      transfer->download_fd, transfer->name, RENAME_EXCL) < 0) {
    return false;
  }
#else
  errno = ENOTSUP;
  return false;
#endif
  transfer->staging_owned = false;
  return true;
}

static void command(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, char *body, int64_t now)
{
  (void)screen;
  struct command frame;
  if (!parse(body, &frame)) {
    if (transfer->phase != IDLE && transfer->phase != CANCELING && transfer->phase != FAILED) {
      fail(transfer, outgoing, "EINVAL:Malformed file-transfer command", now);
    }
    return;
  }
  if (transfer->phase == IDLE) {
    if (strcmp(frame.action, "send") && strcmp(frame.action, "receive")) {
      return;
    }
    strcpy(transfer->id, frame.id);
    transfer->file_id[0] = '\0';
    transfer->query_id[0] = '\0';
    transfer->deadline = now + TRANSFER_WAIT_MS;
    if (!frame.sha_mode || strcmp(frame.sha_mode, "1") ||
        (frame.quiet && strcmp(frame.quiet, "0")) || !regular_metadata(&frame)) {
      fail(transfer, outgoing, "ENOTSUP:Negotiated SHA-256 is required", now);
      return;
    }
    if (!strcmp(frame.action, "receive")) {
      size_t paths;
      if (!number(frame.size, &paths) || paths != 1) {
        fail(transfer, outgoing, "ENOTSUP:Only one regular file can be transferred", now);
      } else {
        transfer->phase = UPLOAD_QUERY;
      }
      return;
    }
    if (transfer->download_fd < 0) {
      fail(transfer, outgoing, "EPERM:A --download-dir is required", now);
      return;
    }
    if (!string_value(frame.name, transfer->path, sizeof(transfer->path)) ||
        !basename_value(transfer->path, transfer->name) ||
        !number(frame.size, &transfer->size) || transfer->size > TRANSFER_LIMIT ||
        !hash_value(frame.hash, transfer->digest)) {
      fail(transfer, outgoing, "EINVAL:Invalid download metadata or size", now);
      return;
    }
    char safe[1025];
    escaped(transfer->name, safe);
    notice(transfer, "\nDownload \"%s\" (%zu bytes) into the selected download directory? [y/N] ",
        safe, transfer->size);
    transfer->phase = CONFIRM_DOWNLOAD;
    return;
  }
  if (strcmp(frame.id, transfer->id)) {
    return;
  }
  if (transfer->phase != CANCELING && transfer->phase != FAILED) {
    transfer->deadline = now + TRANSFER_WAIT_MS;
  }
  if (!strcmp(frame.action, "cancel")) {
    status_packet(transfer, outgoing, NULL, "CANCELED", transfer->position, false);
    notice(transfer, "\nxfer: transfer canceled by Pyxis\n");
    reset(transfer);
    return;
  }
  if (!strcmp(frame.action, "status")) {
    char status[513];
    if (!string_value(frame.status, status, sizeof(status))) {
      if (transfer->phase == CANCELING || transfer->phase == FAILED) {
        return;
      }
      fail(transfer, outgoing, "EINVAL:Invalid transfer status", now);
      return;
    }
    if (transfer->phase == CANCELING && !strcmp(status, "CANCELED")) {
      reset(transfer);
      return;
    }
    if (transfer->phase == CANCELING || transfer->phase == FAILED) {
      return;
    }
    size_t received;
    if (transfer->phase == UPLOAD_ACK && !strcmp(status, "PROGRESS") &&
        frame.file_id && !strcmp(frame.file_id, transfer->file_id) &&
        number(frame.size, &received) && received == transfer->position) {
      transfer->phase = UPLOAD_READY;
      return;
    }
    char safe[2049];
    escaped(status, safe);
    notice(transfer, "\nxfer: Pyxis reported %s\n", safe);
    transfer->phase = FAILED;
    transfer->deadline = now + CANCEL_WAIT_MS;
    return;
  }
  if (transfer->phase == CANCELING || transfer->phase == FAILED) {
    return;
  }
  if (transfer->phase == UPLOAD_QUERY && !strcmp(frame.action, "file")) {
    if (!safe_id(frame.file_id) || !regular_metadata(&frame) ||
        !string_value(frame.name, transfer->path, sizeof(transfer->path)) || !transfer->path[0]) {
      fail(transfer, outgoing, "EINVAL:Invalid host file request", now);
      return;
    }
    strcpy(transfer->query_id, frame.file_id);
    char safe[16389];
    escaped(transfer->path, safe);
    notice(transfer, "\nUpload host file \"%s\" to Pyxis? [y/N] ", safe);
    transfer->phase = CONFIRM_UPLOAD;
    return;
  }
  if (transfer->phase == DOWNLOAD_FILE && !strcmp(frame.action, "file")) {
    char path[4097], name[256], digest[65];
    size_t size;
    if (!safe_id(frame.file_id) || !regular_metadata(&frame) ||
        !string_value(frame.name, path, sizeof(path)) || !basename_value(path, name) ||
        strcmp(name, transfer->name) || !number(frame.size, &size) || size != transfer->size ||
        !hash_value(frame.hash, digest) || strcmp(digest, transfer->digest)) {
      fail(transfer, outgoing, "EINVAL:Download metadata changed after confirmation", now);
      return;
    }
    strcpy(transfer->file_id, frame.file_id);
    transfer->phase = DOWNLOAD_DATA;
    status_packet(transfer, outgoing, transfer->file_id, "STARTED", 0, false);
    return;
  }
  if (transfer->phase == UPLOAD_REQUEST && !strcmp(frame.action, "file")) {
    char path[4097];
    if (!frame.file_id || strcmp(frame.file_id, transfer->file_id) ||
        !regular_metadata(&frame) || !string_value(frame.name, path, sizeof(path)) ||
        strcmp(path, transfer->path)) {
      fail(transfer, outgoing, "EINVAL:Upload request changed after confirmation", now);
    } else {
      transfer->phase = UPLOAD_READY;
    }
    return;
  }
  if (transfer->phase == DOWNLOAD_DATA &&
      (!strcmp(frame.action, "data") || !strcmp(frame.action, "end_data"))) {
    unsigned char data[TRANSFER_CHUNK];
    size_t count;
    if (!frame.file_id || strcmp(frame.file_id, transfer->file_id) ||
        !decode(frame.data ? frame.data : "", data, sizeof(data), &count) ||
        count > transfer->size - transfer->position) {
      fail(transfer, outgoing, "EINVAL:Invalid download data or size", now);
      return;
    }
    memcpy(transfer->bytes + transfer->position, data, count);
    transfer->position += count;
    if (!strcmp(frame.action, "data")) {
      status_packet(transfer, outgoing, transfer->file_id, "PROGRESS", transfer->position, false);
      return;
    }
    char digest[65];
    checksum(transfer->bytes, transfer->position, digest);
    if (transfer->position != transfer->size || strcmp(digest, transfer->digest)) {
      fail(transfer, outgoing, "EBADMSG:File size or SHA-256 mismatch", now);
      return;
    }
    if (!stage(transfer)) {
      char message[256];
      snprintf(message, sizeof(message), "EIO:Cannot publish download: %s", strerror(errno));
      fail(transfer, outgoing, message, now);
      return;
    }
    return;
  }
  if (!strcmp(frame.action, "finish") &&
      (transfer->phase == DOWNLOAD_FINISH || transfer->phase == UPLOAD_FINISH)) {
    status_packet(transfer, outgoing, NULL, "OK", transfer->size, false);
    if (transfer->phase == UPLOAD_FINISH) {
      notice(transfer, "\nxfer: upload verified and published by Pyxis (%zu bytes)\n", transfer->size);
    }
    reset(transfer);
    return;
  }
  fail(transfer, outgoing, "EINVAL:Unexpected transfer command", now);
}

struct file_transfer *transfer_create(const char *download_directory,
    const volatile sig_atomic_t *interrupted)
{
  struct file_transfer *transfer = calloc(1, sizeof(*transfer));
  if (!transfer) {
    return NULL;
  }
  transfer->download_fd = transfer->staging_fd = -1;
  transfer->interrupted = interrupted;
  if (download_directory) {
    transfer->download_fd = open(download_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (transfer->download_fd < 0) {
      free(transfer);
      return NULL;
    }
  }
  return transfer;
}

void transfer_destroy(struct file_transfer *transfer)
{
  if (!transfer) {
    return;
  }
  reset(transfer);
  if (transfer->notice_position < transfer->notice_length) {
    fwrite(transfer->notice + transfer->notice_position, 1,
        transfer->notice_length - transfer->notice_position, stderr);
  }
  if (transfer->download_fd >= 0) {
    close(transfer->download_fd);
  }
  free(transfer);
}

void transfer_output(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, unsigned char byte, int64_t now)
{
  static const unsigned char prefix[] = "\x1b]5113;";
  if (!transfer->osc_length) {
    if (byte != 27) {
      presentation_data(screen, &byte, 1);
      return;
    }
  }
  if (transfer->osc_length < sizeof(prefix) - 1 &&
      byte != prefix[transfer->osc_length]) {
    presentation_data(screen, transfer->osc, transfer->osc_length);
    transfer->osc_length = 0;
    if (byte == 27) {
      transfer->osc[transfer->osc_length++] = byte;
    } else {
      presentation_data(screen, &byte, 1);
    }
    return;
  }
  if (transfer->osc_length == sizeof(transfer->osc)) {
    transfer->osc_overflow = true;
  }
  bool terminated = transfer->osc_length >= sizeof(prefix) - 1 &&
      (byte == 7 || (byte == '\\' && transfer->osc_length &&
       transfer->osc[transfer->osc_length - 1] == 27));
  if (terminated) {
    if (transfer->osc_overflow) {
      if (transfer->phase != IDLE && transfer->phase != CANCELING && transfer->phase != FAILED) {
        fail(transfer, outgoing, "EINVAL:Oversized transfer command", now);
      }
    } else {
      size_t end = transfer->osc_length - (byte == '\\');
      transfer->osc[end] = '\0';
      command(transfer, screen, outgoing, (char *)transfer->osc + sizeof(prefix) - 1, now);
    }
    transfer->osc_length = 0;
    transfer->osc_overflow = false;
    return;
  }
  if (!transfer->osc_overflow) {
    transfer->osc[transfer->osc_length++] = byte;
  } else {
    /* Retain only the latest byte needed to recognize the terminator. */
    transfer->osc[sizeof(transfer->osc) - 1] = byte;
  }
}

bool transfer_input(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, unsigned char byte, int64_t now)
{
  (void)screen;
  if (transfer->phase == IDLE || byte == 0x1d) {
    return false;
  }
  if (transfer->phase == CONFIRM_UPLOAD || transfer->phase == CONFIRM_DOWNLOAD) {
    if (transfer->notice_position < transfer->notice_length) {
      return true;
    }
    if (byte != 'y' && byte != 'Y') {
      if (byte != 'n' && byte != 'N' && byte != '\r' && byte != '\n' && byte != 3 && byte != 27) {
        return true;
      }
      status_packet(transfer, outgoing, NULL, "EPERM:Transfer refused by host user", 0, false);
      notice(transfer, "\nxfer: transfer refused\n");
      transfer->phase = FAILED;
      transfer->deadline = now + CANCEL_WAIT_MS;
      return true;
    }
    bool upload = transfer->phase == CONFIRM_UPLOAD;
    if (upload) {
      if (!read_source(transfer)) {
        char message[256];
        snprintf(message, sizeof(message), "EIO:Cannot read host file: %s", strerror(errno));
        fail(transfer, outgoing, message, now);
        return true;
      }
      strcpy(transfer->file_id, "f1");
      char name[5465], status[89];
      encode(transfer->path, strlen(transfer->path), name);
      encode(transfer->file_id, strlen(transfer->file_id), status);
      status_packet(transfer, outgoing, NULL, "OK", 0, true);
      if (!packet(outgoing, "ac=file;id=%s;fid=%s;st=%s;n=%s;sz=%zu;ft=regular;sha256=%s",
          transfer->id, transfer->query_id, status, name, transfer->size, transfer->digest)) {
        fail(transfer, outgoing, "EINVAL:Host file path is too long for a metadata frame", now);
        return true;
      }
      status_packet(transfer, outgoing, NULL, "OK", 0, false);
      transfer->phase = UPLOAD_REQUEST;
    } else {
      transfer->bytes = malloc(transfer->size ? transfer->size : 1);
      if (!transfer->bytes) {
        fail(transfer, outgoing, "ENOMEM:Cannot buffer download", now);
        return true;
      }
      status_packet(transfer, outgoing, NULL, "OK", 0, true);
      transfer->phase = DOWNLOAD_FILE;
    }
    transfer->deadline = now + TRANSFER_WAIT_MS;
    return true;
  }
  if (byte == 3 || byte == 27) {
    if (transfer->phase != CANCELING && transfer->phase != FAILED) {
      packet(outgoing, "ac=cancel;id=%s", transfer->id);
      notice(transfer, "\nxfer: cancelling transfer\n");
      transfer->phase = CANCELING;
      transfer->deadline = now + CANCEL_WAIT_MS;
    }
  }
  return true;
}

void transfer_pump(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, int64_t now)
{
  unsigned budget = 128;
  while (transfer->notice_position < transfer->notice_length && budget-- &&
      buffer_space(screen->output) >= PRESENT_RESERVE) {
    presentation_data(screen, (unsigned char *)transfer->notice + transfer->notice_position, 1);
    ++transfer->notice_position;
  }
  if (transfer->phase != IDLE && now >= transfer->deadline &&
      (transfer->phase == CANCELING || transfer->phase == FAILED ||
       buffer_space(outgoing) >= TRANSFER_REPLY_RESERVE)) {
    if (transfer->phase == CANCELING || transfer->phase == FAILED) {
      notice(transfer, "\nxfer: cancellation was not acknowledged before the deadline\n");
      transfer->fatal = true;
      cleanup(transfer);
    } else {
      packet(outgoing, "ac=cancel;id=%s", transfer->id);
      notice(transfer, "\nxfer: transfer timed out; cancelling\n");
      transfer->phase = CANCELING;
      transfer->deadline = now + CANCEL_WAIT_MS;
    }
  }
  /* Publication advances one bounded step per event-loop turn, allowing
   * cancellation input between writes, synchronization and the rename. */
  if (transfer->phase >= DOWNLOAD_WRITE && transfer->phase <= DOWNLOAD_RENAME &&
      !*transfer->interrupted && buffer_space(outgoing) >= TRANSFER_REPLY_RESERVE) {
    bool ok = true;
    if (transfer->phase == DOWNLOAD_WRITE) {
      size_t count = transfer->size - transfer->written;
      if (count > 16384) {
        count = 16384;
      }
      if (!count) {
        transfer->phase = DOWNLOAD_SYNC;
      } else {
        ssize_t written = write(transfer->staging_fd,
            transfer->bytes + transfer->written, count);
        if (written > 0) {
          transfer->written += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
          return;
        } else {
          if (!written) { errno = EIO; }
          ok = false;
        }
      }
    } else if (transfer->phase == DOWNLOAD_SYNC) {
      ok = fsync(transfer->staging_fd) == 0;
      int error = errno;
      if (close(transfer->staging_fd) < 0 && ok) {
        ok = false;
        error = errno;
      }
      transfer->staging_fd = -1;
      errno = error;
      if (ok) { transfer->phase = DOWNLOAD_RENAME; }
    } else {
      ok = publish(transfer);
      if (ok) {
        if (fsync(transfer->download_fd) < 0) {
          char message[256];
          snprintf(message, sizeof(message),
              "EIO:Download published but directory sync failed: %s", strerror(errno));
          fail(transfer, outgoing, message, now);
          return;
        }
        status_packet(transfer, outgoing, transfer->file_id, "OK", transfer->size, false);
        notice(transfer, "\nxfer: download verified and published (%zu bytes)\n", transfer->size);
        transfer->phase = DOWNLOAD_FINISH;
      }
    }
    if (!ok) {
      char message[256];
      snprintf(message, sizeof(message), "EIO:Cannot publish download: %s", strerror(errno));
      fail(transfer, outgoing, message, now);
    }
  }
  if (transfer->phase == UPLOAD_READY &&
      buffer_space(outgoing) >= REMOTE_PAYLOAD_MAX + REMOTE_HEADER_SIZE) {
    size_t count = transfer->size - transfer->position;
    if (count > TRANSFER_CHUNK) {
      count = TRANSFER_CHUNK;
    }
    bool last = transfer->position + count == transfer->size;
    char data[4 * ((TRANSFER_CHUNK + 2) / 3) + 1];
    encode(transfer->bytes + transfer->position, count, data);
    if (packet(outgoing, "ac=%s;id=%s;fid=%s;d=%s",
        last ? "end_data" : "data", transfer->id, transfer->file_id, data)) {
      transfer->position += count;
      transfer->phase = last ? UPLOAD_FINISH : UPLOAD_ACK;
      transfer->deadline = now + TRANSFER_WAIT_MS;
    }
  }
}

bool transfer_failed(const struct file_transfer *transfer)
{
  return transfer->fatal;
}

int transfer_timeout(const struct file_transfer *transfer, int64_t now, bool can_present, bool can_reply)
{
  if ((can_present && transfer->notice_position < transfer->notice_length) ||
      (can_reply && (transfer->phase == UPLOAD_READY ||
       (transfer->phase >= DOWNLOAD_WRITE && transfer->phase <= DOWNLOAD_RENAME)))) {
    return 0;
  }
  if (transfer->phase == IDLE) {
    return -1;
  }
  int64_t remaining = transfer->deadline - now;
  if (remaining <= 0 && !can_reply && transfer->phase != CANCELING && transfer->phase != FAILED) {
    return -1;
  }
  return remaining <= 0 ? 0 : remaining > INT_MAX ? INT_MAX : (int)remaining;
}

void transfer_fresh_line(struct file_transfer *transfer, struct presentation *screen)
{
  if (transfer->osc_length && !transfer->osc_overflow) {
    presentation_data(screen, transfer->osc, transfer->osc_length);
  }
  transfer->osc_length = 0;
  transfer->osc_overflow = false;
  if (transfer->phase != IDLE) {
    notice(transfer, "\nxfer: Pyxis program ended during transfer; disconnecting\n");
    cleanup(transfer);
    transfer->fatal = true;
  }
}

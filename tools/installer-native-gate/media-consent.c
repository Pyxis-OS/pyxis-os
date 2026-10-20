/* SPDX-License-Identifier: MPL-2.0 */
/* Packet-local read-only adapter for the installer's unchanged consent reader. */
#define _GNU_SOURCE
#include "installer.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int source_fd = -1;

bool install_read(const struct install_disk *disk, uint64_t offset,
    void *bytes, size_t length)
{
  if (offset > disk->bytes || length > disk->bytes - offset ||
      offset > INT64_MAX || length > SSIZE_MAX) {
    return false;
  }
  size_t done = 0;
  while (done < length) {
    ssize_t amount = pread(source_fd, (char *)bytes + done, length - done,
        (off_t)(offset + done));
    if (amount < 0 && errno == EINTR) {
      continue;
    }
    if (amount <= 0) {
      return false;
    }
    done += (size_t)amount;
  }
  return true;
}

void install_progress_begin(const char *phase, enum install_progress_measure measure,
    const char *unit)
{
  (void)phase;
  (void)measure;
  (void)unit;
}

void install_progress_items_total(uint64_t total)
{
  (void)total;
}

void install_progress_advance(uint64_t bytes, uint64_t items)
{
  (void)bytes;
  (void)items;
}

void install_progress_finish(void)
{
}

int main(int argc, char **argv)
{
  if (argc == 2 && !strcmp(argv[1], "--help")) {
    puts("usage: media-consent SOURCE EXPECTED-BYTES (read-only)");
    return 0;
  }
  if (argc != 3) {
    fputs("media-consent: require source and expected size\n", stderr);
    return 1;
  }
  char *end;
  errno = 0;
  uint64_t expected = strtoull(argv[2], &end, 10);
  if (errno || !argv[2][0] || *end || !expected || expected > INT64_MAX) {
    fputs("media-consent: invalid expected size\n", stderr);
    return 1;
  }
  source_fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  struct stat info;
  uint64_t bytes = 0;
  unsigned block_size = 512;
  bool opened = source_fd >= 0 && !fstat(source_fd, &info);
  if (opened && S_ISBLK(info.st_mode)) {
    opened = !ioctl(source_fd, BLKGETSIZE64, &bytes) &&
        !ioctl(source_fd, BLKSSZGET, &block_size);
  } else if (opened && S_ISREG(info.st_mode) && info.st_size >= 0) {
    bytes = (uint64_t)info.st_size;
  } else {
    opened = false;
  }
  if (!opened || bytes != expected || !block_size || bytes % block_size) {
    fputs("media-consent: source open, type, size or geometry refused\n", stderr);
    if (source_fd >= 0) {
      close(source_fd);
    }
    return 1;
  }
  /* These are inspection inputs after the wrapper's writable-media check,
   * never an opened native write capability. This adapter has no write path. */
  struct install_disk disk = {
    .bytes = bytes,
    .info = { .preparation = DISK_READY,
      .flags = DISK_FLAG_WRITABLE | DISK_FLAG_FLUSH_SUPPORTED,
      .block_size = block_size, .block_count = bytes / block_size },
  };
  struct install_consent consent;
  bool inspected = install_scan_consent(&disk, false, &consent);
  const char *classification = "vetoing";
  if (inspected && consent.eligible) {
    classification = "marked";
  } else if (inspected && consent.blank) {
    classification = "blank";
  }
  /* The reader supplies static reason strings; never emit source paths,
   * pool/volume labels or disk identifiers. Uncertainty is a preflight veto. */
  printf("%s\n%s\npools=%u volumes=%u\n", classification,
      consent.reason ? consent.reason : "consent unavailable", consent.pools, consent.volumes);
  install_consent_destroy(&consent);
  close(source_fd);
  return inspected ? 0 : 1;
}

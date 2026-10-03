#include <abi/directory.h>
#include <disk.h>
#include <directory.h>
#include <clock.h>
#include <file.h>
#include <handle.h>
#include <startup.h>
#include <syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static handle_t disks, clock_handle;
static const uint64_t root_rights = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_READ_FILES;
static uint8_t bytes[4096], original[4096], pattern[4096], other[4096];

static void report(const char *message)
{
  fprintf(stderr, "%s\n", message);
  for (const char *p = message; *p; p++) {
    syscall1(SYSCALL_LOG_PUTCHAR, *p);
  }
  syscall1(SYSCALL_LOG_PUTCHAR, '\n');
}

static void expect(const char *name, enum call_status status, enum call_status wanted)
{
  if (status != wanted) {
    char message[160];
    snprintf(message, sizeof(message), "PROBE FAIL: %s: got %u expected %u", name, status, wanted);
    report(message);
    exit(1);
  }
}

static void require(const char *name, bool value)
{
  if (!value) {
    report(name);
    exit(1);
  }
}

static uint32_t load32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void store32(uint8_t *p, uint32_t value)
{
  for (unsigned i = 0; i < 4; i++) {
    p[i] = value >> (8 * i);
  }
}

static uint32_t crc32(const uint8_t *p, size_t size)
{
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; i++) {
    crc ^= p[i];
    for (unsigned bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
  }
  return ~crc;
}

static uint64_t now(void)
{
  uint64_t value;
  expect("clock", clock_now(clock_handle, &value), CALL_OK);
  return value;
}

static void close_handle(handle_t handle)
{
  require("PROBE FAIL: close", handle_close(handle) == 0);
}

int main(void)
{
  disks = startup_resource("disks");
  clock_handle = startup_resource("clock");
  require("PROBE FAIL: resources absent", disks && clock_handle);
  require("PROBE FAIL: ambient launch/root authority", !startup_resource("launcher") &&
      !startup_root("home") && !startup_namespace() && !startup_working_directory_count() &&
      !startup_environment_count());
  handle_t sources[] = {startup_resource("boot_kernel"), startup_resource("boot_archive")};
  const char *magic[] = {"\177ELF", "0707"};
  for (unsigned i = 0; i < 2; i++) {
    uint64_t size;
    size_t count;
    expect("source size", file_size(sources[i], &size), CALL_OK);
    require("PROBE FAIL: source empty", size > 4);
    expect("source read", file_read(sources[i], 0, bytes, 4, &count), CALL_OK);
    require("PROBE FAIL: source magic", count == 4 && !memcmp(bytes, magic[i], 4));
    expect("source write denied", file_write(sources[i], 0, bytes, 4, &count), CALL_DENIED);
  }
  report("PROBE PASS: explicit installer grants and read-only original sources");

  uint64_t mounted_id = 0, writable_id = 0, fourk_id = 0, readonly_id = 0, unsupported_id = 0;
  struct disk_info writable_info = {0};
  uint64_t end = now() + UINT64_C(30000000000);
  for (;;) {
    bool pending = false;
    unsigned count = 0;
    for (uint64_t index = 0;; index++) {
      struct disk_info info;
      enum call_status status = disks_enumerate(disks, index, &info);
      if (status == CALL_NOT_FOUND) {
        break;
      }
      expect("enumerate", status, CALL_OK);
      count++;
      if (info.gpt_status == DISK_GPT_PENDING) {
        pending = true;
      }
      if (info.preparation == DISK_UNSUPPORTED) {
        unsupported_id = info.id;
      } else if (!(info.flags & DISK_FLAG_WRITABLE)) {
        readonly_id = info.id;
      } else if (info.block_size == 4096) {
        fourk_id = info.id;
      } else if (load32(info.gpt_guid) == UINT32_C(0x12345678)) {
        mounted_id = info.id;
      } else if (load32(info.gpt_guid) == UINT32_C(0x87654321)) {
        writable_id = info.id;
        writable_info = info;
      }
    }
    if (!pending) {
      require("PROBE FAIL: fixture inventory", count == 5 && mounted_id && writable_id &&
          fourk_id && readonly_id && unsupported_id);
      break;
    }
    require("PROBE FAIL: initial GPT timeout", now() < end);
  }
  handle_t disk, inspection, root, denied, alias;
  expect("unsupported", disks_open(disks, unsupported_id, DISK_ACCESS_READ_ONLY, &disk), CALL_UNAVAILABLE);
  expect("read-only device", disks_open(disks, readonly_id, DISK_ACCESS_READ_WRITE, &disk), CALL_READ_ONLY);
  expect("mounted inspection", disks_open(disks, mounted_id, DISK_ACCESS_READ_ONLY, &inspection), CALL_OK);
  expect("normal mount", disk_open_volume(inspection, 1, "bench", root_rights, &root), CALL_OK);
  expect("retained pool exclusion", disks_open(disks, mounted_id, DISK_ACCESS_READ_WRITE, &denied), CALL_BUSY);
  close_handle(root);
  expect("last root close retains mount", disks_open(disks, mounted_id, DISK_ACCESS_READ_WRITE, &denied), CALL_BUSY);
  report("PROBE PASS: unsupported/read-only inventory and retained-pool exclusion");

  expect("claim", disks_open(disks, writable_id, DISK_ACCESS_READ_WRITE, &disk), CALL_OK);
  expect("claim excludes writer", disks_open(disks, writable_id, DISK_ACCESS_READ_WRITE, &denied), CALL_BUSY);
  expect("claim excludes mount", disk_open_volume(disk, 1, "bench", root_rights, &root), CALL_BUSY);
  expect("deny writable root", disk_open_volume(disk, 1, "bench",
      root_rights | DIRECTORY_RIGHT_CREATE, &root), CALL_BAD_REQUEST);
  expect("copy alias", handle_copy(disk, &alias), CALL_OK);
  expect("attenuate", handle_copy_restricted(disk, DISK_RIGHT_INFO | DISK_RIGHT_READ, 0, &denied), CALL_OK);
  expect("attenuated write", disk_write(denied, 65536, bytes, 512), CALL_DENIED);
  close_handle(denied);
  expect("save other disk", disk_read(inspection, 65536, other, 4096), CALL_OK);
  expect("save gap", disk_read(disk, 65536, original, 4096), CALL_OK);
  for (size_t i = 0; i < sizeof(pattern); i++) {
    pattern[i] = (uint8_t)(i * 17 + 43);
  }
  expect("raw write", disk_write(disk, 65536, pattern, 4096), CALL_OK);
  expect("flush", disk_flush(disk), CALL_OK);
  expect("readback", disk_read(disk, 65536, bytes, 4096), CALL_OK);
  require("PROBE FAIL: written bytes", !memcmp(bytes, pattern, 4096));
  expect("other disk read", disk_read(inspection, 65536, bytes, 4096), CALL_OK);
  require("PROBE FAIL: cross-device write", !memcmp(bytes, other, 4096));
  expect("restore gap", disk_write(disk, 65536, original, 4096), CALL_OK);
  report("PROBE PASS: raw read/write/flush, attenuation and disk independence");

  uint8_t new_guid[16];
  memcpy(new_guid, writable_info.gpt_guid, 16);
  new_guid[0] ^= 0x55;
  uint64_t header_offsets[] = {512, (writable_info.block_count - 1) * 512};
  for (unsigned i = 0; i < 2; i++) {
    expect("GPT header read", disk_read(disk, header_offsets[i], bytes, 512), CALL_OK);
    require("PROBE FAIL: GPT signature", !memcmp(bytes, "EFI PART", 8));
    uint32_t size = load32(bytes + 12);
    require("PROBE FAIL: GPT header size", size >= 92 && size <= 512);
    memcpy(bytes + 56, new_guid, 16);
    store32(bytes + 16, 0);
    store32(bytes + 16, crc32(bytes, size));
    expect("GPT header write", disk_write(disk, header_offsets[i], bytes, 512), CALL_OK);
  }
  expect("cached info", disk_get_info(disk, &writable_info), CALL_OK);
  require("PROBE FAIL: early GPT replacement", memcmp(writable_info.gpt_guid, new_guid, 16));
  expect("release", disk_release(disk), CALL_OK);
  expect("alias cannot mutate", disk_write(alias, 65536, pattern, 4096), CALL_DENIED);
  expect("new info", disk_get_info(alias, &writable_info), CALL_OK);
  require("PROBE FAIL: GPT rescan", writable_info.gpt_status == DISK_GPT_HEALTHY &&
      !(writable_info.flags & DISK_FLAG_CLAIMED) && !memcmp(writable_info.gpt_guid, new_guid, 16));
  expect("mount after release", disk_open_volume(disk, 1, "bench", root_rights, &root), CALL_OK);
  close_handle(disk);
  close_handle(alias);
  expect("root survives disk close", directory_lookup(root, "not-present",
      DIRECTORY_KIND_FILE, FILE_RIGHT_READ, &denied), CALL_NOT_FOUND);
  close_handle(root);
  expect("released mounted exclusion", disks_open(disks, writable_id, DISK_ACCESS_READ_WRITE, &denied), CALL_BUSY);
  close_handle(inspection);
  report("PROBE PASS: GPT release rescan, alias revocation and normal mount path");

  expect("4K claim", disks_open(disks, fourk_id, DISK_ACCESS_READ_WRITE, &disk), CALL_OK);
  expect("4K rejects small transfer", disk_read(disk, 65536, bytes, 512), CALL_BAD_REQUEST);
  expect("4K rejects offset", disk_write(disk, 65537, pattern, 4096), CALL_BAD_REQUEST);
  expect("4K write", disk_write(disk, 65536, pattern, 4096), CALL_OK);
  expect("4K read", disk_read(disk, 65536, bytes, 4096), CALL_OK);
  require("PROBE FAIL: 4K bytes", !memcmp(bytes, pattern, 4096));
  close_handle(disk);
  end = now() + UINT64_C(30000000000);
  for (;;) {
    enum call_status status = disks_open(disks, fourk_id, DISK_ACCESS_READ_WRITE, &disk);
    if (status == CALL_OK) {
      break;
    }
    expect("cleanup pending", status, CALL_BUSY);
    require("PROBE FAIL: cleanup timeout", now() < end);
  }
  expect("4K release", disk_release(disk), CALL_OK);
  close_handle(disk);
  report("PROBE PASS: 4K geometry and last-close claim cleanup");
  report("PROBE COMPLETE: all checks passed");
  return 0;
}

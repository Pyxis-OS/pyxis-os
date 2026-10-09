#include <arch/smp.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/boot.h>
#include <kernel/boot/options.h>
#include <kernel/panic.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/user/launch.h>
#include <remote/beacon.h>

static char command_line_storage[BOOT_COMMAND_LINE_SIZE];
static struct boot_options options;
static bool parsed;

static bool same_text(const char *left, const char *right)
{
  size_t size = strlen(left);
  return size == strlen(right) && !memcmp(left, right, size);
}

static unsigned hex_digit(char value)
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  panic("invalid mount disk GUID hex digit");
}

static struct gpt_guid parse_disk_guid(const char *text)
{
  if (strlen(text) != 36 || text[8] != '-' || text[13] != '-' ||
      text[18] != '-' || text[23] != '-') {
    panic("mount.disk must be a canonical GPT GUID");
  }
  uint8_t canonical[16];
  unsigned index = 0;
  for (size_t offset = 0; offset < 36;) {
    if (offset == 8 || offset == 13 || offset == 18 || offset == 23) {
      ++offset;
      continue;
    }
    canonical[index++] = hex_digit(text[offset]) * 16 + hex_digit(text[offset + 1]);
    offset += 2;
  }
  struct gpt_guid disk;
  /* The first three textual fields use little-endian GPT encoding. */
  for (unsigned i = 0; i < 4; ++i) {
    disk.bytes[i] = canonical[3 - i];
  }
  for (unsigned i = 0; i < 2; ++i) {
    disk.bytes[4 + i] = canonical[5 - i];
    disk.bytes[6 + i] = canonical[7 - i];
  }
  memcpy(disk.bytes + 8, canonical + 8, 8);
  struct gpt_guid zero = {0};
  if (!memcmp(&disk, &zero, sizeof(disk))) {
    panic("mount.disk must be nonzero");
  }
  return disk;
}

/* Each option may occur once. FLAG options accept only the value 1. */
static void take_option(const char **option, const char *key, const char *value)
{
  if (*option) {
    panic("kernel option %s must occur once", key);
  }
  *option = value;
}

static bool flag_option(const char *key, const char *value)
{
  if (!same_text(value, "1")) {
    panic("kernel option %s accepts only the value 1", key);
  }
  return true;
}

const struct boot_options *boot_options_parse(const char *command_line)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count() && !parsed);
  parsed = true;

  size_t length = 0;
  while (length < sizeof(command_line_storage) && command_line[length]) {
    command_line_storage[length] = command_line[length];
    ++length;
  }
  if (length == sizeof(command_line_storage)) {
    panic("kernel command line exceeds %zu bytes", sizeof(command_line_storage) - 1);
  }
  command_line_storage[length] = '\0';

  const char *init = NULL, *mount_disk = NULL, *install = NULL, *default_config = NULL;
  const char *remote_beacon = NULL, *log_udp = NULL, *display_size = NULL;
  const char *display_timing = NULL, *display_timing_metrics = NULL, *display_inventory = NULL;

  char *cursor = command_line_storage;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    if (!*cursor) {
      break;
    }
    char *key = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') {
      ++cursor;
    }
    if (*cursor) {
      *cursor++ = '\0';
    }
    char *value = key;
    while (*value && *value != '=') {
      ++value;
    }
    if (!*value) {
      panic("kernel option needs a value: %s", key);
    }
    *value++ = '\0';
    if (same_text(key, "init")) {
      take_option(&init, key, value);
    } else if (same_text(key, "mount.disk")) {
      take_option(&mount_disk, key, value);
    } else if (same_text(key, "boot.install")) {
      take_option(&install, key, value);
    } else if (same_text(key, "boot.default_config")) {
      take_option(&default_config, key, value);
    } else if (same_text(key, "remote.beacon")) {
      take_option(&remote_beacon, key, value);
    } else if (same_text(key, "log.udp")) {
      take_option(&log_udp, key, value);
    } else if (same_text(key, "display.size")) {
      take_option(&display_size, key, value);
    } else if (same_text(key, "display.timing")) {
      take_option(&display_timing, key, value);
    } else if (same_text(key, "display.timing.metrics")) {
      take_option(&display_timing_metrics, key, value);
    } else if (same_text(key, "display.inventory")) {
      take_option(&display_inventory, key, value);
    } else {
      panic("unknown kernel option: %s", key);
    }
  }
  if (!init) {
    panic("kernel command line must name init");
  }
  options.log_udp = log_udp && flag_option("log.udp", log_udp);
  if (remote_beacon && !remote_beacon_name_length(remote_beacon)) {
    panic("remote.beacon must name 1..%u printable ASCII bytes without spaces",
        REMOTE_BEACON_NAME_MAX);
  }
  if (strlen(init) <= USER_BOOT_ROOT_PREFIX_LENGTH ||
      memcmp(init, USER_BOOT_ROOT_PREFIX, USER_BOOT_ROOT_PREFIX_LENGTH)) {
    panic("init must name a " USER_BOOT_ROOT_PREFIX " archive entry: %s", init);
  }

  if (mount_disk) {
    options.mount.disk = parse_disk_guid(mount_disk);
    options.mount.enabled = true;
  }

  options.init = init;
  options.install = install && flag_option("boot.install", install);
  options.default_config = default_config && flag_option("boot.default_config", default_config);
  options.remote_beacon = remote_beacon;
  options.display_size = display_size;
  options.display_timing = display_timing;
  options.display_timing_metrics = display_timing_metrics &&
    flag_option("display.timing.metrics", display_timing_metrics);
  options.display_inventory = display_inventory && flag_option("display.inventory", display_inventory);
  return &options;
}

const struct boot_options *boot_options_get(void)
{
  KASSERT(parsed);
  return &options;
}

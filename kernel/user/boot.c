#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/mount.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/user/launch.h>

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

void user_launch_initial(const char *command_line)
{
  KASSERT(arch_cpu_index() == 0);
  char *options = strndup(command_line, strlen(command_line));
  if (!options) {
    panic("cannot allocate kernel options");
  }
  const char *init = NULL, *mount_disk = NULL, *install = NULL, *default_config = NULL;

  char *cursor = options;
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
    } else {
      panic("unknown kernel option: %s", key);
    }
  }
  if (!init) {
    panic("kernel command line must name init");
  }
  if (strlen(init) <= USER_BOOT_ROOT_PREFIX_LENGTH ||
      memcmp(init, USER_BOOT_ROOT_PREFIX, USER_BOOT_ROOT_PREFIX_LENGTH)) {
    panic("init must name a " USER_BOOT_ROOT_PREFIX " archive entry: %s", init);
  }

  struct mount_config mount = {0};
  if (mount_disk) {
    mount.disk = parse_disk_guid(mount_disk);
    mount.enabled = true;
  }
  /* Boot init runs in Caelum's space and creates every other space. */
  user_launch_boot_init(init, &mount, install && flag_option("boot.install", install),
      default_config && flag_option("boot.default_config", default_config));
  kfree(options);
}

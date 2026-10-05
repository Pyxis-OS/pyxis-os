#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/mount.h>
#include <kernel/panic.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/user/launch.h>

static bool same_text(const char *left, const char *right)
{
  size_t size = strlen(left);
  return size == strlen(right) && !memcmp(left, right, size);
}

#define SPACE_NAME_MAX 31
#define SPACE_OPTION_PREFIX "space."

struct space_selection {
  const char *name;
  const char *image;
};

/* Names identify configured spaces; they grant nothing and need not be titles. */
static bool valid_space_name(const char *name)
{
  size_t length = strlen(name);
  if (!length || length > SPACE_NAME_MAX) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    char c = name[i];
    if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') {
      return false;
    }
  }
  return true;
}

/* Until tasks can migrate, each workload space is pinned to one CPU: workload
 * CPUs in configuration order, wrapping, or the BSP on a single-CPU boot. */
static size_t workload_cpu(size_t position)
{
  size_t count = arch_cpu_count();
  return count == 1 ? 0 : 1 + position % (count - 1);
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

void user_launch_initial(const char *command_line)
{
  KASSERT(arch_cpu_index() == 0);
  size_t length = strlen(command_line);
  char *options = strndup(command_line, length);
  /* Every option is a separated KEY=VALUE token of at least three bytes. */
  size_t capacity = length / 2 + 1;
  struct space_selection *selections = kmalloc(capacity * sizeof(*selections));
  if (!options || !selections) {
    panic("cannot allocate space selections");
  }
  size_t selection_count = 0;
  const char *mount_disk = NULL;
  bool install = false;

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
    if (same_text(key, "boot.install")) {
      if (install || !same_text(value, "1")) {
        panic("boot.install must occur once with value 1");
      }
      install = true;
      continue;
    }
    if (same_text(key, "mount.disk")) {
      if (mount_disk) {
        panic("duplicate mount disk configuration");
      }
      mount_disk = value;
      continue;
    }
    size_t prefix = strlen(SPACE_OPTION_PREFIX);
    if (strlen(key) <= prefix || memcmp(key, SPACE_OPTION_PREFIX, prefix)) {
      panic("unknown kernel option: %s", key);
    }
    const char *name = key + prefix;
    if (!valid_space_name(name)) {
      panic("invalid space name: %s", name);
    }
    if (strlen(value) <= 6 || memcmp(value, "app://", 6)) {
      panic("space %s init must name an app:// archive entry: %s", name, value);
    }
    for (size_t i = 0; i < selection_count; ++i) {
      if (same_text(selections[i].name, name)) {
        panic("duplicate space configuration: %s", name);
      }
    }
    KASSERT(selection_count < capacity);
    selections[selection_count++] = (struct space_selection){name, value};
  }
  if (!selection_count) {
    panic("kernel command line must configure at least one space");
  }
  if (install && selection_count != 1) {
    panic("boot.install requires exactly one configured space");
  }

  struct mount_config mount = {0};
  if (mount_disk) {
    mount.disk = parse_disk_guid(mount_disk);
    mount.enabled = true;
  }

  for (size_t i = 0; i < selection_count; ++i) {
    struct space *space = space_create(selections[i].name, workload_cpu(i));
    user_launch_init(space, selections[i].image, &mount, install);
  }
  kfree(selections);
  kfree(options);
}

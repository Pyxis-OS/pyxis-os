#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/log.h>
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

static size_t parse_cpu(const char *text)
{
  if (!*text) {
    panic("init selection has no CPU index");
  }
  size_t index = 0;
  while (*text) {
    if (*text < '0' || *text > '9' || index > (SIZE_MAX - (*text - '0')) / 10) {
      panic("invalid init CPU index: %s", text);
    }
    index = index * 10 + (*text++ - '0');
  }
  return index;
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
  size_t count = arch_cpu_count();
  KASSERT(count && count <= SIZE_MAX / sizeof(const char *));
  size_t primary = count > 1 ? 1 : 0;
  char *options = strndup(command_line, strlen(command_line));
  const char **images = kmalloc(count * sizeof(*images));
  if (!options || !images) {
    panic("cannot allocate init selections");
  }
  memset(images, 0, count * sizeof(*images));
  const char *default_image = NULL, *primary_image = NULL;
  const char *mount_disk = NULL;
  bool install = false, install_seen = false;

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
    char *image = key;
    while (*image && *image != '=') {
      ++image;
    }
    if (!*image) {
      panic("kernel option needs a value: %s", key);
    }
    *image++ = '\0';
    if (same_text(key, "boot.install")) {
      if (install_seen || !same_text(image, "1")) {
        panic("boot.install must occur once with value 1");
      }
      install = true;
      install_seen = true;
      continue;
    }
    if (same_text(key, "mount.disk")) {
      if (mount_disk) {
        panic("duplicate mount disk configuration");
      }
      mount_disk = image;
      continue;
    }
    if (strlen(image) <= 6 || memcmp(image, "app://", 6)) {
      panic("init must name an app:// archive entry: %s", image);
    }

    if (same_text(key, "init")) {
      if (default_image) {
        panic("duplicate default init selection");
      }
      default_image = image;
    } else if (same_text(key, "init.primary")) {
      if (primary_image) {
        panic("duplicate primary init selection");
      }
      primary_image = image;
    } else if (strlen(key) > 5 && !memcmp(key, "init.", 5)) {
      size_t index = parse_cpu(key + 5);
      if (index >= count) {
        klog("userspace: skipping init for absent CPU %zu\n", index);
        continue;
      }
      if (index == 0 && count > 1) {
        panic("CPU 0 is reserved for Caelum on a multicore boot");
      }
      if (images[index]) {
        panic("duplicate init selection for CPU %zu", index);
      }
      images[index] = image;
    } else {
      panic("unknown kernel option: %s", key);
    }
  }
  if (!default_image) {
    panic("kernel command line must select a default init");
  }

  struct mount_config mount = {0};
  if (mount_disk) {
    mount.disk = parse_disk_guid(mount_disk);
    mount.enabled = true;
  }

  /* Numeric overrides win over the primary selection regardless of option
   * order. Every workload CPU gets an init; an idle script can simply exit. */
  for (size_t index = primary; index < count; ++index) {
    arch_clock_maintain();
    const char *image = images[index];
    if (!image && index == primary) {
      image = primary_image;
    }
    image = image ? image : default_image;
    if (install) {
      image = index == primary ? "app://init-install.pxe" : "app://init-idle";
    }
    user_launch_init(index, image, &mount, install && index == primary);
  }
  kfree(images);
  kfree(options);
}

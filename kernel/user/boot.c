#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
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

  /* Numeric overrides win over the primary selection regardless of option
   * order. Every workload CPU gets an init; an idle script can simply exit. */
  for (size_t index = primary; index < count; ++index) {
    const char *image = images[index];
    if (!image && index == primary) {
      image = primary_image;
    }
    user_launch_init(index, image ? image : default_image);
  }
  kfree(images);
  kfree(options);
}

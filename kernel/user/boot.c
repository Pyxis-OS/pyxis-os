#include <arch/smp.h>
#include <kernel/format.h>
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
#define SPACE_CPUS_SUFFIX "cpus"
#define NO_ABSENT_CPU SIZE_MAX

struct space_selection {
  const char *name;
  const char *image;
  const char *cpus; /* NULL: every boot CPU. */
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

static struct space_selection *find_selection(struct space_selection *selections,
    size_t count, const char *name)
{
  for (size_t i = 0; i < count; ++i) {
    if (same_text(selections[i].name, name)) {
      return &selections[i];
    }
  }
  return NULL;
}

static size_t parse_cpu_number(const char **cursor, const char *name)
{
  const char *text = *cursor;
  if (*text < '0' || *text > '9') {
    panic("space %s CPU set is malformed", name);
  }
  size_t value = 0;
  while (*text >= '0' && *text <= '9') {
    if (value > (SIZE_MAX - (*text - '0')) / 10) {
      panic("space %s CPU index overflows", name);
    }
    value = value * 10 + (*text++ - '0');
  }
  *cursor = text;
  return value;
}

static void allow_cpu(uint64_t *allowed, size_t cpu)
{
  allowed[cpu / 64] |= UINT64_C(1) << (cpu % 64);
}

/* LIST is comma-separated boot CPU indices and inclusive A-B ranges. Malformed
 * syntax is fatal. Returns the lowest index without a boot CPU, if any. */
static size_t parse_cpu_list(const char *name, const char *list, uint64_t *allowed)
{
  size_t count = arch_cpu_count();
  size_t absent = NO_ABSENT_CPU;
  const char *cursor = list;
  for (;;) {
    size_t first = parse_cpu_number(&cursor, name);
    size_t last = first;
    if (*cursor == '-') {
      ++cursor;
      last = parse_cpu_number(&cursor, name);
      if (last < first) {
        panic("space %s CPU range %zu-%zu is reversed", name, first, last);
      }
    }
    size_t first_absent = first > count ? first : count;
    if (last >= count && first_absent < absent) {
      absent = first_absent;
    }
    for (size_t cpu = first; cpu <= last && cpu < count; ++cpu) {
      allow_cpu(allowed, cpu);
    }
    if (!*cursor) {
      return absent;
    }
    if (*cursor++ != ',') {
      panic("space %s CPU set is malformed", name);
    }
  }
}

static void report_unstarted(struct space *space, const char *name, const char *reason)
{
  /* Bounded: a 31-byte name and a reason of at most 96 bytes. */
  char text[160];
  KASSERT(strlen(name) <= SPACE_NAME_MAX && strlen(reason) <= 96);
  sprintf(text, "space %s not started: %s\n", name, reason);
  klog("userspace: %s", text);
  space_report(space, text);
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
  struct space_selection *cpu_sets = kmalloc(capacity * sizeof(*cpu_sets));
  if (!options || !selections || !cpu_sets) {
    panic("cannot allocate space selections");
  }
  size_t selection_count = 0, cpu_set_count = 0;
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
    char *name = key + prefix;
    char *attribute = name;
    while (*attribute && *attribute != '.') {
      ++attribute;
    }
    if (*attribute) {
      *attribute++ = '\0';
      if (!same_text(attribute, SPACE_CPUS_SUFFIX)) {
        panic("unknown kernel option: space.%s.%s", name, attribute);
      }
    }
    if (!valid_space_name(name)) {
      panic("invalid space name: %s", name);
    }
    if (*attribute) {
      for (size_t i = 0; i < cpu_set_count; ++i) {
        if (same_text(cpu_sets[i].name, name)) {
          panic("duplicate CPU set for space %s", name);
        }
      }
      KASSERT(cpu_set_count < capacity);
      cpu_sets[cpu_set_count++] = (struct space_selection){.name = name, .cpus = value};
      continue;
    }
    if (strlen(value) <= 6 || memcmp(value, "app://", 6)) {
      panic("space %s init must name an app:// archive entry: %s", name, value);
    }
    if (find_selection(selections, selection_count, name)) {
      panic("duplicate space configuration: %s", name);
    }
    KASSERT(selection_count < capacity);
    selections[selection_count++] = (struct space_selection){.name = name, .image = value};
  }
  for (size_t i = 0; i < cpu_set_count; ++i) {
    struct space_selection *selection = find_selection(selections, selection_count,
        cpu_sets[i].name);
    if (!selection) {
      panic("CPU set for unconfigured space %s", cpu_sets[i].name);
    }
    selection->cpus = cpu_sets[i].cpus;
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

  size_t words = space_cpu_words();
  for (size_t i = 0; i < selection_count; ++i) {
    const char *name = selections[i].name;
    uint64_t *allowed = kmalloc(words * sizeof(*allowed));
    if (!allowed) {
      panic("cannot allocate space CPU set");
    }
    memset(allowed, 0, words * sizeof(*allowed));
    size_t absent = NO_ABSENT_CPU;
    if (selections[i].cpus) {
      absent = parse_cpu_list(name, selections[i].cpus, allowed);
    } else {
      for (size_t cpu = 0; cpu < arch_cpu_count(); ++cpu) {
        allow_cpu(allowed, cpu);
      }
    }

    const char *reason = NULL;
    char absent_reason[64];
    if (absent != NO_ABSENT_CPU) {
      sprintf(absent_reason, "CPU set names absent CPU %zu", absent);
      reason = absent_reason;
    }
    if (reason) {
      /* An unstarted space accepts no tasks. */
      memset(allowed, 0, words * sizeof(*allowed));
      report_unstarted(space_create(name, allowed), name, reason);
      continue;
    }
    struct space *space = space_create(name, allowed);
    user_launch_init(space, selections[i].image, &mount, install);
  }
  kfree(cpu_sets);
  kfree(selections);
  kfree(options);
}

#include <arch/clock.h>
#include <kernel/boot.h>
#include <kernel/initrd.h>
#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#define NEWC_ALIGNMENT 4
#define NEWC_TYPE_MASK 0170000
#define NEWC_REGULAR 0100000
#define NEWC_DIRECTORY 0040000
#define INITRD_CLOCK_SAMPLE_BYTES (256 * PAGE_SIZE)

/* newc stores numbers as eight ASCII hexadecimal digits, not binary integers.
 * Names and payloads follow the header, with each payload/header aligned to 4. */
struct newc_header {
  char magic[6];
  char inode[8];
  char mode[8];
  char uid[8];
  char gid[8];
  char links[8];
  char mtime[8];
  char file_size[8];
  char device_major[8];
  char device_minor[8];
  char rdevice_major[8];
  char rdevice_minor[8];
  char name_size[8];
  char checksum[8];
};

_Static_assert(sizeof(struct newc_header) == 110, "newc header size");

struct cpio_entry {
  const char *name;
  size_t name_length;
  const void *data;
  size_t size;
  bool directory;
  bool trailer;
};

static const uint8_t *archive_bytes;
static size_t archive_size;

static bool read_hex(const char field[8], uint32_t *value)
{
  uint32_t number = 0;
  for (size_t i = 0; i < 8; ++i) {
    unsigned digit;
    if (field[i] >= '0' && field[i] <= '9') {
      digit = field[i] - '0';
    } else if (field[i] >= 'a' && field[i] <= 'f') {
      digit = field[i] - 'a' + 10;
    } else if (field[i] >= 'A' && field[i] <= 'F') {
      digit = field[i] - 'A' + 10;
    } else {
      return false;
    }
    number = (number << 4) | digit;
  }
  *value = number;
  return true;
}

static bool align_offset(size_t size, size_t *offset)
{
  size_t padding = (NEWC_ALIGNMENT - (*offset % NEWC_ALIGNMENT)) % NEWC_ALIGNMENT;
  if (padding > size - *offset) {
    return false;
  }
  *offset += padding;
  return true;
}

static enum initrd_result read_entry(const uint8_t *bytes, size_t size,
                                     size_t *offset, struct cpio_entry *entry)
{
  if (sizeof(struct newc_header) > size - *offset) {
    return INITRD_INVALID;
  }
  const struct newc_header *header = (const void *)(bytes + *offset);
  *offset += sizeof(*header);
  if (memcmp(header->magic, "070701", sizeof(header->magic))) {
    return INITRD_INVALID;
  }

  uint32_t mode, links, file_size, name_size, checksum;
  if (!read_hex(header->mode, &mode) || !read_hex(header->links, &links) ||
      !read_hex(header->file_size, &file_size) ||
      !read_hex(header->name_size, &name_size) ||
      !read_hex(header->checksum, &checksum) || checksum ||
      name_size < 2 || name_size > size - *offset) {
    return INITRD_INVALID;
  }

  entry->name = (const char *)(bytes + *offset);
  entry->name_length = strnlen(entry->name, name_size);
  if (!entry->name_length || entry->name_length == name_size) {
    return INITRD_INVALID;
  }
  /* Some writers include extra terminating NULs in name_size. */
  for (size_t i = entry->name_length; i < name_size; ++i) {
    if (entry->name[i]) {
      return INITRD_INVALID;
    }
  }
  *offset += name_size;
  if (!align_offset(size, offset) || file_size > size - *offset) {
    return INITRD_INVALID;
  }

  entry->data = bytes + *offset;
  entry->size = file_size;
  *offset += file_size;
  if (!align_offset(size, offset)) {
    return INITRD_INVALID;
  }

  entry->trailer = entry->name_length == sizeof("TRAILER!!!") - 1 &&
                  !memcmp(entry->name, "TRAILER!!!", sizeof("TRAILER!!!") - 1);
  entry->directory = (mode & NEWC_TYPE_MASK) == NEWC_DIRECTORY;
  if (entry->trailer) {
    return file_size ? INITRD_INVALID : INITRD_OK;
  }
  if (!links) {
    return INITRD_INVALID;
  }
  if (entry->directory) {
    return file_size ? INITRD_INVALID : INITRD_OK;
  }
  /* Hard-link data may live in a later entry. Never expose an empty alias as
   * an ordinary file when this reader does not implement link resolution. */
  if ((mode & NEWC_TYPE_MASK) != NEWC_REGULAR || links != 1) {
    return INITRD_UNSUPPORTED;
  }
  return INITRD_OK;
}

static enum initrd_result validate_archive(const uint8_t *bytes, size_t size)
{
  size_t offset = 0;
  while (offset < size) {
    arch_clock_maintain();
    struct cpio_entry entry;
    enum initrd_result result = read_entry(bytes, size, &offset, &entry);
    if (result != INITRD_OK) {
      return result;
    }
    if (entry.trailer) {
      /* Accept cpio's final block padding, but not concatenated archives. */
      for (; offset < size; ++offset) {
        if (!(offset % INITRD_CLOCK_SAMPLE_BYTES)) {
          arch_clock_maintain();
        }
        if (bytes[offset]) {
          return INITRD_INVALID;
        }
      }
      return INITRD_OK;
    }
  }
  return INITRD_INVALID;
}

enum initrd_result initrd_init(const struct boot_module *module)
{
  if (archive_bytes || !module || !module->size) {
    return INITRD_INVALID;
  }
  size_t page_offset = module->physical & (PAGE_SIZE - 1);
  if (module->size > SIZE_MAX - page_offset - (PAGE_SIZE - 1)) {
    return INITRD_INVALID;
  }
  size_t mapped_size = (page_offset + module->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  phys_addr_t first_frame = module->physical - page_offset;
  if (first_frame > UINT64_MAX - mapped_size) {
    return INITRD_INVALID;
  }

  uintptr_t mapping;
  enum mm_result status = vm_reserve(vm_kernel_space(), mapped_size, PAGE_SIZE, &mapping);
  if (status != MM_OK) {
    return status == MM_NO_MEMORY ? INITRD_NO_MEMORY : INITRD_INVALID;
  }

  size_t mapped = 0;
  while (mapped < mapped_size) {
    status = vm_map(vm_kernel_space(), mapping + mapped, first_frame + mapped, 0);
    if (status != MM_OK) {
      break;
    }
    mapped += PAGE_SIZE;
    if (!(mapped % INITRD_CLOCK_SAMPLE_BYTES)) {
      arch_clock_maintain();
    }
  }

  const uint8_t *bytes = (const void *)(mapping + page_offset);
  enum initrd_result result;
  if (status == MM_OK) {
    result = validate_archive(bytes, module->size);
  } else {
    result = status == MM_NO_MEMORY ? INITRD_NO_MEMORY : INITRD_INVALID;
  }
  if (result == INITRD_OK) {
    archive_bytes = bytes;
    archive_size = module->size;
    return INITRD_OK;
  }

  while (mapped) {
    mapped -= PAGE_SIZE;
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), mapping + mapped, &physical) == MM_OK);
    KASSERT(physical == first_frame + mapped);
  }
  KASSERT(vm_release(vm_kernel_space(), mapping, mapped_size) == MM_OK);
  return result;
}

enum initrd_result initrd_lookup(const char *name, struct initrd_file *file)
{
  if (file) {
    *file = (struct initrd_file){0};
  }
  if (!archive_bytes || !name || !file) {
    return INITRD_INVALID;
  }

  size_t name_length = strlen(name);
  size_t offset = 0;
  while (offset < archive_size) {
    struct cpio_entry entry;
    enum initrd_result result = read_entry(archive_bytes, archive_size, &offset, &entry);
    if (result != INITRD_OK) {
      return result;
    }
    if (entry.trailer) {
      break;
    }
    if (!entry.directory && entry.name_length == name_length &&
        !memcmp(entry.name, name, name_length)) {
      *file = (struct initrd_file){.data = entry.data, .size = entry.size};
      return INITRD_OK;
    }
  }
  return INITRD_NOT_FOUND;
}

enum initrd_result initrd_next(size_t *offset, struct initrd_entry *entry)
{
  if (entry) {
    *entry = (struct initrd_entry){0};
  }
  if (!archive_bytes || !offset || !entry || *offset > archive_size) {
    return INITRD_INVALID;
  }
  if (*offset == archive_size) {
    return INITRD_END;
  }

  struct cpio_entry parsed;
  size_t next = *offset;
  enum initrd_result result = read_entry(archive_bytes, archive_size, &next, &parsed);
  if (result != INITRD_OK) {
    return result;
  }
  if (parsed.trailer) {
    *offset = archive_size;
    return INITRD_END;
  }
  *entry = (struct initrd_entry){
    .name = parsed.name,
    .name_length = parsed.name_length,
    .file = {parsed.data, parsed.size},
    .directory = parsed.directory,
  };
  *offset = next;
  return INITRD_OK;
}

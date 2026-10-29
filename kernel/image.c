#include <arch/cpu.h>
#include <arch/paging.h>
#include <kernel/image.h>
#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <pxe/p1f.h>

#define IMAGE_MAX_SPAN (UINT64_C(256) * 1024 * 1024)
#define IMAGE_STEP_PAGES 64

static void preparation_yield(void)
{
  if (task_current_actor()) {
    task_yield_masked();
  }
}

/* P1F is little-endian, as is the supported x86_64 kernel. Copying into local
 * structures also avoids unaligned access and trusting a typed input pointer. */
static struct p1f_segment read_segment(const uint8_t *bytes, size_t index)
{
  struct p1f_segment segment;
  memcpy(&segment, bytes + sizeof(struct p1f_header) + index * sizeof(segment),
         sizeof(segment));
  return segment;
}

static bool validate_image(const uint8_t *bytes, size_t size,
    uintptr_t reserved_base, uintptr_t reserved_end,
    struct p1f_header *header, size_t *payload)
{
  if (!bytes || size < sizeof(*header)) {
    return false;
  }
  memcpy(header, bytes, sizeof(*header));
  if (header->magic != P1F_MAGIC || !header->segment_count ||
      header->segment_count > (size - sizeof(*header)) / sizeof(struct p1f_segment)) {
    return false;
  }

  *payload = sizeof(*header) + header->segment_count * sizeof(struct p1f_segment);
  size_t remaining = size - *payload;
  uint64_t previous_end = P1F_PAGE_SIZE;
  uint64_t image_base = read_segment(bytes, 0).virtual_address;
  bool executable_entry = false;

  for (size_t i = 0; i < header->segment_count; ++i) {
    if (i && !(i % IMAGE_STEP_PAGES)) {
      preparation_yield();
    }
    struct p1f_segment segment = read_segment(bytes, i);
    if ((segment.virtual_address & (P1F_PAGE_SIZE - 1)) ||
        segment.virtual_address < previous_end || segment.virtual_address >= P1F_USER_LIMIT ||
        !segment.memory_size || segment.memory_size > P1F_USER_LIMIT - segment.virtual_address ||
        segment.file_size > segment.memory_size || segment.file_size > remaining ||
        (segment.flags & ~(P1F_READ | P1F_WRITE | P1F_EXECUTE)) ||
        !(segment.flags & P1F_READ) ||
        (segment.flags & (P1F_WRITE | P1F_EXECUTE)) == (P1F_WRITE | P1F_EXECUTE)) {
      return false;
    }

    uint64_t end = segment.virtual_address + segment.memory_size;
    previous_end = (end + P1F_PAGE_SIZE - 1) & ~(P1F_PAGE_SIZE - 1);
    if (previous_end - image_base > IMAGE_MAX_SPAN ||
        (segment.virtual_address < reserved_end && previous_end > reserved_base)) {
      return false;
    }
    remaining -= segment.file_size;
    if ((segment.flags & P1F_EXECUTE) && header->entry >= segment.virtual_address &&
        header->entry < end) {
      executable_entry = true;
    }
  }
  return !remaining && executable_entry;
}

static enum mm_result load_segment(struct vm_space *space,
                                   const struct p1f_segment *segment,
                                   const uint8_t *payload)
{
  /* VM owns and zeroes every destination frame, including the BSS and the last
   * partial page. No executable user mapping is writable during the copy. */
  enum mm_result status = vm_reserve_at(space, segment->virtual_address,
      segment->memory_size);
  if (status != MM_OK) {
    return status;
  }
  struct vm_backing backing;
  status = vm_back_begin(space, segment->virtual_address, segment->memory_size,
      PAGE_USER | PAGE_WRITE, &backing);
  if (status != MM_OK) {
    return status;
  }
  while (!vm_back_step(space, &backing, IMAGE_STEP_PAGES)) {
    preparation_yield();
  }
  if (backing.status != MM_OK) {
    return backing.status;
  }

  for (size_t offset = 0; offset < segment->file_size; offset += PAGE_SIZE) {
    struct page_translation translation;
    KASSERT(vm_query(space, segment->virtual_address + offset, &translation) == MM_OK);
    size_t count = segment->file_size - offset;
    if (count > PAGE_SIZE) {
      count = PAGE_SIZE;
    }
    void *page = arch_frame_map(translation.physical);
    memcpy(page, payload + offset, count);
    arch_frame_unmap();
    if (!((offset / PAGE_SIZE + 1) % IMAGE_STEP_PAGES)) {
      preparation_yield();
    }
  }

  unsigned permissions = PAGE_USER;
  if (segment->flags & P1F_WRITE) {
    permissions |= PAGE_WRITE;
  }
  if (segment->flags & P1F_EXECUTE) {
    permissions |= PAGE_EXEC;
  }
  for (size_t offset = 0; offset < segment->memory_size; offset += PAGE_SIZE) {
    status = vm_protect(space, segment->virtual_address + offset, permissions);
    if (status != MM_OK) {
      return status;
    }
    if (!((offset / PAGE_SIZE + 1) % IMAGE_STEP_PAGES)) {
      preparation_yield();
    }
  }
  return MM_OK;
}

enum image_result image_load(const void *bytes, size_t size,
    uintptr_t reserved_base, uintptr_t reserved_end,
    struct vm_space **space, uintptr_t *entry)
{
  _Static_assert(P1F_PAGE_SIZE == PAGE_SIZE, "P1F page granularity");
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(arch_space_active(arch_kernel_space()));

  if (space) {
    *space = NULL;
  }
  if (entry) {
    *entry = 0;
  }
  if (!space || !entry || reserved_base < P1F_PAGE_SIZE ||
      reserved_base >= reserved_end || reserved_end > P1F_USER_LIMIT ||
      ((reserved_base | reserved_end) & (P1F_PAGE_SIZE - 1))) {
    return IMAGE_INVALID;
  }

  struct p1f_header header;
  size_t payload;
  if (!validate_image(bytes, size, reserved_base, reserved_end, &header, &payload)) {
    return IMAGE_INVALID;
  }

  struct vm_space *loaded;
  enum mm_result status = vm_space_create(&loaded);
  if (status != MM_OK) {
    return status == MM_BUSY ? IMAGE_BUSY :
        status == MM_NO_MEMORY ? IMAGE_NO_MEMORY : IMAGE_INVALID;
  }

  for (size_t i = 0; i < header.segment_count; ++i) {
    struct p1f_segment segment = read_segment(bytes, i);
    status = load_segment(loaded, &segment, (const uint8_t *)bytes + payload);
    if (status != MM_OK) {
      break;
    }
    payload += segment.file_size;
    if (!((i + 1) % IMAGE_STEP_PAGES)) {
      preparation_yield();
    }
  }

  if (status != MM_OK) {
    KASSERT(vm_space_retire(loaded) == MM_OK);
    while (!vm_space_release_step(loaded, IMAGE_STEP_PAGES)) {
      preparation_yield();
    }
    return status == MM_BUSY ? IMAGE_BUSY :
        status == MM_NO_MEMORY ? IMAGE_NO_MEMORY : IMAGE_INVALID;
  }

  *space = loaded;
  *entry = header.entry;
  return IMAGE_OK;
}

#include <kernel/mm/pmm.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define FRAMES_PER_BITMAP_BYTE 8
#define PMM_BITMAP_COUNT 2

/* Unavailable includes allocations and reservations. Allocatable is immutable
 * after initialization, so reserved frames can never be freed by a caller. */
static uint8_t *unavailable_bitmap;
static uint8_t *allocatable_bitmap;
static size_t bitmap_bytes;
static size_t frame_count;
static struct pmm_stats stats;

static bool bit_get(const uint8_t *bitmap, size_t frame)
{
  size_t byte = frame / FRAMES_PER_BITMAP_BYTE;
  unsigned bit = frame % FRAMES_PER_BITMAP_BYTE;
  return (bitmap[byte] >> bit) & 1;
}

static void bit_set(uint8_t *bitmap, size_t frame, bool value)
{
  uint8_t mask = (uint8_t)(1u << (frame % FRAMES_PER_BITMAP_BYTE));
  if (value) {
    bitmap[frame / FRAMES_PER_BITMAP_BYTE] |= mask;
  } else {
    bitmap[frame / FRAMES_PER_BITMAP_BYTE] &= (uint8_t)~mask;
  }
}

static size_t bitmap_size_for_frames(size_t frames)
{
  return (frames + FRAMES_PER_BITMAP_BYTE - 1) / FRAMES_PER_BITMAP_BYTE;
}

static uint64_t page_up(uint64_t value)
{
  if (value > UINT64_MAX - (PAGE_SIZE - 1)) {
    panic("PMM address rounding overflow");
  }
  return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static uint64_t usable_memory_end(const struct boot_info *boot)
{
  uint64_t limit = 0;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->type == BOOT_USABLE) {
      if (region->base > UINT64_MAX - region->length ||
          region->base + region->length > PMM_MAX_PHYS) {
        panic("PMM: usable memory exceeds 64 GiB bitmap capacity");
      }
      if (region->base + region->length > limit) {
        limit = region->base + region->length;
      }
    }
  }
  return limit;
}

struct pmm_bootstrap pmm_plan(const struct boot_info *boot)
{
  struct pmm_bootstrap plan = {
    .frame_count = usable_memory_end(boot) / PAGE_SIZE,
  };
  if (!plan.frame_count) {
    panic("PMM: no usable physical pages");
  }
  size_t bytes = bitmap_size_for_frames(plan.frame_count);
  plan.metadata_pages = page_up(bytes * PMM_BITMAP_COUNT) / PAGE_SIZE;
  size_t storage_bytes = plan.metadata_pages * PAGE_SIZE;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->type != BOOT_USABLE) {
      continue;
    }
    uint64_t start = page_up(region->base);
    uint64_t end = (region->base + region->length) & ~(PAGE_SIZE - 1);
    if (!start) {
      start = PAGE_SIZE;
    }
    if (start <= end && storage_bytes <= end - start) {
      plan.metadata_phys = start;
      return plan;
    }
  }
  panic("PMM: no usable contiguous range for bitmap metadata");
}

void pmm_rebase(void *metadata)
{
  KASSERT(metadata != NULL);
  unavailable_bitmap = metadata;
  allocatable_bitmap = unavailable_bitmap + bitmap_bytes;
}

void pmm_init(const struct boot_info *boot, struct pmm_bootstrap plan,
              void *metadata)
{
  KASSERT(!unavailable_bitmap);
  frame_count = plan.frame_count;
  bitmap_bytes = bitmap_size_for_frames(frame_count);
  pmm_rebase(metadata);
  memset(unavailable_bitmap, UINT8_MAX, bitmap_bytes);
  memset(allocatable_bitmap, 0, bitmap_bytes);
  stats.metadata_pages = plan.metadata_pages;
  uint64_t metadata_end = plan.metadata_phys + plan.metadata_pages * PAGE_SIZE;
  uint64_t kernel_end = boot->kernel_phys + boot->kernel_size;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->type != BOOT_USABLE) {
      continue;
    }
    uint64_t end = (region->base + region->length) & ~(PAGE_SIZE - 1);
    for (uint64_t physical = page_up(region->base); physical < end;
         physical += PAGE_SIZE) {
      if (!physical ||
          (physical >= plan.metadata_phys && physical < metadata_end) ||
          (physical >= boot->kernel_phys && physical < kernel_end)) {
        continue;
      }
      size_t frame = physical / PAGE_SIZE;
      KASSERT(frame < frame_count);
      bit_set(allocatable_bitmap, frame, true);
      bit_set(unavailable_bitmap, frame, false);
      ++stats.total_frames;
    }
  }
  stats.free_frames = stats.total_frames;
}

phys_addr_t pmm_alloc(size_t pages)
{
  KASSERT(unavailable_bitmap != NULL);
  if (!pages || pages > stats.free_frames) {
    return 0;
  }
  size_t run = 0;
  for (size_t frame = 1; frame < frame_count; ++frame) {
    if (bit_get(unavailable_bitmap, frame)) {
      run = 0;
      continue;
    }
    ++run;
    if (run == pages) {
      size_t first = frame + 1 - pages;
      for (size_t i = first; i <= frame; ++i) {
        bit_set(unavailable_bitmap, i, true);
      }
      stats.free_frames -= pages;
      stats.allocated_frames += pages;
      return first * PAGE_SIZE;
    }
  }
  return 0;
}

void pmm_free(phys_addr_t physical, size_t pages)
{
  size_t first = physical / PAGE_SIZE;
  if (!unavailable_bitmap || !physical || (physical & (PAGE_SIZE - 1)) || !pages ||
      first >= frame_count || pages > frame_count - first) {
    panic("PMM: invalid free phys=0x%lx pages=%zu", physical, pages);
  }
  /* Validate the whole extent before changing anything. Eligibility is a second
   * bitmap so reserved and metadata frames cannot be freed as allocations. */
  for (size_t i = first; i < first + pages; ++i) {
    if (!bit_get(allocatable_bitmap, i) || !bit_get(unavailable_bitmap, i)) {
      panic("PMM: reserved or already-free frame 0x%lx", i * PAGE_SIZE);
    }
  }
  for (size_t i = first; i < first + pages; ++i) {
    bit_set(unavailable_bitmap, i, false);
  }
  stats.free_frames += pages;
  stats.allocated_frames -= pages;
}

struct pmm_stats pmm_get_stats(void)
{
  return stats;
}

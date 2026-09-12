#include <kernel/mm/pmm.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

static uint8_t *allocated;
static uint8_t *eligible;
static size_t bitmap_bytes;
static size_t frame_count;
static struct pmm_stats stats;

static bool bit_get(const uint8_t *bitmap, size_t frame)
{
  return (bitmap[frame / 8] >> (frame % 8)) & 1;
}

static void bit_set(uint8_t *bitmap, size_t frame, bool value)
{
  uint8_t mask = (uint8_t)(1u << (frame % 8));
  if (value) {
    bitmap[frame / 8] |= mask;
  } else {
    bitmap[frame / 8] &= (uint8_t)~mask;
  }
}

static uint64_t page_up(uint64_t value)
{
  if (value > UINT64_MAX - (PAGE_SIZE - 1)) {
    panic("PMM address rounding overflow");
  }
  return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

struct pmm_bootstrap pmm_plan(const struct boot_info *boot)
{
  uint64_t limit = 0;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->type == BOOT_USABLE) {
      if (r->base > UINT64_MAX - r->length || r->base + r->length > PMM_MAX_PHYS) {
        panic("PMM: usable memory exceeds 64 GiB bitmap capacity");
      }
      if (r->base + r->length > limit) {
        limit = r->base + r->length;
      }
    }
  }
  struct pmm_bootstrap plan = {.frame_count = limit / PAGE_SIZE};
  if (!plan.frame_count) {
    panic("PMM: no usable physical pages");
  }
  size_t bytes = (plan.frame_count + 7) / 8;
  plan.metadata_pages = page_up(bytes * 2) / PAGE_SIZE;
  size_t storage_bytes = plan.metadata_pages * PAGE_SIZE;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->type != BOOT_USABLE) {
      continue;
    }
    uint64_t start = page_up(r->base);
    uint64_t end = (r->base + r->length) & ~(PAGE_SIZE - 1);
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
  allocated = metadata;
  eligible = allocated + bitmap_bytes;
}

void pmm_init(const struct boot_info *boot, struct pmm_bootstrap plan, void *metadata)
{
  KASSERT(!allocated);
  frame_count = plan.frame_count;
  bitmap_bytes = (frame_count + 7) / 8;
  pmm_rebase(metadata);
  memset(allocated, 0xff, bitmap_bytes);
  memset(eligible, 0, bitmap_bytes);
  stats.metadata_pages = plan.metadata_pages;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->type != BOOT_USABLE) {
      continue;
    }
    uint64_t end = (r->base + r->length) & ~(PAGE_SIZE - 1);
    for (uint64_t physical = page_up(r->base); physical < end; physical += PAGE_SIZE) {
      if (!physical ||
          (physical >= plan.metadata_phys &&
           physical < plan.metadata_phys + plan.metadata_pages * PAGE_SIZE) ||
          (physical >= boot->kernel_phys && physical < boot->kernel_phys + boot->kernel_size)) {
        continue;
      }
      size_t frame = physical / PAGE_SIZE;
      KASSERT(frame < frame_count);
      bit_set(eligible, frame, true);
      bit_set(allocated, frame, false);
      ++stats.total_frames;
    }
  }
  stats.free_frames = stats.total_frames;
}

phys_addr_t pmm_alloc(size_t pages)
{
  KASSERT(allocated != NULL);
  if (!pages || pages > stats.free_frames) {
    return 0;
  }
  size_t run = 0;
  for (size_t frame = 1; frame < frame_count; ++frame) {
    if (bit_get(allocated, frame)) {
      run = 0;
      continue;
    }
    if (++run == pages) {
      size_t first = frame + 1 - pages;
      for (size_t i = first; i <= frame; ++i) {
        bit_set(allocated, i, true);
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
  if (!allocated || !physical || (physical & (PAGE_SIZE - 1)) || !pages ||
      first >= frame_count || pages > frame_count - first) {
    panic("PMM: invalid free phys=0x%lx pages=%zu", physical, pages);
  }
  /* Validate the whole extent before changing anything. Eligibility is a second
   * bitmap so reserved and metadata frames cannot be freed as allocations. */
  for (size_t i = first; i < first + pages; ++i) {
    if (!bit_get(eligible, i) || !bit_get(allocated, i)) {
      panic("PMM: reserved or already-free frame 0x%lx", i * PAGE_SIZE);
    }
  }
  for (size_t i = first; i < first + pages; ++i) {
    bit_set(allocated, i, false);
  }
  stats.free_frames += pages;
  stats.allocated_frames -= pages;
}

struct pmm_stats pmm_get_stats(void)
{
  return stats;
}

#include <kernel/mm/pmm.h>
#include <kernel/mm/pressure.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

#define FRAMES_PER_WORD 64
#define PMM_BITMAP_COUNT 2

/* Unavailable includes allocations and reservations. Allocatable is immutable
 * after initialization, so reserved frames can never be freed by a caller.
 * Bits past frame_count stay unavailable, so whole words can be scanned. */
static uint64_t *unavailable_bitmap;
static uint64_t *allocatable_bitmap;
static size_t bitmap_words;
static size_t frame_count;

/* Protects unavailable_bitmap, first_candidate and stats after initialization.
 * A leaf lock: nothing is taken, woken or zeroed while holding it. */
static struct spinlock pmm_lock;
static struct pmm_stats stats;

/* Every frame below this one is unavailable, so searches may start here. */
static size_t first_candidate;

static bool bit_get(const uint64_t *bitmap, size_t frame)
{
  return (bitmap[frame / FRAMES_PER_WORD] >> (frame % FRAMES_PER_WORD)) & 1;
}

static void bit_set(uint64_t *bitmap, size_t frame, bool value)
{
  uint64_t mask = UINT64_C(1) << (frame % FRAMES_PER_WORD);
  if (value) {
    bitmap[frame / FRAMES_PER_WORD] |= mask;
  } else {
    bitmap[frame / FRAMES_PER_WORD] &= ~mask;
  }
}

static size_t bitmap_words_for_frames(size_t frames)
{
  return (frames + FRAMES_PER_WORD - 1) / FRAMES_PER_WORD;
}

static uint64_t page_up(uint64_t value)
{
  if (value > UINT64_MAX - (PAGE_SIZE - 1)) {
    panic("PMM address rounding overflow");
  }
  return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/* Bound before addition or alignment, including wholly out-of-range regions. */
static bool usable_region_pages(const struct boot_region *region,
                                uint64_t *start, uint64_t *end)
{
  if (region->type != BOOT_USABLE || region->base >= PMM_MAX_PHYS) {
    return false;
  }

  *start = page_up(region->base);
  uint64_t length = region->length;
  if (length > PMM_MAX_PHYS - region->base) {
    length = PMM_MAX_PHYS - region->base;
  }
  *end = (region->base + length) & ~(PAGE_SIZE - 1);
  return *start < *end;
}

static uint64_t usable_memory_end(const struct boot_info *boot)
{
  uint64_t limit = 0;
  bool clipped = false;

  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->type == BOOT_USABLE && region->length &&
        (region->base >= PMM_MAX_PHYS ||
         region->length > PMM_MAX_PHYS - region->base)) {
      clipped = true;
    }

    uint64_t start, end;
    if (usable_region_pages(region, &start, &end) && end > limit) {
      limit = end;
    }
  }
  if (clipped) {
    kwarn("PMM: ignoring usable memory at or above %lu GiB\n",
          (unsigned long)(PMM_MAX_PHYS >> 30));
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

  size_t bytes = bitmap_words_for_frames(plan.frame_count) * sizeof(uint64_t);
  plan.metadata_pages = page_up(bytes * PMM_BITMAP_COUNT) / PAGE_SIZE;
  size_t storage_bytes = plan.metadata_pages * PAGE_SIZE;

  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    uint64_t start, end;
    if (!usable_region_pages(region, &start, &end)) {
      continue;
    }

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

  KASSERT(!((uintptr_t)metadata % alignof(uint64_t)));

  unavailable_bitmap = metadata;
  allocatable_bitmap = unavailable_bitmap + bitmap_words;
}

void pmm_init(const struct boot_info *boot, struct pmm_bootstrap plan,
              void *metadata)
{
  KASSERT(!unavailable_bitmap);

  frame_count = plan.frame_count;
  bitmap_words = bitmap_words_for_frames(frame_count);
  pmm_rebase(metadata);
  memset(unavailable_bitmap, UINT8_MAX, bitmap_words * sizeof(uint64_t));
  memset(allocatable_bitmap, 0, bitmap_words * sizeof(uint64_t));

  stats.metadata_pages = plan.metadata_pages;
  uint64_t metadata_end = plan.metadata_phys + plan.metadata_pages * PAGE_SIZE;
  uint64_t kernel_end = boot->kernel_phys + boot->kernel_size;

  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    uint64_t start, end;
    if (!usable_region_pages(region, &start, &end)) {
      continue;
    }

    for (uint64_t physical = start; physical < end; physical += PAGE_SIZE) {
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

/* The first available frame at or after FRAME, or frame_count. Fully
 * unavailable words are skipped whole; the first available bit in a word is
 * its lowest clear bit. */
static size_t next_available(size_t frame)
{
  if (frame >= frame_count) {
    return frame_count;
  }
  size_t word = frame / FRAMES_PER_WORD;
  uint64_t available = ~unavailable_bitmap[word] & (UINT64_MAX << (frame % FRAMES_PER_WORD));
  while (!available) {
    if (++word == bitmap_words) {
      return frame_count;
    }
    available = ~unavailable_bitmap[word];
  }
  size_t found = word * FRAMES_PER_WORD + (size_t)__builtin_ctzll(available);
  return found < frame_count ? found : frame_count;
}

/* The first unavailable frame in [FRAME, LIMIT), or LIMIT. Scanning stops at
 * LIMIT, so a check for a short run never walks a long free stretch. */
static size_t next_unavailable(size_t frame, size_t limit)
{
  size_t word = frame / FRAMES_PER_WORD;
  uint64_t taken = unavailable_bitmap[word] & (UINT64_MAX << (frame % FRAMES_PER_WORD));
  while (!taken) {
    if ((++word) * FRAMES_PER_WORD >= limit) {
      return limit;
    }
    taken = unavailable_bitmap[word];
  }
  size_t found = word * FRAMES_PER_WORD + (size_t)__builtin_ctzll(taken);
  return found < limit ? found : limit;
}

/* First fit: the lowest run of PAGES available frames ending at or below
 * END_FRAME. A failed bounded search says nothing about frames above it. */
static phys_addr_t take_run_locked(size_t pages, size_t end_frame)
{
  if (pages > stats.free_frames) {
    return 0;
  }

  size_t lowest = next_available(first_candidate);
  for (size_t first = lowest; first < end_frame;) {
    size_t limit = pages <= end_frame - first ? first + pages : end_frame;
    size_t end = next_unavailable(first, limit);
    if (end - first == pages) {
      for (size_t i = first; i < end; ++i) {
        bit_set(unavailable_bitmap, i, true);
      }
      stats.free_frames -= pages;
      stats.allocated_frames += pages;
      first_candidate = first == lowest ? end : lowest;
      return first * PAGE_SIZE;
    }
    first = next_available(end);
  }
  first_candidate = lowest;
  return 0;
}

phys_addr_t pmm_alloc(size_t pages)
{
  return pmm_alloc_below(pages, UINT64_MAX);
}

phys_addr_t pmm_alloc_below(size_t pages, phys_addr_t maximum)
{
  KASSERT(unavailable_bitmap != NULL);
  if (!pages) {
    return 0;
  }

  /* Frame F is eligible when its last byte, F * PAGE_SIZE + PAGE_SIZE - 1,
   * is at or below MAXIMUM. */
  if (maximum < PAGE_SIZE - 1) {
    return 0;
  }
  uint64_t eligible = (maximum - (PAGE_SIZE - 1)) / PAGE_SIZE + 1;
  size_t end_frame = eligible < frame_count ? (size_t)eligible : frame_count;

  spin_lock(&pmm_lock);
  phys_addr_t physical = take_run_locked(pages, end_frame);
  /* Start asynchronous cache reclamation before physical exhaustion. */
  bool pressure = !physical || stats.free_frames < stats.total_frames / 16;
  spin_unlock(&pmm_lock);

  /* The notification can wake a task, which takes the queue lock. */
  if (pressure) {
    mm_pressure_notify();
  }
  return physical;
}

size_t pmm_alloc_frames(phys_addr_t *frames, size_t count)
{
  KASSERT(unavailable_bitmap && frames && count && count <= PMM_FRAME_BATCH_MAX);
  spin_lock(&pmm_lock);
  size_t allocated = 0;
  for (; allocated < count; ++allocated) {
    phys_addr_t physical = take_run_locked(1, frame_count);
    if (!physical) {
      break;
    }
    frames[allocated] = physical;
  }
  bool pressure = allocated < count || stats.free_frames < stats.total_frames / 16;
  spin_unlock(&pmm_lock);

  if (pressure) {
    mm_pressure_notify();
  }
  return allocated;
}

void pmm_free(phys_addr_t physical, size_t pages)
{
  size_t first = physical / PAGE_SIZE;
  if (!unavailable_bitmap || !physical || (physical & (PAGE_SIZE - 1)) || !pages ||
      first >= frame_count || pages > frame_count - first) {
    panic("PMM: invalid free phys=0x%lx pages=%zu", physical, pages);
  }

  spin_lock(&pmm_lock);
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
  if (first < first_candidate) {
    first_candidate = first;
  }

  stats.free_frames += pages;
  stats.allocated_frames -= pages;
  spin_unlock(&pmm_lock);
}

void pmm_free_frames(const phys_addr_t *frames, size_t count)
{
  KASSERT(unavailable_bitmap && frames && count && count <= PMM_FRAME_BATCH_MAX);
  /* The vector is private to this caller. Check its shape/duplicates outside
   * the leaf lock; allocation bits are validated together under the lock. */
  for (size_t i = 0; i < count; ++i) {
    phys_addr_t physical = frames[i];
    if (!physical || (physical & (PAGE_SIZE - 1)) ||
        physical / PAGE_SIZE >= frame_count) {
      panic("PMM: invalid free frame 0x%lx", physical);
    }
    for (size_t j = 0; j < i; ++j) {
      if (physical == frames[j]) {
        panic("PMM: duplicate free frame 0x%lx", physical);
      }
    }
  }

  spin_lock(&pmm_lock);
  for (size_t i = 0; i < count; ++i) {
    size_t frame = frames[i] / PAGE_SIZE;
    if (!bit_get(allocatable_bitmap, frame) || !bit_get(unavailable_bitmap, frame)) {
      panic("PMM: reserved or already-free frame 0x%lx", frames[i]);
    }
  }
  for (size_t i = 0; i < count; ++i) {
    size_t frame = frames[i] / PAGE_SIZE;
    bit_set(unavailable_bitmap, frame, false);
    if (frame < first_candidate) {
      first_candidate = frame;
    }
  }
  stats.free_frames += count;
  stats.allocated_frames -= count;
  spin_unlock(&pmm_lock);
}

struct pmm_stats pmm_get_stats(void)
{
  spin_lock(&pmm_lock);
  struct pmm_stats snapshot = stats;
  spin_unlock(&pmm_lock);
  return snapshot;
}

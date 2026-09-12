#include <arch/paging.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

enum range_state {
  RANGE_UNUSED,
  RANGE_FREE,
  RANGE_RESERVED,
  RANGE_BACKED,
};

struct vm_range {
  uintptr_t base;
  size_t pages;
  enum range_state state;
  struct vm_range *prev, *next;
};

/* An address-ordered partition of the allocation area. Fixed metadata breaks
 * the VM/heap bootstrap cycle; split nodes are obtained before mutating a range. */
static struct vm_range ranges[VM_MAX_RANGES];
static struct vm_range *head;
static size_t total_pages;

static bool page_count(size_t bytes, size_t *pages)
{
  if (!bytes || bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return false;
  }

  *pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
  return true;
}

static struct vm_range *take_record(void)
{
  for (size_t i = 0; i < VM_MAX_RANGES; ++i) {
    if (ranges[i].state == RANGE_UNUSED) {
      ranges[i].state = RANGE_RESERVED;
      return &ranges[i];
    }
  }
  return NULL;
}

static struct vm_range *find_range(void *base, size_t bytes)
{
  size_t pages;
  if (!head || !page_count(bytes, &pages) || ((uintptr_t)base & (PAGE_SIZE - 1))) {
    return NULL;
  }

  for (struct vm_range *range = head; range; range = range->next) {
    if (range->base == (uintptr_t)base && range->pages == pages) {
      return range;
    }
  }
  return NULL;
}

static void merge_next(struct vm_range *range)
{
  struct vm_range *next = range->next;
  KASSERT(range->state == RANGE_FREE && next && next->state == RANGE_FREE);
  KASSERT(range->base + range->pages * PAGE_SIZE == next->base);

  range->pages += next->pages;
  range->next = next->next;
  if (range->next) {
    range->next->prev = range;
  }

  *next = (struct vm_range){0};
}

static void release_range(struct vm_range *range)
{
  range->state = RANGE_FREE;
  if (range->next && range->next->state == RANGE_FREE) {
    merge_next(range);
  }
  if (range->prev && range->prev->state == RANGE_FREE) {
    merge_next(range->prev);
  }
}

void vm_init(void)
{
  KASSERT(!head);

  uintptr_t base = arch_vm_base();
  size_t bytes = arch_vm_size();
  KASSERT(base && !(base & (PAGE_SIZE - 1)) && bytes && !(bytes & (PAGE_SIZE - 1)));
  KASSERT(bytes <= UINTPTR_MAX - base);

  total_pages = bytes / PAGE_SIZE;
  head = &ranges[0];
  *head = (struct vm_range){.base = base, .pages = total_pages, .state = RANGE_FREE};
}

/* Allocate both split records before changing links. A metadata failure must
 * leave the original free range intact. */
static enum mm_result split_free_range(struct vm_range *range, uintptr_t aligned,
                                      size_t pages)
{
  size_t prefix_pages = (aligned - range->base) / PAGE_SIZE;
  size_t suffix_pages = range->pages - prefix_pages - pages;

  struct vm_range *before = prefix_pages ? take_record() : NULL;
  if (prefix_pages && !before) {
    return MM_NO_MEMORY;
  }

  struct vm_range *after = suffix_pages ? take_record() : NULL;
  if (suffix_pages && !after) {
    if (before) {
      *before = (struct vm_range){0};
    }
    return MM_NO_MEMORY;
  }

  if (before) {
    *before = (struct vm_range){
      .base = range->base,
      .pages = prefix_pages,
      .state = RANGE_FREE,
      .prev = range->prev,
      .next = range,
    };
    if (range->prev) {
      range->prev->next = before;
    } else {
      head = before;
    }
    range->prev = before;
  }

  if (after) {
    *after = (struct vm_range){
      .base = aligned + pages * PAGE_SIZE,
      .pages = suffix_pages,
      .state = RANGE_FREE,
      .prev = range,
      .next = range->next,
    };
    if (range->next) {
      range->next->prev = after;
    }
    range->next = after;
  }

  range->base = aligned;
  range->pages = pages;
  range->state = RANGE_RESERVED;
  return MM_OK;
}

enum mm_result vm_reserve(size_t bytes, size_t alignment, void **result)
{
  if (!result) {
    return MM_INVALID;
  }

  *result = NULL;
  size_t pages;
  if (!head || !page_count(bytes, &pages) || alignment < PAGE_SIZE ||
      (alignment & (alignment - 1))) {
    return MM_INVALID;
  }

  for (struct vm_range *range = head; range; range = range->next) {
    if (range->state != RANGE_FREE || range->base > UINTPTR_MAX - (alignment - 1)) {
      continue;
    }

    uintptr_t aligned = (range->base + alignment - 1) & ~(alignment - 1);
    size_t prefix_pages = (aligned - range->base) / PAGE_SIZE;
    if (prefix_pages > range->pages || pages > range->pages - prefix_pages) {
      continue;
    }

    enum mm_result status = split_free_range(range, aligned, pages);
    if (status != MM_OK) {
      return status;
    }

    *result = (void *)aligned;
    return MM_OK;
  }
  return MM_NO_MEMORY;
}

static enum mm_result require_unmapped(const struct vm_range *range)
{
  for (size_t i = 0; i < range->pages; ++i) {
    struct page_translation translation;
    enum mm_result status = arch_page_query(range->base + i * PAGE_SIZE,
                                            &translation);
    if (status == MM_OK) {
      return MM_COLLISION;
    }
    if (status != MM_NOT_MAPPED) {
      return status;
    }
  }
  return MM_OK;
}

enum mm_result vm_release(void *base, size_t bytes)
{
  struct vm_range *range = find_range(base, bytes);
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }

  enum mm_result status = require_unmapped(range);
  if (status != MM_OK) {
    return status;
  }

  release_range(range);
  return MM_OK;
}

static void free_backing(const struct vm_range *range, size_t pages)
{
  for (size_t i = 0; i < pages; ++i) {
    phys_addr_t physical;
    KASSERT(arch_page_unmap(range->base + i * PAGE_SIZE, &physical) == MM_OK);
    pmm_free(physical, 1);
  }
}

enum mm_result vm_back(void *base, size_t bytes, unsigned permissions)
{
  struct vm_range *range = find_range(base, bytes);
  if (!range || range->state != RANGE_RESERVED ||
      (permissions & ~(PAGE_WRITE | PAGE_EXEC)) ||
      permissions == (PAGE_WRITE | PAGE_EXEC)) {
    return MM_INVALID;
  }

  enum mm_result status = require_unmapped(range);
  if (status != MM_OK) {
    return status;
  }

  size_t mapped_pages = 0;
  for (; mapped_pages < range->pages; ++mapped_pages) {
    phys_addr_t physical = pmm_alloc(1);
    if (!physical) {
      status = MM_NO_MEMORY;
      break;
    }

    arch_frame_zero(physical);
    status = arch_page_map(range->base + mapped_pages * PAGE_SIZE, physical,
                           permissions);
    if (status != MM_OK) {
      pmm_free(physical, 1);
      break;
    }
  }

  if (mapped_pages != range->pages) {
    /* Only this prefix owns frames; the remainder is still just reserved. */
    free_backing(range, mapped_pages);
    return status;
  }

  range->state = RANGE_BACKED;
  return MM_OK;
}

enum mm_result vm_alloc(size_t bytes, size_t alignment, unsigned permissions,
                        void **result)
{
  enum mm_result status = vm_reserve(bytes, alignment, result);
  if (status != MM_OK) {
    return status;
  }

  status = vm_back(*result, bytes, permissions);
  if (status != MM_OK) {
    KASSERT(vm_release(*result, bytes) == MM_OK);
    *result = NULL;
  }
  return status;
}

enum mm_result vm_free(void *base, size_t bytes)
{
  struct vm_range *range = find_range(base, bytes);
  if (!range || range->state != RANGE_BACKED) {
    return MM_INVALID;
  }

  free_backing(range, range->pages);
  release_range(range);
  return MM_OK;
}

struct vm_stats vm_get_stats(void)
{
  struct vm_stats stats = {.total_pages = total_pages};

  for (struct vm_range *range = head; range; range = range->next) {
    ++stats.range_records;
    if (range->state == RANGE_RESERVED || range->state == RANGE_BACKED) {
      stats.reserved_pages += range->pages;
    }
    if (range->state == RANGE_BACKED) {
      stats.backed_pages += range->pages;
    }
  }
  return stats;
}

#include <arch/paging.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

enum range_state { RANGE_UNUSED, RANGE_FREE, RANGE_RESERVED, RANGE_BACKED };

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
  for (struct vm_range *r = head; r; r = r->next) {
    if (r->base == (uintptr_t)base && r->pages == pages) {
      return r;
    }
  }
  return NULL;
}

static void merge_next(struct vm_range *r)
{
  struct vm_range *next = r->next;
  KASSERT(r->state == RANGE_FREE && next && next->state == RANGE_FREE);
  KASSERT(r->base + r->pages * PAGE_SIZE == next->base);
  r->pages += next->pages;
  r->next = next->next;
  if (r->next) {
    r->next->prev = r;
  }
  *next = (struct vm_range){0};
}

static void release_range(struct vm_range *r)
{
  r->state = RANGE_FREE;
  if (r->next && r->next->state == RANGE_FREE) {
    merge_next(r);
  }
  if (r->prev && r->prev->state == RANGE_FREE) {
    merge_next(r->prev);
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
  for (struct vm_range *r = head; r; r = r->next) {
    if (r->state != RANGE_FREE || r->base > UINTPTR_MAX - (alignment - 1)) {
      continue;
    }
    uintptr_t aligned = (r->base + alignment - 1) & ~(alignment - 1);
    size_t prefix = (aligned - r->base) / PAGE_SIZE;
    if (prefix > r->pages || pages > r->pages - prefix) {
      continue;
    }
    size_t suffix = r->pages - prefix - pages;
    struct vm_range *before = prefix ? take_record() : NULL;
    if (prefix && !before) {
      return MM_NO_MEMORY;
    }
    struct vm_range *after = suffix ? take_record() : NULL;
    if (suffix && !after) {
      if (before) {
        *before = (struct vm_range){0};
      }
      return MM_NO_MEMORY;
    }
    if (before) {
      *before = (struct vm_range){r->base, prefix, RANGE_FREE, r->prev, r};
      if (r->prev) {
        r->prev->next = before;
      } else {
        head = before;
      }
      r->prev = before;
    }
    if (after) {
      *after = (struct vm_range){aligned + pages * PAGE_SIZE, suffix, RANGE_FREE, r, r->next};
      if (r->next) {
        r->next->prev = after;
      }
      r->next = after;
    }
    r->base = aligned;
    r->pages = pages;
    r->state = RANGE_RESERVED;
    *result = (void *)aligned;
    return MM_OK;
  }
  return MM_NO_MEMORY;
}

static enum mm_result require_unmapped(const struct vm_range *r)
{
  for (size_t i = 0; i < r->pages; ++i) {
    struct page_translation translation;
    enum mm_result status = arch_page_query(r->base + i * PAGE_SIZE, &translation);
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
  struct vm_range *r = find_range(base, bytes);
  if (!r || r->state != RANGE_RESERVED) {
    return MM_INVALID;
  }
  enum mm_result status = require_unmapped(r);
  if (status != MM_OK) {
    return status;
  }
  release_range(r);
  return MM_OK;
}

static void free_backing(const struct vm_range *r, size_t pages)
{
  for (size_t i = 0; i < pages; ++i) {
    phys_addr_t physical;
    KASSERT(arch_page_unmap(r->base + i * PAGE_SIZE, &physical) == MM_OK);
    pmm_free(physical, 1);
  }
}

enum mm_result vm_back(void *base, size_t bytes, unsigned permissions)
{
  struct vm_range *r = find_range(base, bytes);
  if (!r || r->state != RANGE_RESERVED || (permissions & ~(PAGE_WRITE | PAGE_EXEC)) ||
      permissions == (PAGE_WRITE | PAGE_EXEC)) {
    return MM_INVALID;
  }
  enum mm_result status = require_unmapped(r);
  if (status != MM_OK) {
    return status;
  }
  size_t done = 0;
  for (; done < r->pages; ++done) {
    phys_addr_t physical = pmm_alloc(1);
    if (!physical) {
      status = MM_NO_MEMORY;
      break;
    }
    arch_frame_zero(physical);
    status = arch_page_map(r->base + done * PAGE_SIZE, physical, permissions);
    if (status != MM_OK) {
      pmm_free(physical, 1);
      break;
    }
  }
  if (done != r->pages) {
    free_backing(r, done);
    return status;
  }
  r->state = RANGE_BACKED;
  return MM_OK;
}

enum mm_result vm_alloc(size_t bytes, size_t alignment, unsigned permissions, void **result)
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
  struct vm_range *r = find_range(base, bytes);
  if (!r || r->state != RANGE_BACKED) {
    return MM_INVALID;
  }
  free_backing(r, r->pages);
  release_range(r);
  return MM_OK;
}

struct vm_stats vm_get_stats(void)
{
  struct vm_stats stats = {.total_pages = total_pages};
  for (struct vm_range *r = head; r; r = r->next) {
    ++stats.range_records;
    if (r->state == RANGE_RESERVED || r->state == RANGE_BACKED) {
      stats.reserved_pages += r->pages;
    }
    if (r->state == RANGE_BACKED) {
      stats.backed_pages += r->pages;
    }
  }
  return stats;
}

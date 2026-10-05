#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/heap.h>
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

struct vm_space {
  struct arch_address_space arch;
  struct vm_range *head;
  size_t total_pages;
};

static struct vm_space kernel_space;

/* The general kernel area stays with the BSP. Its ranges are reused, and
 * changing a reused mapping that another CPU may hold would need a remote TLB
 * shootdown. Heap growth maps its own arena instead. */
static void require_kernel_owner(const struct vm_space *space)
{
  if (space == &kernel_space) {
    KASSERT(arch_cpu_index() == 0);
  }
}

static struct arch_address_space *page_space(struct vm_space *space)
{
  return space == &kernel_space ? arch_kernel_space() : &space->arch;
}

/* Each space partitions its allocation area in address order. Only the kernel
 * needs fixed metadata to break the VM/heap bootstrap cycle. */
static struct vm_range kernel_ranges[VM_MAX_RANGES];

static bool permissions_valid(const struct vm_space *space, unsigned permissions)
{
  bool user = permissions & PAGE_USER;
  return !(permissions & ~(PAGE_WRITE | PAGE_EXEC | PAGE_USER)) &&
         (permissions & (PAGE_WRITE | PAGE_EXEC)) != (PAGE_WRITE | PAGE_EXEC) &&
         user == (space != &kernel_space);
}

static bool page_count(size_t bytes, size_t *pages)
{
  if (!bytes || bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return false;
  }

  *pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
  return true;
}

static struct vm_range *take_record(struct vm_space *space)
{
  if (space != &kernel_space) {
    struct vm_range *range = kmalloc(sizeof(*range));
    if (range) {
      *range = (struct vm_range){.state = RANGE_RESERVED};
    }
    return range;
  }

  for (size_t i = 0; i < VM_MAX_RANGES; ++i) {
    if (kernel_ranges[i].state == RANGE_UNUSED) {
      kernel_ranges[i].state = RANGE_RESERVED;
      return &kernel_ranges[i];
    }
  }
  return NULL;
}

static void drop_record(struct vm_space *space, struct vm_range *range)
{
  if (space == &kernel_space) {
    *range = (struct vm_range){0};
  } else {
    kfree(range);
  }
}

static struct vm_range *find_range(struct vm_space *space, uintptr_t base,
                                  size_t bytes)
{
  size_t pages;
  if (!space || !space->head || !page_count(bytes, &pages) ||
      (base & (PAGE_SIZE - 1))) {
    return NULL;
  }

  for (struct vm_range *range = space->head; range; range = range->next) {
    if (range->base == base && range->pages == pages) {
      return range;
    }
  }
  return NULL;
}

static void merge_next(struct vm_space *space, struct vm_range *range)
{
  struct vm_range *next = range->next;
  KASSERT(range->state == RANGE_FREE && next && next->state == RANGE_FREE);
  KASSERT(range->base + range->pages * PAGE_SIZE == next->base);

  range->pages += next->pages;
  range->next = next->next;
  if (range->next) {
    range->next->prev = range;
  }

  drop_record(space, next);
}

static void release_range(struct vm_space *space, struct vm_range *range)
{
  range->state = RANGE_FREE;
  if (range->next && range->next->state == RANGE_FREE) {
    merge_next(space, range);
  }
  if (range->prev && range->prev->state == RANGE_FREE) {
    merge_next(space, range->prev);
  }
}

void vm_init(void)
{
  KASSERT(!kernel_space.head);

  uintptr_t base = arch_vm_base();
  size_t bytes = arch_vm_size();
  KASSERT(base && !(base & (PAGE_SIZE - 1)) && bytes && !(bytes & (PAGE_SIZE - 1)));
  KASSERT(bytes <= UINTPTR_MAX - base);

  kernel_space.total_pages = bytes / PAGE_SIZE;
  kernel_space.head = &kernel_ranges[0];
  *kernel_space.head = (struct vm_range){
    .base = base, .pages = kernel_space.total_pages, .state = RANGE_FREE,
  };
}

/* Allocate both split records before changing links. A metadata failure must
 * leave the original free range intact. */
static enum mm_result split_free_range(struct vm_space *space,
                                      struct vm_range *range, uintptr_t aligned,
                                      size_t pages)
{
  size_t prefix_pages = (aligned - range->base) / PAGE_SIZE;
  size_t suffix_pages = range->pages - prefix_pages - pages;

  struct vm_range *before = prefix_pages ? take_record(space) : NULL;
  if (prefix_pages && !before) {
    return MM_NO_MEMORY;
  }

  struct vm_range *after = suffix_pages ? take_record(space) : NULL;
  if (suffix_pages && !after) {
    if (before) {
      drop_record(space, before);
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
      space->head = before;
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

enum mm_result vm_reserve(struct vm_space *space, size_t bytes, size_t alignment,
                          uintptr_t *result)
{
  require_kernel_owner(space);
  if (!result) {
    return MM_INVALID;
  }

  *result = 0;
  size_t pages;
  if (!space || !space->head || !page_count(bytes, &pages) ||
      alignment < PAGE_SIZE || (alignment & (alignment - 1))) {
    return MM_INVALID;
  }

  for (struct vm_range *range = space->head; range; range = range->next) {
    if (range->state != RANGE_FREE || range->base > UINTPTR_MAX - (alignment - 1)) {
      continue;
    }

    uintptr_t aligned = (range->base + alignment - 1) & ~(alignment - 1);
    size_t prefix_pages = (aligned - range->base) / PAGE_SIZE;
    if (prefix_pages > range->pages || pages > range->pages - prefix_pages) {
      continue;
    }

    enum mm_result status = split_free_range(space, range, aligned, pages);
    if (status != MM_OK) {
      return status;
    }

    *result = aligned;
    return MM_OK;
  }
  return MM_NO_MEMORY;
}

enum mm_result vm_reserve_at(struct vm_space *space, uintptr_t base, size_t bytes)
{
  require_kernel_owner(space);
  size_t pages;
  if (!space || !space->head || (base & (PAGE_SIZE - 1)) ||
      !page_count(bytes, &pages)) {
    return MM_INVALID;
  }

  uintptr_t area_base = space == &kernel_space ?
    arch_vm_base() : arch_user_vm_base();
  if (base < area_base || (base - area_base) / PAGE_SIZE >= space->total_pages ||
      pages > space->total_pages - (base - area_base) / PAGE_SIZE) {
    return MM_INVALID;
  }

  for (struct vm_range *range = space->head; range; range = range->next) {
    if (base < range->base || (base - range->base) / PAGE_SIZE >= range->pages) {
      continue;
    }

    size_t offset_pages = (base - range->base) / PAGE_SIZE;
    if (range->state != RANGE_FREE || pages > range->pages - offset_pages) {
      return MM_COLLISION;
    }
    return split_free_range(space, range, base, pages);
  }
  return MM_INVALID;
}

static enum mm_result require_unmapped(struct vm_space *space,
                                       const struct vm_range *range)
{
  for (size_t i = 0; i < range->pages; ++i) {
    struct page_translation translation;
    enum mm_result status = arch_page_query(page_space(space),
                                            range->base + i * PAGE_SIZE,
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

enum mm_result vm_release(struct vm_space *space, uintptr_t base, size_t bytes)
{
  require_kernel_owner(space);
  struct vm_range *range = find_range(space, base, bytes);
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }

  enum mm_result status = require_unmapped(space, range);
  if (status != MM_OK) {
    return status;
  }

  release_range(space, range);
  return MM_OK;
}

static void free_backing(struct vm_space *space, const struct vm_range *range,
                         size_t pages)
{
  for (size_t i = 0; i < pages; ++i) {
    phys_addr_t physical;
    KASSERT(arch_page_unmap(page_space(space), range->base + i * PAGE_SIZE,
                             &physical) == MM_OK);
    pmm_free(physical, 1);
  }
}

enum mm_result vm_back(struct vm_space *space, uintptr_t base, size_t bytes,
                       unsigned permissions)
{
  require_kernel_owner(space);
  struct vm_range *range = find_range(space, base, bytes);
  if (!range || range->state != RANGE_RESERVED ||
      !permissions_valid(space, permissions)) {
    return MM_INVALID;
  }

  enum mm_result status = require_unmapped(space, range);
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
    status = arch_page_map(page_space(space),
                           range->base + mapped_pages * PAGE_SIZE, physical,
                           permissions);
    if (status != MM_OK) {
      pmm_free(physical, 1);
      break;
    }
  }

  if (mapped_pages != range->pages) {
    /* Only this prefix owns frames; the remainder is still just reserved. */
    free_backing(space, range, mapped_pages);
    return status;
  }

  range->state = RANGE_BACKED;
  return MM_OK;
}

enum mm_result vm_alloc(struct vm_space *space, size_t bytes, size_t alignment,
                        unsigned permissions, uintptr_t *result)
{
  enum mm_result status = vm_reserve(space, bytes, alignment, result);
  if (status != MM_OK) {
    return status;
  }

  status = vm_back(space, *result, bytes, permissions);
  if (status != MM_OK) {
    KASSERT(vm_release(space, *result, bytes) == MM_OK);
    *result = 0;
  }
  return status;
}

enum mm_result vm_alloc_at(struct vm_space *space, uintptr_t base, size_t bytes,
                           unsigned permissions)
{
  enum mm_result status = vm_reserve_at(space, base, bytes);
  if (status != MM_OK) {
    return status;
  }

  status = vm_back(space, base, bytes, permissions);
  if (status != MM_OK) {
    KASSERT(vm_release(space, base, bytes) == MM_OK);
  }
  return status;
}

enum mm_result vm_free(struct vm_space *space, uintptr_t base, size_t bytes)
{
  require_kernel_owner(space);
  struct vm_range *range = find_range(space, base, bytes);
  if (!range || range->state != RANGE_BACKED) {
    return MM_INVALID;
  }

  free_backing(space, range, range->pages);
  release_range(space, range);
  return MM_OK;
}

struct vm_stats vm_get_stats(const struct vm_space *space)
{
  require_kernel_owner(space);
  struct vm_stats stats = {0};
  if (!space) {
    return stats;
  }
  stats.total_pages = space->total_pages;

  for (struct vm_range *range = space->head; range; range = range->next) {
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

struct vm_space *vm_kernel_space(void)
{
  return &kernel_space;
}

enum mm_result vm_space_create(struct vm_space **result)
{
  if (!result) {
    return MM_INVALID;
  }
  *result = NULL;
  if (!kernel_space.head) {
    return MM_INVALID;
  }

  struct vm_space *space = kmalloc(sizeof(*space));
  if (!space) {
    return MM_NO_MEMORY;
  }
  *space = (struct vm_space){0};

  struct vm_range *range = take_record(space);
  if (!range) {
    kfree(space);
    return MM_NO_MEMORY;
  }

  enum mm_result status = arch_space_create(&space->arch);
  if (status != MM_OK) {
    drop_record(space, range);
    kfree(space);
    return status;
  }

  space->total_pages = arch_user_vm_size() / PAGE_SIZE;
  *range = (struct vm_range){
    .base = arch_user_vm_base(),
    .pages = space->total_pages,
    .state = RANGE_FREE,
  };
  space->head = range;
  *result = space;
  return MM_OK;
}

enum mm_result vm_space_activate(struct vm_space *space)
{
  if (!space || !space->head) {
    return MM_INVALID;
  }
  return arch_space_activate(page_space(space));
}

enum mm_result vm_space_destroy(struct vm_space *space)
{
  if (!space || space == &kernel_space || arch_space_active(page_space(space))) {
    return MM_INVALID;
  }

  /* Check borrowed mappings before freeing anything. A rejected destruction
   * must leave the entire space usable, including its VM-owned allocations. */
  for (struct vm_range *range = space->head; range; range = range->next) {
    if (range->state == RANGE_RESERVED) {
      enum mm_result status = require_unmapped(space, range);
      if (status != MM_OK) {
        return status;
      }
    }
  }

  for (struct vm_range *range = space->head; range;) {
    struct vm_range *next = range->next;
    if (range->state == RANGE_BACKED) {
      free_backing(space, range, range->pages);
    }
    drop_record(space, range);
    range = next;
  }

  KASSERT(arch_space_destroy(&space->arch) == MM_OK);
  kfree(space);
  return MM_OK;
}

static struct vm_range *range_containing(struct vm_space *space, uintptr_t base)
{
  if (!space || (base & (PAGE_SIZE - 1))) {
    return NULL;
  }

  for (struct vm_range *range = space->head; range; range = range->next) {
    if (base >= range->base && (base - range->base) / PAGE_SIZE < range->pages) {
      return range;
    }
  }
  return NULL;
}

enum mm_result vm_map(struct vm_space *space, uintptr_t base, phys_addr_t physical,
                      unsigned permissions)
{
  require_kernel_owner(space);
  struct vm_range *range = range_containing(space, base);
  if (!range || range->state != RANGE_RESERVED ||
      !permissions_valid(space, permissions)) {
    return MM_INVALID;
  }
  return arch_page_map(page_space(space), base, physical, permissions);
}

enum mm_result vm_map_mmio(uintptr_t base, phys_addr_t physical)
{
  require_kernel_owner(&kernel_space);
  struct vm_range *range = range_containing(&kernel_space, base);
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }
  return arch_page_map_mmio(base, physical);
}

enum mm_result vm_unmap(struct vm_space *space, uintptr_t base,
                        phys_addr_t *physical)
{
  require_kernel_owner(space);
  struct vm_range *range = range_containing(space, base);
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }
  return arch_page_unmap(page_space(space), base, physical);
}

enum mm_result vm_protect(struct vm_space *space, uintptr_t base,
                          unsigned permissions)
{
  require_kernel_owner(space);
  struct vm_range *range = range_containing(space, base);
  if (!range || (range->state != RANGE_RESERVED && range->state != RANGE_BACKED) ||
      !permissions_valid(space, permissions)) {
    return MM_INVALID;
  }
  return arch_page_protect(page_space(space), base, permissions);
}

enum mm_result vm_query(struct vm_space *space, uintptr_t address,
                        struct page_translation *result)
{
  require_kernel_owner(space);
  if (!space || !space->head) {
    return MM_INVALID;
  }
  return arch_page_query(page_space(space), address, result);
}

bool vm_user_buffer_accessible(struct vm_space *space, uintptr_t address,
                               size_t bytes, bool write)
{
  if (!space || space == &kernel_space) {
    return false;
  }
  return arch_user_buffer_accessible(page_space(space), address, bytes, write);
}

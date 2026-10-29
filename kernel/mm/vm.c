#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/memory.h>
#include <kernel/object/object.h>
#include <kernel/object/execution_group.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/panic.h>
#include <kernel/user/address_wait.h>
#include <stdatomic.h>

enum range_state {
  RANGE_UNUSED,
  RANGE_FREE,
  RANGE_RESERVED,
  RANGE_BACKED,
  RANGE_SPARSE,
  RANGE_WINDOW,
  RANGE_RETIRED,
};

/* Metadata and token work never scale with an unused reservation span. */
#define SPARSE_EXTENT_PAGES 256
#define KERNEL_FLUSH_TIMEOUT_NS UINT64_C(1000000000)

struct vm_extent {
  uintptr_t base;
  size_t pages, token_capacity;
  struct vm_extent *next;
  uint64_t tokens[];
};

struct vm_range {
  uintptr_t base;
  size_t pages;
  size_t backed_pages; /* Eager suffix length, or total sparse backing. */
  uint64_t *wait_tokens; /* Private owned suffix; zero entries are ineligible. */
  struct vm_extent *extents;
  bool sparse_withdrawn; /* Retired-space destruction only. */
  bool quarantined; /* An unconfirmed shared retirement: never clear/reuse. */
  bool retirement_confirm_remote;
  struct execution_group *retirement_group;
  struct vm_range *retirement_next;
  enum range_state state;
  struct vm_range *prev, *next;
};

struct vm_space {
  struct arch_address_space arch;
  struct vm_range *head;
  size_t total_pages;
  atomic_size_t waiters;
  /* Sum of ranges' backed_pages. Owners update it with each change; it is
   * readable without ownership while the space exists, as a gauge. */
  atomic_size_t backed_pages;
  bool retired; /* Owner committed to release; no other operation follows. */
};

/* Frames unmapped per leaf-table visit before freeing; stack-sized. */
#define BACKING_RUN_PAGES 64
_Static_assert(BACKING_RUN_PAGES <= PMM_FRAME_BATCH_MAX, "bounded PMM frame run");

static struct vm_space kernel_space;
/* Any-CPU reservation; never reused, even after failed provisional backing. */
static atomic_uint_fast64_t next_wait_token = 1;
/* Kernel range links/counters and retained cleanup queue. No allocation/wait
 * under this guard; structural exclusion belongs to the logical owner. */
static struct spinlock registry_guard;
static struct vm_range *retired_first, *retired_last;
struct ram_retirement_queue {
  struct vm_range *first, *last;
};
static struct ram_retirement_queue *ram_retirement_queues;
static size_t ram_retirement_cpu_count;

static void require_kernel_owner(const struct vm_space *space)
{
  if (space == &kernel_space) {
    KASSERT(vm_kernel_owner_held());
  }
}

static void registry_lock(const struct vm_space *space)
{
  if (space == &kernel_space) {
    spin_lock(&registry_guard);
  }
}

static void registry_unlock(const struct vm_space *space)
{
  if (space == &kernel_space) {
    spin_unlock(&registry_guard);
  }
}

static void backed_add(struct vm_space *space, size_t pages)
{
  atomic_fetch_add_explicit(&space->backed_pages, pages, memory_order_relaxed);
}

static void backed_remove(struct vm_space *space, size_t pages)
{
  atomic_fetch_sub_explicit(&space->backed_pages, pages, memory_order_relaxed);
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

static enum mm_result reserve_wait_tokens(size_t count, uint64_t *first)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(count && first);
  uint64_t value = atomic_load_explicit(&next_wait_token, memory_order_relaxed);
  for (;;) {
    if (!value || count - 1 > UINT64_MAX - value) {
      return MM_NO_MEMORY;
    }
    uint64_t last = value + count - 1;
    if (atomic_compare_exchange_weak_explicit(&next_wait_token, &value,
          last == UINT64_MAX ? 0 : last + 1, memory_order_relaxed, memory_order_relaxed)) {
      *first = value;
      return MM_OK;
    }
  }
}

static void withdraw_wait_pages(struct vm_space *space, uintptr_t base, size_t bytes)
{
  KASSERT(space && space != &kernel_space);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  /* Root/borrower drainage prevents enrollment while this hint is sampled. */
  if (atomic_load_explicit(&space->waiters, memory_order_relaxed)) {
    address_wait_withdraw(space, base, bytes);
  }
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

static struct vm_range *range_containing(struct vm_space *space, uintptr_t base);

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
  KASSERT(!range->quarantined && !range->retirement_group && !range->retirement_next);
  range->state = RANGE_FREE;
  backed_remove(space, range->backed_pages);
  range->backed_pages = 0;
  KASSERT(!range->wait_tokens);
  KASSERT(!range->extents);
  range->sparse_withdrawn = false;
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
  atomic_init(&kernel_space.waiters, 0);
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

static enum mm_result reserve_range(struct vm_space *space, size_t bytes, size_t alignment,
                          uintptr_t *result)
{
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

static enum mm_result reserve_at(struct vm_space *space, uintptr_t base, size_t bytes)
{
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

enum mm_result vm_reserve(struct vm_space *space, size_t bytes, size_t alignment,
    uintptr_t *result)
{
  require_kernel_owner(space);
  registry_lock(space);
  enum mm_result status = reserve_range(space, bytes, alignment, result);
  registry_unlock(space);
  return status;
}

enum mm_result vm_reserve_at(struct vm_space *space, uintptr_t base, size_t bytes)
{
  require_kernel_owner(space);
  registry_lock(space);
  enum mm_result status = reserve_at(space, base, bytes);
  registry_unlock(space);
  return status;
}

static enum mm_result require_unmapped(struct vm_space *space,
                                       const struct vm_range *range)
{
  return arch_range_vacant(page_space(space), range->base, range->pages);
}

enum mm_result vm_release(struct vm_space *space, uintptr_t base, size_t bytes)
{
  require_kernel_owner(space);
  struct vm_range *range = find_range(space, base, bytes);
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }

  enum mm_result status = require_unmapped(space, range);
  if (status != MM_OK) {
    return status;
  }

  registry_lock(space);
  release_range(space, range);
  registry_unlock(space);
  return MM_OK;
}

static void free_backing(struct vm_space *space, uintptr_t base,
                         size_t pages)
{
  phys_addr_t frames[BACKING_RUN_PAGES];
  while (pages) {
    size_t count = arch_page_unmap_run(page_space(space), base,
        pages < BACKING_RUN_PAGES ? pages : BACKING_RUN_PAGES, frames);
    KASSERT(count);
    pmm_free_frames(frames, count);
    base += count * PAGE_SIZE;
    pages -= count;
  }
}

static uintptr_t backing_base(const struct vm_range *range)
{
  return range->base + (range->pages - range->backed_pages) * PAGE_SIZE;
}

static void drop_wait_tokens(struct vm_space *space, struct vm_range *range)
{
  if (range->wait_tokens) {
    withdraw_wait_pages(space, backing_base(range), range->backed_pages * PAGE_SIZE);
    kfree(range->wait_tokens);
    range->wait_tokens = NULL;
  }
}

enum mm_result vm_back_begin(struct vm_space *space, uintptr_t base, size_t bytes,
                             unsigned permissions, struct vm_backing *backing)
{
  require_kernel_owner(space);
  *backing = (struct vm_backing){0};
  size_t pages;
  struct vm_range *range = range_containing(space, base);
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_RESERVED ||
      !page_count(bytes, &pages) ||
      pages != range->pages - (base - range->base) / PAGE_SIZE ||
      !permissions_valid(space, permissions)) {
    return MM_INVALID;
  }

  enum mm_result status = require_unmapped(space, range);
  if (status != MM_OK) {
    return status;
  }

  uint64_t *tokens = NULL;
  uint64_t first_token = 0;
  if (space != &kernel_space) {
    if (pages > SIZE_MAX / sizeof(*tokens)) {
      return MM_NO_MEMORY;
    }
    tokens = kmalloc(pages * sizeof(*tokens));
    if (!tokens) {
      return MM_NO_MEMORY;
    }
    if (permissions & PAGE_WRITE) {
      status = reserve_wait_tokens(pages, &first_token);
      if (status != MM_OK) {
        kfree(tokens);
        return status;
      }
    }
  }

  *backing = (struct vm_backing){
    .range = range, .base = base, .pages = pages, .permissions = permissions,
    .tokens = tokens, .first_token = first_token, .status = MM_OK,
  };
  return MM_OK;
}

/* Allocates, zeroes and maps up to count frames within one leaf table. */
static enum mm_result back_run(struct vm_space *space, uintptr_t base,
                               size_t count, unsigned permissions, size_t *mapped)
{
  phys_addr_t frames[BACKING_RUN_PAGES];
  size_t allocated = pmm_alloc_frames(frames, count);
  for (size_t i = 0; i < allocated; ++i) {
    arch_frame_zero(frames[i]);
  }
  enum mm_result status = allocated ? MM_OK : MM_NO_MEMORY;
  *mapped = 0;
  /* A run maps within one leaf table, so a batch can take two. */
  while (status == MM_OK && *mapped < allocated) {
    size_t count_mapped;
    status = arch_page_map_run(page_space(space),
        base + *mapped * PAGE_SIZE, allocated - *mapped,
        frames + *mapped, permissions, &count_mapped);
    *mapped += count_mapped;
  }
  if (*mapped < allocated) {
    pmm_free_frames(frames + *mapped, allocated - *mapped);
  }
  if (status == MM_OK && allocated < count) {
    status = MM_NO_MEMORY;
  }
  return status;
}

bool vm_back_step(struct vm_space *space, struct vm_backing *backing, size_t budget)
{
  require_kernel_owner(space);
  for (;;) {
    if (backing->status != MM_OK) {
      /* Unwind from the end, so the remaining backing stays a prefix. */
      if (!backing->mapped) {
        kfree(backing->tokens);
        backing->tokens = NULL;
        return true;
      }
      if (!budget) {
        return false;
      }
      size_t pages = backing->mapped < budget ? backing->mapped : budget;
      free_backing(space, backing->base + (backing->mapped - pages) * PAGE_SIZE, pages);
      backing->mapped -= pages;
      budget -= pages;
      continue;
    }

    if (backing->mapped == backing->pages) {
      struct vm_range *range = backing->range;
      if (backing->tokens) {
        for (size_t i = 0; i < backing->pages; ++i) {
          backing->tokens[i] = backing->first_token ? backing->first_token + i : 0;
        }
      }
      registry_lock(space);
      range->wait_tokens = backing->tokens;
      backed_add(space, backing->pages - range->backed_pages);
      range->backed_pages = backing->pages;
      range->state = RANGE_BACKED;
      registry_unlock(space);
      backing->tokens = NULL;
      return true;
    }
    if (!budget) {
      return false;
    }

    size_t count = backing->pages - backing->mapped;
    count = count < budget ? count : budget;
    count = count < BACKING_RUN_PAGES ? count : BACKING_RUN_PAGES;
    size_t mapped;
    enum mm_result status = back_run(space, backing->base + backing->mapped * PAGE_SIZE,
        count, backing->permissions, &mapped);
    backing->mapped += mapped;
    budget -= mapped ? mapped : 1;
    if (status != MM_OK) {
      backing->status = status;
    }
  }
}

enum mm_result vm_back(struct vm_space *space, uintptr_t base, size_t bytes,
                       unsigned permissions)
{
  struct vm_backing backing;
  enum mm_result status = vm_back_begin(space, base, bytes, permissions, &backing);
  if (status != MM_OK) {
    return status;
  }
  /* An unlimited budget finishes in one call. */
  KASSERT(vm_back_step(space, &backing, SIZE_MAX));
  return backing.status;
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
  struct vm_reclaim reclaim;
  enum mm_result status = vm_free_begin(space, base, bytes, &reclaim);
  if (status == MM_OK) {
    KASSERT(vm_free_step(&reclaim, SIZE_MAX));
  }
  return status;
}

enum mm_result vm_free_begin(struct vm_space *space, uintptr_t base, size_t bytes,
    struct vm_reclaim *reclaim)
{
  require_kernel_owner(space);
  *reclaim = (struct vm_reclaim){0};
  struct vm_range *range = find_range(space, base, bytes);
  if (range && (range->quarantined || range->state == RANGE_RETIRED)) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_BACKED) {
    return MM_INVALID;
  }
  *reclaim = (struct vm_reclaim){.range = range, .space = space};
  return MM_OK;
}

bool vm_free_step(struct vm_reclaim *reclaim, size_t budget)
{
  KASSERT(reclaim && reclaim->range && budget);
  struct vm_space *space = reclaim->space;
  require_kernel_owner(space);
  struct vm_range *range = reclaim->range;
  KASSERT(!range->quarantined &&
      (range->state == RANGE_BACKED || range->state == RANGE_RETIRED));
  /* Private wait withdrawal is done before any unmap; kernel allocations have
   * no user wait tokens. The logical owner remains retained across steps. */
  drop_wait_tokens(space, range);
  size_t pages = range->backed_pages < budget ? range->backed_pages : budget;
  if (pages) {
    free_backing(space, backing_base(range), pages);
    registry_lock(space);
    range->backed_pages -= pages;
    backed_remove(space, pages);
    registry_unlock(space);
  }
  if (range->backed_pages) {
    return false;
  }
  registry_lock(space);
  release_range(space, range);
  registry_unlock(space);
  *reclaim = (struct vm_reclaim){0};
  return true;
}

void vm_kernel_retire(uintptr_t base, size_t bytes)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!vm_kernel_runtime()) {
    KASSERT(vm_free(&kernel_space, base, bytes) == MM_OK);
    return;
  }
  uintptr_t current_stack = (uintptr_t)&base;
  KASSERT(current_stack < base || current_stack - base >= bytes);
  struct execution_group *group = object_cleanup_defer();
  registry_lock(&kernel_space);
  struct vm_range *range = find_range(&kernel_space, base, bytes);
  KASSERT(range && range->state == RANGE_BACKED && !range->quarantined &&
      !range->retirement_group && !range->retirement_next);
  range->state = RANGE_RETIRED;
  range->retirement_group = group;
  if (retired_last) {
    retired_last->retirement_next = range;
  } else {
    retired_first = range;
  }
  retired_last = range;
  registry_unlock(&kernel_space);
  process_cleanup_notify();
}

bool vm_kernel_retire_pending(void)
{
  KASSERT(!cpu_interrupts_enabled());
  registry_lock(&kernel_space);
  bool pending = retired_first != NULL;
  registry_unlock(&kernel_space);
  return pending;
}

void vm_kernel_reap(void)
{
  KASSERT(cpu_interrupts_enabled());
  uint64_t check_flags = cpu_save_interrupts();
  bool pending = vm_kernel_retire_pending();
  cpu_restore_interrupts(check_flags);
  if (!pending) {
    return;
  }
  struct vm_kernel_owner owner = {0};
  vm_kernel_owner_cleanup(&owner);
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    registry_lock(&kernel_space);
    struct vm_range *range = retired_first;
    if (range) {
      retired_first = range->retirement_next;
      if (!retired_first) {
        retired_last = NULL;
      }
      range->retirement_next = NULL;
    }
    registry_unlock(&kernel_space);
    if (!range) {
      vm_kernel_owner_release(&owner);
      cpu_restore_interrupts(flags);
      return;
    }
    KASSERT(range->state == RANGE_RETIRED && !range->quarantined);
    struct execution_group *group = range->retirement_group;
    range->retirement_group = NULL;
    struct vm_reclaim reclaim = {.range = range, .space = &kernel_space};
    for (;;) {
      bool done = vm_free_step(&reclaim, BACKING_RUN_PAGES);
      cpu_restore_interrupts(flags);
      if (done) {
        break;
      }
      kernel_task_yield_if_runnable();
      flags = cpu_save_interrupts();
    }
    if (group) {
      flags = cpu_save_interrupts();
      execution_group_cleanup_end(group);
      cpu_restore_interrupts(flags);
    }
    kernel_task_yield_if_runnable();
  }
}

void vm_kernel_ram_reapers_init(void)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() == 0 && !ram_retirement_queues);
  size_t count = arch_cpu_count();
  KASSERT(count && count <= SIZE_MAX / sizeof(*ram_retirement_queues));
  ram_retirement_queues = kmalloc(count * sizeof(*ram_retirement_queues));
  if (!ram_retirement_queues) {
    panic("cannot allocate CPU RAM retirement queues");
  }
  memset(ram_retirement_queues, 0, count * sizeof(*ram_retirement_queues));
  ram_retirement_cpu_count = count;
}

void vm_kernel_retire_shared(uintptr_t base, size_t bytes, bool confirm_remote)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!vm_kernel_runtime()) {
    KASSERT(!confirm_remote);
    vm_kernel_retire(base, bytes);
    return;
  }
  size_t cpu = arch_cpu_index();
  KASSERT(ram_retirement_queues && cpu < ram_retirement_cpu_count);
  KASSERT(arch_space_active(arch_kernel_space()));
  uintptr_t current_stack = (uintptr_t)&base;
  KASSERT(current_stack < base || current_stack - base >= bytes);
  struct execution_group *group = object_cleanup_defer();
  registry_lock(&kernel_space);
  struct vm_range *range = find_range(&kernel_space, base, bytes);
  KASSERT(range && range->state == RANGE_BACKED && !range->quarantined &&
      !range->retirement_group && !range->retirement_next);
  range->state = RANGE_RETIRED;
  range->retirement_group = group;
  range->retirement_confirm_remote = confirm_remote;
  struct ram_retirement_queue *queue = &ram_retirement_queues[cpu];
  if (queue->last) {
    queue->last->retirement_next = range;
  } else {
    queue->first = range;
  }
  queue->last = range;
  registry_unlock(&kernel_space);
  process_ram_cleanup_notify(cpu);
}

bool vm_kernel_ram_retire_pending(void)
{
  KASSERT(!cpu_interrupts_enabled());
  size_t cpu = arch_cpu_index();
  KASSERT(ram_retirement_queues && cpu < ram_retirement_cpu_count);
  registry_lock(&kernel_space);
  bool pending = ram_retirement_queues[cpu].first != NULL;
  registry_unlock(&kernel_space);
  return pending;
}

void vm_kernel_reap_ram(void)
{
  KASSERT(cpu_interrupts_enabled());
  uint64_t flags = cpu_save_interrupts();
  bool pending = vm_kernel_ram_retire_pending();
  cpu_restore_interrupts(flags);
  if (!pending) {
    return;
  }
  struct vm_kernel_owner owner = {0};
  vm_kernel_owner_cleanup(&owner);
  flags = cpu_save_interrupts();
  registry_lock(&kernel_space);
  struct ram_retirement_queue *queue = &ram_retirement_queues[arch_cpu_index()];
  struct vm_range *range = queue->first;
  KASSERT(range);
  queue->first = range->retirement_next;
  if (!queue->first) {
    queue->last = NULL;
  }
  range->retirement_next = NULL;
  registry_unlock(&kernel_space);
  KASSERT(range->state == RANGE_RETIRED && !range->quarantined);
  cpu_restore_interrupts(flags);

  if (range->retirement_confirm_remote && !vm_kernel_flush_remote()) {
    flags = cpu_save_interrupts();
    registry_lock(&kernel_space);
    /* The intrinsic record and cleanup pin remain owned until reboot. It is
     * deliberately absent from all work queues: a late ACK cannot reclaim it. */
    range->quarantined = true;
    registry_unlock(&kernel_space);
    vm_kernel_owner_release(&owner);
    cpu_restore_interrupts(flags);
    return;
  }

  flags = cpu_save_interrupts();
  struct execution_group *group = range->retirement_group;
  range->retirement_group = NULL;
  range->retirement_confirm_remote = false;
  struct vm_reclaim reclaim = {.range = range, .space = &kernel_space};
  while (!vm_free_step(&reclaim, BACKING_RUN_PAGES)) {
    cpu_restore_interrupts(flags);
    kernel_task_yield_if_runnable();
    flags = cpu_save_interrupts();
  }
  if (group) {
    execution_group_cleanup_end(group);
  }
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
}

void vm_kernel_quarantine(uintptr_t base, size_t bytes)
{
  require_kernel_owner(&kernel_space);
  registry_lock(&kernel_space);
  struct vm_range *range = find_range(&kernel_space, base, bytes);
  KASSERT(range && (range->state == RANGE_BACKED || range->state == RANGE_RESERVED));
  range->quarantined = true;
  registry_unlock(&kernel_space);
}

enum mm_result vm_kernel_window_prepare(size_t bytes, size_t alignment,
    struct vm_kernel_window *window)
{
  KASSERT(!vm_kernel_runtime() && arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (!window || window->base || !bytes || (bytes & (PAGE_SIZE - 1))) {
    return MM_INVALID;
  }
  uintptr_t base;
  enum mm_result status = vm_reserve(&kernel_space, bytes, alignment, &base);
  if (status != MM_OK) {
    return status;
  }
  status = arch_kernel_prepare_range(base, bytes);
  if (status != MM_OK) {
    KASSERT(vm_release(&kernel_space, base, bytes) == MM_OK);
    return status;
  }
  registry_lock(&kernel_space);
  struct vm_range *range = find_range(&kernel_space, base, bytes);
  KASSERT(range && range->state == RANGE_RESERVED);
  range->state = RANGE_WINDOW;
  registry_unlock(&kernel_space);
  *window = (struct vm_kernel_window){.base = base, .bytes = bytes};
  return MM_OK;
}

enum mm_result vm_kernel_window_map(struct vm_kernel_window *window,
    phys_addr_t physical, size_t bytes, bool mmio, uintptr_t *address)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (!address) {
    return MM_INVALID;
  }
  *address = 0;
  if (!window || !window->base || !bytes || (bytes & (PAGE_SIZE - 1)) ||
      (physical & (PAGE_SIZE - 1)) || window->used > window->bytes ||
      bytes > window->bytes - window->used || bytes - 1 > UINT64_MAX - physical) {
    return MM_INVALID;
  }
  registry_lock(&kernel_space);
  struct vm_range *range = find_range(&kernel_space, window->base, window->bytes);
  bool sealed = range && range->state == RANGE_WINDOW;
  registry_unlock(&kernel_space);
  KASSERT(sealed);
  uintptr_t base = window->base + window->used;
  window->used += bytes;
  size_t mapped = 0;
  enum mm_result status = MM_OK;
  for (; mapped < bytes; mapped += PAGE_SIZE) {
    status = arch_kernel_map_prepared(base + mapped, physical + mapped, mmio);
    if (status != MM_OK) {
      break;
    }
  }
  if (status != MM_OK) {
    while (mapped) {
      mapped -= PAGE_SIZE;
      phys_addr_t removed;
      KASSERT(arch_kernel_unmap_prepared(base + mapped, &removed) == MM_OK);
      KASSERT(removed == physical + mapped);
    }
    return status;
  }
  *address = base;
  return MM_OK;
}

enum sparse_phase {
  SPARSE_COMMIT,
  SPARSE_PUBLISH,
  SPARSE_UNWIND,
  SPARSE_PREPARE_DISCARD,
  SPARSE_DISCARD,
};

struct vm_sparse_update {
  struct vm_space *space;
  struct vm_range *range;
  enum vm_sparse_action action;
  enum sparse_phase phase;
  enum mm_result status;
  bool done;
  uintptr_t start, end, cursor;
  struct vm_extent *scan;
  struct vm_extent *created, *created_tail;
  struct vm_extent *building, *split;
  struct vm_extent **link;
  size_t target_pages;
  uint64_t first_token;
  uintptr_t cut_base;
  size_t cut_pages, remaining;
};

static struct vm_extent *new_extent(uintptr_t base, size_t pages)
{
  KASSERT(pages && pages <= SPARSE_EXTENT_PAGES);
  struct vm_extent *extent = kmalloc(sizeof(*extent) + pages * sizeof(uint64_t));
  if (extent) {
    *extent = (struct vm_extent){.base = base, .token_capacity = pages};
  }
  return extent;
}

enum mm_result vm_sparse_reserve(struct vm_space *space, size_t bytes,
    size_t alignment, uintptr_t *address)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!address) {
    return MM_INVALID;
  }
  *address = 0;
  if (!space || space == &kernel_space) {
    return MM_INVALID;
  }
  enum mm_result status = vm_reserve(space, bytes, alignment, address);
  if (status == MM_OK) {
    struct vm_range *range = find_range(space, *address, bytes);
    KASSERT(range && range->state == RANGE_RESERVED);
    range->state = RANGE_SPARSE;
  }
  return status;
}

enum mm_result vm_sparse_begin(struct vm_space *space, uintptr_t base,
    size_t offset, size_t bytes, enum vm_sparse_action action,
    struct vm_sparse_update **result)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!result) {
    return MM_INVALID;
  }
  *result = NULL;
  struct vm_range *range = range_containing(space, base);
  if (!range || space == &kernel_space || range->state != RANGE_SPARSE ||
      range->base != base || !bytes || (offset & (PAGE_SIZE - 1)) ||
      (bytes & (PAGE_SIZE - 1)) || offset / PAGE_SIZE >= range->pages ||
      bytes / PAGE_SIZE > range->pages - offset / PAGE_SIZE ||
      (action != VM_SPARSE_COMMIT && action != VM_SPARSE_DECOMMIT &&
       action != VM_SPARSE_RELEASE) ||
      (action == VM_SPARSE_RELEASE && (offset || bytes / PAGE_SIZE != range->pages))) {
    return MM_INVALID;
  }
  struct vm_sparse_update *update = kmalloc(sizeof(*update));
  if (!update) {
    return MM_NO_MEMORY;
  }
  *update = (struct vm_sparse_update){
    .space = space, .range = range, .action = action,
    .phase = action == VM_SPARSE_COMMIT ? SPARSE_COMMIT : SPARSE_PREPARE_DISCARD,
    .status = MM_OK, .start = base + offset, .end = base + offset + bytes,
    .cursor = base + offset, .scan = range->extents, .link = &range->extents,
  };
  *result = update;
  return MM_OK;
}

static void sparse_complete_discard(struct vm_sparse_update *update)
{
  if (update->action == VM_SPARSE_RELEASE) {
    KASSERT(!update->range->extents && !update->range->backed_pages);
    release_range(update->space, update->range);
  }
  update->done = true;
}

/* This only describes new backing: none of these nodes or tokens are visible
 * to VM queries before the entire commit has become nonfailing. */
static void sparse_commit_step(struct vm_sparse_update *update, size_t *budget)
{
  struct vm_extent *extent = update->building;
  if (extent) {
    size_t count = update->target_pages - extent->pages;
    count = count < *budget ? count : *budget;
    count = count < BACKING_RUN_PAGES ? count : BACKING_RUN_PAGES;
    size_t mapped;
    enum mm_result status = back_run(update->space,
        extent->base + extent->pages * PAGE_SIZE, count,
        PAGE_USER | PAGE_WRITE, &mapped);
    for (size_t i = 0; i < mapped; ++i) {
      extent->tokens[extent->pages + i] = update->first_token + extent->pages + i;
    }
    extent->pages += mapped;
    *budget -= count;
    if (status != MM_OK) {
      update->status = status;
      update->phase = SPARSE_UNWIND;
    } else if (extent->pages == update->target_pages) {
      update->cursor += extent->pages * PAGE_SIZE;
      update->building = NULL;
    }
    return;
  }
  if (update->cursor == update->end) {
    update->phase = SPARSE_PUBLISH;
    update->link = &update->range->extents;
    return;
  }
  extent = update->scan;
  if (extent && extent->base + extent->pages * PAGE_SIZE <= update->cursor) {
    update->scan = extent->next;
    --*budget;
    return;
  }
  if (extent && extent->base <= update->cursor) {
    uintptr_t end = extent->base + extent->pages * PAGE_SIZE;
    update->cursor = end < update->end ? end : update->end;
    update->scan = extent->next;
    --*budget;
    return;
  }
  uintptr_t end = extent && extent->base < update->end ? extent->base : update->end;
  size_t pages = (end - update->cursor) / PAGE_SIZE;
  pages = pages < SPARSE_EXTENT_PAGES ? pages : SPARSE_EXTENT_PAGES;
  struct vm_extent *created = new_extent(update->cursor, pages);
  enum mm_result status = created ?
      reserve_wait_tokens(pages, &update->first_token) : MM_NO_MEMORY;
  --*budget;
  if (status != MM_OK) {
    kfree(created);
    update->status = status;
    update->phase = SPARSE_UNWIND;
    return;
  }
  if (update->created_tail) {
    update->created_tail->next = created;
  } else {
    update->created = created;
  }
  update->created_tail = created;
  update->building = created;
  update->target_pages = pages;
}

static void sparse_prepare_discard(struct vm_sparse_update *update, size_t *budget)
{
  struct vm_extent *extent = *update->link;
  if (extent && extent->base + extent->pages * PAGE_SIZE <= update->start) {
    update->link = &extent->next;
    --*budget;
    return;
  }
  if (!extent || extent->base >= update->end) {
    sparse_complete_discard(update);
    return;
  }
  uintptr_t end = extent->base + extent->pages * PAGE_SIZE;
  /* Only an extent containing both boundaries can require a new node. All
   * other survivors can retain their existing bounded token allocation. */
  if (extent->base < update->start && end > update->end) {
    size_t pages = (end - update->end) / PAGE_SIZE;
    struct vm_extent *split = new_extent(update->end, pages);
    if (!split) {
      update->status = MM_NO_MEMORY;
      update->done = true;
      return;
    }
    size_t first = (update->end - extent->base) / PAGE_SIZE;
    for (size_t i = 0; i < pages; ++i) {
      split->tokens[i] = extent->tokens[first + i];
    }
    split->pages = pages;
    update->split = split;
  }
  --*budget;
  /* All fallible preparation is finished. Withdraw once for the operation,
   * rather than repeatedly scanning global wait buckets for each extent. */
  withdraw_wait_pages(update->space, update->start, update->end - update->start);
  update->phase = SPARSE_DISCARD;
}

static void sparse_discard_step(struct vm_sparse_update *update, size_t *budget)
{
  struct vm_extent *extent = update->building;
  if (!extent) {
    extent = *update->link;
    if (!extent || extent->base >= update->end) {
      sparse_complete_discard(update);
      return;
    }
    uintptr_t end = extent->base + extent->pages * PAGE_SIZE;
    update->cut_base = extent->base > update->start ? extent->base : update->start;
    uintptr_t cut_end = end < update->end ? end : update->end;
    update->cut_pages = (cut_end - update->cut_base) / PAGE_SIZE;
    update->remaining = update->cut_pages;
    update->cursor = update->cut_base;
    update->building = extent;
  }
  if (update->remaining) {
    size_t pages = update->remaining < *budget ? update->remaining : *budget;
    free_backing(update->space, update->cursor, pages);
    update->cursor += pages * PAGE_SIZE;
    update->remaining -= pages;
    update->range->backed_pages -= pages;
    backed_remove(update->space, pages);
    *budget -= pages;
    return;
  }
  size_t prefix = (update->cut_base - extent->base) / PAGE_SIZE;
  size_t suffix = extent->pages - prefix - update->cut_pages;
  if (prefix && suffix) {
    KASSERT(update->split);
    update->split->next = extent->next;
    extent->next = update->split;
    extent->pages = prefix;
    update->split = NULL;
    update->link = &extent->next;
  } else if (prefix) {
    extent->pages = prefix;
    update->link = &extent->next;
  } else if (suffix) {
    for (size_t i = 0; i < suffix; ++i) {
      extent->tokens[i] = extent->tokens[update->cut_pages + i];
    }
    extent->base += update->cut_pages * PAGE_SIZE;
    extent->pages = suffix;
    update->link = &extent->next;
  } else {
    *update->link = extent->next;
    kfree(extent);
  }
  update->building = NULL;
  --*budget;
}

bool vm_sparse_step(struct vm_space *space, struct vm_sparse_update *update,
    size_t budget)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(update && update->space == space && budget);
  while (budget && !update->done) {
    switch (update->phase) {
    case SPARSE_COMMIT:
      sparse_commit_step(update, &budget);
      break;
    case SPARSE_PUBLISH: {
      struct vm_extent *extent = update->created;
      if (!extent) {
        update->done = true;
        break;
      }
      if (*update->link && (*update->link)->base < extent->base) {
        update->link = &(*update->link)->next;
      } else {
        update->created = extent->next;
        extent->next = *update->link;
        *update->link = extent;
        update->link = &extent->next;
        update->range->backed_pages += extent->pages;
        backed_add(space, extent->pages);
      }
      --budget;
      break;
    }
    case SPARSE_UNWIND: {
      struct vm_extent *extent = update->created;
      if (!extent) {
        update->done = true;
        break;
      }
      if (extent->pages) {
        size_t pages = extent->pages < budget ? extent->pages : budget;
        free_backing(space, extent->base, pages);
        extent->base += pages * PAGE_SIZE;
        extent->pages -= pages;
        budget -= pages;
      } else {
        update->created = extent->next;
        kfree(extent);
        --budget;
      }
      break;
    }
    case SPARSE_PREPARE_DISCARD:
      sparse_prepare_discard(update, &budget);
      break;
    case SPARSE_DISCARD:
      sparse_discard_step(update, &budget);
      break;
    }
  }
  return update->done;
}

enum mm_result vm_sparse_finish(struct vm_sparse_update *update)
{
  KASSERT(update && update->done && !update->created && !update->split);
  enum mm_result status = update->status;
  kfree(update);
  return status;
}

static struct vm_stats space_stats(const struct vm_space *space)
{
  struct vm_stats stats = {0};
  if (!space) {
    return stats;
  }
  stats.total_pages = space->total_pages;
  stats.metadata_bytes = sizeof(*space);

  for (struct vm_range *range = space->head; range; range = range->next) {
    ++stats.range_records;
    stats.metadata_bytes += sizeof(*range);
    if (range->wait_tokens) {
      stats.metadata_bytes += range->backed_pages * sizeof(uint64_t);
    }
    for (struct vm_extent *extent = range->extents; extent; extent = extent->next) {
      ++stats.extent_records;
      stats.metadata_bytes += sizeof(*extent) + extent->token_capacity * sizeof(uint64_t);
    }
    if (range->state == RANGE_RESERVED || range->state == RANGE_BACKED ||
        range->state == RANGE_SPARSE || range->state == RANGE_WINDOW ||
        range->state == RANGE_RETIRED) {
      stats.reserved_pages += range->pages;
    }
    if (range->state == RANGE_BACKED || range->state == RANGE_SPARSE ||
        range->state == RANGE_RETIRED) {
      stats.backed_pages += range->backed_pages;
    }
  }
  return stats;
}

size_t vm_space_backed_pages(const struct vm_space *space)
{
  return atomic_load_explicit(&space->backed_pages, memory_order_relaxed);
}

struct vm_stats vm_get_stats(const struct vm_space *space)
{
  registry_lock(space);
  struct vm_stats stats = space_stats(space);
  registry_unlock(space);
  return stats;
}

struct vm_space *vm_kernel_space(void)
{
  return &kernel_space;
}

bool vm_kernel_flush_remote(void)
{
  require_kernel_owner(&kernel_space);
  uint64_t now = arch_monotonic_ns();
  uint64_t deadline = KERNEL_FLUSH_TIMEOUT_NS > UINT64_MAX - now ? UINT64_MAX :
    now + KERNEL_FLUSH_TIMEOUT_NS;
  return arch_kernel_flush_remote(deadline);
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
  atomic_init(&space->waiters, 0);
  atomic_init(&space->backed_pages, 0);

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

enum mm_result vm_space_retire(struct vm_space *space)
{
  if (!space || space == &kernel_space || space->retired ||
      arch_space_active(page_space(space))) {
    return MM_INVALID;
  }

  /* Check borrowed mappings before freeing anything. A rejected retirement
   * must leave the entire space usable, including its VM-owned allocations. */
  for (struct vm_range *range = space->head; range; range = range->next) {
    if (range->state == RANGE_RESERVED) {
      enum mm_result status = require_unmapped(space, range);
      if (status != MM_OK) {
        return status;
      }
    }
  }
  space->retired = true;
  return MM_OK;
}

bool vm_space_release_step(struct vm_space *space, size_t budget)
{
  KASSERT(space && space->retired && budget);
  while (budget) {
    struct vm_range *range = space->head;
    if (!range) {
      if (!arch_space_release_step(&space->arch, &budget)) {
        return false;
      }
      KASSERT(!atomic_load_explicit(&space->waiters, memory_order_relaxed));
      kfree(space);
      return true;
    }

    if (range->state == RANGE_SPARSE && range->extents) {
      if (!range->sparse_withdrawn) {
        withdraw_wait_pages(space, range->base, range->pages * PAGE_SIZE);
        range->sparse_withdrawn = true;
      }
      struct vm_extent *extent = range->extents;
      if (extent->pages) {
        size_t pages = extent->pages < budget ? extent->pages : budget;
        free_backing(space, extent->base, pages);
        extent->base += pages * PAGE_SIZE;
        extent->pages -= pages;
        range->backed_pages -= pages;
        backed_remove(space, pages);
        budget -= pages;
      } else {
        range->extents = extent->next;
        kfree(extent);
        --budget;
      }
      continue;
    }
    if (range->state == RANGE_BACKED && range->backed_pages) {
      drop_wait_tokens(space, range);
      /* Freeing from the front keeps the remaining backing a suffix. */
      size_t pages = range->backed_pages < budget ? range->backed_pages : budget;
      free_backing(space, backing_base(range), pages);
      range->backed_pages -= pages;
      backed_remove(space, pages);
      budget -= pages;
      continue;
    }
    space->head = range->next;
    drop_record(space, range);
    --budget;
  }
  return false;
}

enum mm_result vm_space_destroy(struct vm_space *space)
{
  enum mm_result status = vm_space_retire(space);
  if (status != MM_OK) {
    return status;
  }
  /* An unlimited budget finishes in one call. */
  KASSERT(vm_space_release_step(space, SIZE_MAX));
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
  if (range && range->quarantined) {
    return MM_BUSY;
  }
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
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }
  return arch_page_map_mmio(base, physical);
}

enum mm_result vm_map_scanout(uintptr_t base, phys_addr_t physical)
{
  require_kernel_owner(&kernel_space);
  struct vm_range *range = range_containing(&kernel_space, base);
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }
  return arch_page_map_scanout(base, physical);
}

enum mm_result vm_map_scanout_run(uintptr_t base, const phys_addr_t *physical,
    size_t count, size_t *mapped)
{
  require_kernel_owner(&kernel_space);
  *mapped = 0;
  struct vm_range *range = range_containing(&kernel_space, base);
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_RESERVED || !physical || !count ||
      count > PMM_FRAME_BATCH_MAX || count > range->pages - (base - range->base) / PAGE_SIZE) {
    return MM_INVALID;
  }
  return arch_page_map_scanout_run(base, physical, count, mapped);
}

bool vm_kernel_physical_overlap(phys_addr_t physical, size_t bytes)
{
  const struct vm_physical_range range = {physical, bytes};
  return vm_kernel_physical_overlap_any(&range, 1);
}

bool vm_kernel_physical_overlap_any(const struct vm_physical_range *ranges, size_t count)
{
  require_kernel_owner(&kernel_space);
  if (!count) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!ranges[i].bytes || ranges[i].bytes > UINT64_MAX - ranges[i].physical) {
      return true;
    }
  }
  for (const struct vm_range *r = kernel_space.head; r; r = r->next) {
    /* Retirement may change BACKED to RETIRED without the logical owner.
     * The owner still stabilizes this record's links and extent. */
    registry_lock(&kernel_space);
    bool mapped = r->state == RANGE_RESERVED || r->state == RANGE_BACKED ||
        r->state == RANGE_WINDOW || r->state == RANGE_RETIRED;
    registry_unlock(&kernel_space);
    if (!mapped) {
      continue;
    }
    if (arch_kernel_physical_overlap(r->base, r->pages, ranges, count)) {
      return true;
    }
  }
  return false;
}

enum mm_result vm_unmap(struct vm_space *space, uintptr_t base,
                        phys_addr_t *physical)
{
  require_kernel_owner(space);
  struct vm_range *range = range_containing(space, base);
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || range->state != RANGE_RESERVED) {
    return MM_INVALID;
  }
  return arch_page_unmap(page_space(space), base, physical);
}

static uint64_t *owned_wait_token(struct vm_range *range, uintptr_t base)
{
  if (range->state == RANGE_SPARSE) {
    for (struct vm_extent *extent = range->extents; extent; extent = extent->next) {
      if (base < extent->base) {
        break;
      }
      size_t page = (base - extent->base) / PAGE_SIZE;
      if (page < extent->pages) {
        return &extent->tokens[page];
      }
    }
    return NULL;
  }
  if (range->state == RANGE_BACKED && range->wait_tokens) {
    size_t page = (base - range->base) / PAGE_SIZE;
    size_t prefix = range->pages - range->backed_pages;
    if (page >= prefix) {
      return &range->wait_tokens[page - prefix];
    }
  }
  return NULL;
}

enum mm_result vm_protect(struct vm_space *space, uintptr_t base,
                          unsigned permissions)
{
  require_kernel_owner(space);
  struct vm_range *range = range_containing(space, base);
  if (range && range->quarantined) {
    return MM_BUSY;
  }
  if (!range || (range->state != RANGE_RESERVED && range->state != RANGE_BACKED &&
      range->state != RANGE_SPARSE) ||
      !permissions_valid(space, permissions)) {
    return MM_INVALID;
  }
  uint64_t *token = owned_wait_token(range, base);
  if (!token) {
    return range->state == RANGE_SPARSE ? MM_NOT_MAPPED :
        arch_page_protect(page_space(space), base, permissions);
  }
  uint64_t fresh = 0;
  if ((permissions & PAGE_WRITE) && !*token) {
    enum mm_result status = reserve_wait_tokens(1, &fresh);
    if (status != MM_OK) {
      return status;
    }
  }
  if (*token && !(permissions & PAGE_WRITE)) {
    withdraw_wait_pages(space, base, PAGE_SIZE);
    *token = 0;
  }
  /* Owned backing is mapped, with permissive ancestors and no device
   * cache flags. Exclusive VM ownership makes this commit nonfailing. */
  KASSERT(arch_page_protect(page_space(space), base, permissions) == MM_OK);
  if (fresh) {
    *token = fresh;
  }
  return MM_OK;
}

enum mm_result vm_protect_begin(struct vm_space *space, uintptr_t base,
    size_t bytes, unsigned permissions, struct vm_protection *protection)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!protection) {
    return MM_INVALID;
  }
  *protection = (struct vm_protection){
    .phase = VM_PROTECTION_DONE, .status = MM_INVALID,
  };
  if (!space || space == &kernel_space || space->retired || !bytes ||
      (base & (PAGE_SIZE - 1)) || (bytes & (PAGE_SIZE - 1)) ||
      !permissions_valid(space, permissions)) {
    return MM_INVALID;
  }
  struct vm_range *range = range_containing(space, base);
  if (!range || range->state != RANGE_BACKED || !range->wait_tokens ||
      base < backing_base(range)) {
    return MM_INVALID;
  }
  size_t offset = (base - backing_base(range)) / PAGE_SIZE;
  size_t pages = bytes / PAGE_SIZE;
  if (pages > range->backed_pages - offset) {
    return MM_INVALID;
  }
  *protection = (struct vm_protection){
    .space = space, .base = base, .tokens = range->wait_tokens + offset,
    .pages = pages, .permissions = permissions,
    .phase = VM_PROTECTION_PREPARE, .status = MM_OK,
  };
  return MM_OK;
}

bool vm_protect_step(struct vm_protection *protection, size_t budget)
{
  KASSERT(!cpu_interrupts_enabled() && protection && budget);
  while (protection->phase == VM_PROTECTION_PREPARE && budget) {
    uintptr_t address = protection->base + protection->cursor * PAGE_SIZE;
    struct page_translation mapping;
    enum mm_result status = arch_page_query(page_space(protection->space),
        address, &mapping);
    if (status == MM_OK && (!permissions_valid(protection->space,
          mapping.permissions) || !!(mapping.permissions & PAGE_WRITE) !=
          !!protection->tokens[protection->cursor])) {
      status = MM_INVALID;
    }
    if (status != MM_OK) {
      protection->status = status;
      protection->phase = VM_PROTECTION_DONE;
      return true;
    }
    protection->old_writable_pages += !!(mapping.permissions & PAGE_WRITE);
    protection->old_executable_pages += !!(mapping.permissions & PAGE_EXEC);
    if ((protection->permissions & PAGE_WRITE) &&
        !protection->tokens[protection->cursor]) {
      ++protection->fresh_tokens;
    }
    ++protection->cursor;
    --budget;
    if (protection->cursor == protection->pages) {
      if (protection->fresh_tokens) {
        protection->status = reserve_wait_tokens(protection->fresh_tokens,
            &protection->next_token);
        if (protection->status != MM_OK) {
          protection->phase = VM_PROTECTION_DONE;
          return true;
        }
      }
      /* This is the first observable effect. All remaining work is nonfallible
       * under the same drained owner, including after a native stop request. */
      if (!(protection->permissions & PAGE_WRITE) && protection->old_writable_pages) {
        withdraw_wait_pages(protection->space, protection->base,
            protection->pages * PAGE_SIZE);
      }
      protection->cursor = 0;
      protection->phase = VM_PROTECTION_COMMIT;
    }
  }
  while (protection->phase == VM_PROTECTION_COMMIT && budget) {
    size_t page = protection->cursor;
    uintptr_t address = protection->base + page * PAGE_SIZE;
    /* Eager owned RAM has permissive ancestors and no device cache flags;
     * the full validation above makes each leaf update nonfailing. */
    KASSERT(arch_page_protect(page_space(protection->space), address,
        protection->permissions) == MM_OK);
    if (!(protection->permissions & PAGE_WRITE)) {
      protection->tokens[page] = 0;
    } else if (!protection->tokens[page]) {
      protection->tokens[page] = protection->next_token++;
    }
    ++protection->cursor;
    --budget;
    if (protection->cursor == protection->pages) {
      protection->phase = VM_PROTECTION_DONE;
    }
  }
  return protection->phase == VM_PROTECTION_DONE;
}

enum mm_result vm_query(struct vm_space *space, uintptr_t address,
                        struct page_translation *result)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!space || (space == &kernel_space ? !space->total_pages : !space->head)) {
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

bool vm_address_wait_token(struct vm_space *space, uintptr_t address,
    size_t word_size, uint64_t *token)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!token) {
    return false;
  }
  *token = 0;
  if ((word_size != 4 && word_size != 8) || (address & (word_size - 1)) ||
      !vm_user_buffer_accessible(space, address, word_size, true)) {
    return false;
  }
  uintptr_t page = address & ~(uintptr_t)(PAGE_SIZE - 1);
  struct vm_range *range = range_containing(space, page);
  if (!range) {
    return false;
  }
  uint64_t *incarnation = owned_wait_token(range, page);
  if (!incarnation || !*incarnation) {
    return false;
  }
  *token = *incarnation;
  return true;
}

void vm_wait_enroll(struct vm_space *space)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(space && space != &kernel_space && space->head);
  size_t count = atomic_load_explicit(&space->waiters, memory_order_relaxed);
  do {
    KASSERT(count != SIZE_MAX);
  } while (!atomic_compare_exchange_weak_explicit(&space->waiters, &count, count + 1,
      memory_order_relaxed, memory_order_relaxed));
}

void vm_wait_detach(struct vm_space *space)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(space && space != &kernel_space && space->head);
  size_t count = atomic_load_explicit(&space->waiters, memory_order_relaxed);
  do {
    KASSERT(count);
  } while (!atomic_compare_exchange_weak_explicit(&space->waiters, &count, count - 1,
      memory_order_relaxed, memory_order_relaxed));
}

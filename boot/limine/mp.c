#include "mp.h"
#include <limine.h>
#include <arch/smp.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <stddef.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
  .id = LIMINE_MP_REQUEST_ID,
};

static const struct boot_info *startup_boot;
static uint64_t cpu_array_physical;
static uint64_t pointer_offset;
static size_t cpu_count;
static uint32_t bsp_lapic_id;

struct mp_mapping {
  uintptr_t base;
  size_t bytes;
  void *data;
};

[[noreturn]] void limine_ap_entry(struct limine_mp_info *info);

static uint64_t pointer_physical(const void *pointer)
{
  uintptr_t address = (uintptr_t)pointer;
  if (address < pointer_offset || (address % sizeof(uint64_t))) {
    panic("invalid Limine MP pointer");
  }
  return address - pointer_offset;
}

void limine_capture_cpus(const struct boot_info *boot)
{
  _Static_assert(offsetof(struct limine_mp_info, extra_argument) == 24,
                 "Limine AP entry argument offset");
  const struct limine_mp_response *response = mp_request.response;
  if (!response || !response->cpu_count || !response->cpus ||
      response->cpu_count > SIZE_MAX / sizeof(struct limine_mp_info *)) {
    panic("missing or invalid Limine MP response");
  }
  if (response->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC) {
    panic("x2APIC boot mode is not supported");
  }

  startup_boot = boot;
  pointer_offset = boot->bootstrap_direct_offset;
  cpu_array_physical = pointer_physical(response->cpus);
  cpu_count = response->cpu_count;
  bsp_lapic_id = response->bsp_lapic_id;
}

static void require_bootloader_frames(uint64_t start, size_t bytes)
{
  if (start > UINT64_MAX - bytes) {
    panic("Limine MP mapping overflows");
  }
  uint64_t end = start + bytes;
  uint64_t covered = start;
  for (size_t i = 0; i < startup_boot->region_count && covered < end; ++i) {
    const struct boot_region *region = &startup_boot->regions[i];
    uint64_t region_end = region->base + region->length;
    if (region_end <= covered) {
      continue;
    }
    if (region->base > covered || region->type != BOOT_LOADER) {
      break;
    }
    covered = region_end;
  }
  if (covered < end) {
    panic("Limine MP data is outside bootloader reservations");
  }
}

static struct mp_mapping map_mp_data(uint64_t physical, size_t bytes)
{
  size_t offset = physical & (PAGE_SIZE - 1);
  KASSERT(bytes && bytes <= SIZE_MAX - offset - (PAGE_SIZE - 1));
  struct mp_mapping mapping = {
    .bytes = (offset + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
  };
  uint64_t first_frame = physical - offset;
  require_bootloader_frames(first_frame, mapping.bytes);
  KASSERT(vm_reserve(vm_kernel_space(), mapping.bytes, PAGE_SIZE, &mapping.base) == MM_OK);
  for (size_t i = 0; i < mapping.bytes; i += PAGE_SIZE) {
    KASSERT(vm_map(vm_kernel_space(), mapping.base + i, first_frame + i, PAGE_WRITE) == MM_OK);
  }
  mapping.data = (void *)(mapping.base + offset);
  return mapping;
}

static void unmap_mp_data(struct mp_mapping mapping)
{
  for (size_t i = 0; i < mapping.bytes; i += PAGE_SIZE) {
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), mapping.base + i, &physical) == MM_OK);
  }
  KASSERT(vm_release(vm_kernel_space(), mapping.base, mapping.bytes) == MM_OK);
}

void boot_start_cpus(void)
{
  KASSERT(startup_boot && pointer_offset);
  arch_smp_prepare(cpu_count, bsp_lapic_id);
  struct mp_mapping array = map_mp_data(cpu_array_physical,
                                        cpu_count * sizeof(struct limine_mp_info *));
  struct limine_mp_info **records = array.data;
  bool found_bsp = false;

  for (size_t i = 0; i < cpu_count; ++i) {
    struct mp_mapping record = map_mp_data(pointer_physical(records[i]),
                                           sizeof(struct limine_mp_info));
    struct limine_mp_info *info = record.data;
    if (info->lapic_id == bsp_lapic_id) {
      KASSERT(!found_bsp);
      found_bsp = true;
    } else {
      KASSERT(!info->goto_address);
      struct ap_boot *handoff = arch_ap_prepare(info->lapic_id);
      info->extra_argument = (uintptr_t)handoff;
      /* Limine's parked AP acquires this publication before reading the
       * argument. Reserved fields belong to Limine and must stay untouched. */
      __atomic_store_n(&info->goto_address, limine_ap_entry, __ATOMIC_RELEASE);
      arch_ap_wait();
    }
    unmap_mp_data(record);
  }
  KASSERT(found_bsp);
  unmap_mp_data(array);
  arch_smp_finish();

  /* Only borrowed aliases were removed; the original frames stay reserved.
   * No AP or BSP continues to use Limine pointers or the old HHDM. */
  startup_boot = NULL;
  pointer_offset = 0;
  cpu_array_physical = 0;
}

#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

enum mm_result dma_buffer_allocate(struct dma_buffer *buffer, size_t bytes)
{
  KASSERT(!buffer->address && !buffer->physical && !buffer->bytes);
  if (!bytes || bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return MM_INVALID;
  }
  bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  uintptr_t address;
  enum mm_result result = vm_reserve(vm_kernel_space(), bytes, PAGE_SIZE, &address);
  if (result != MM_OK) {
    return result;
  }
  phys_addr_t physical = pmm_alloc(bytes / PAGE_SIZE);
  if (!physical) {
    KASSERT(vm_release(vm_kernel_space(), address, bytes) == MM_OK);
    return MM_NO_MEMORY;
  }

  size_t mapped = 0;
  while (mapped < bytes) {
    result = vm_map(vm_kernel_space(), address + mapped, physical + mapped, PAGE_WRITE);
    if (result != MM_OK) {
      break;
    }
    mapped += PAGE_SIZE;
  }
  if (result != MM_OK) {
    while (mapped) {
      mapped -= PAGE_SIZE;
      phys_addr_t frame;
      KASSERT(vm_unmap(vm_kernel_space(), address + mapped, &frame) == MM_OK);
    }
    pmm_free(physical, bytes / PAGE_SIZE);
    KASSERT(vm_release(vm_kernel_space(), address, bytes) == MM_OK);
    return result;
  }
  memset((void *)address, 0, bytes);
  *buffer = (struct dma_buffer){.address = address, .physical = physical, .bytes = bytes};
  return MM_OK;
}

void dma_buffer_release(struct dma_buffer *buffer)
{
  if (!buffer->address) {
    return;
  }
  for (size_t offset = 0; offset < buffer->bytes; offset += PAGE_SIZE) {
    phys_addr_t frame;
    KASSERT(vm_unmap(vm_kernel_space(), buffer->address + offset, &frame) == MM_OK);
    KASSERT(frame == buffer->physical + offset);
  }
  pmm_free(buffer->physical, buffer->bytes / PAGE_SIZE);
  KASSERT(vm_release(vm_kernel_space(), buffer->address, buffer->bytes) == MM_OK);
  *buffer = (struct dma_buffer){0};
}

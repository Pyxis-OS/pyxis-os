#include <arch/intel/display_inventory.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/pci/registers.h>

#define INTEL_VENDOR 0x8086
#define PANTHER_LAKE_DEVICE 0xb0a0
#define DISPLAY_CLASS 3
#define GMD_ID_DISPLAY 0x510a0u
#define GMD_ARCH_SHIFT 22
#define GMD_ARCH_MASK 0x3ffu
#define GMD_RELEASE_SHIFT 14
#define GMD_RELEASE_MASK 0xffu
#define GMD_STEP_MASK 0x3fu

/* Linux v6.19 probe_gmdid_display() reads this one register directly, before
 * choosing the display layout. Powered display-register reads are different:
 * intel_de_read() can acquire a write-based DMC wakelock on this generation. */
static bool overlaps(uint64_t first, uint64_t bytes, uint64_t other, uint64_t size)
{
  return size && (bytes > UINT64_MAX - first || size > UINT64_MAX - other ||
      (first < other + size && other < first + bytes));
}

static bool page_available(const struct boot_info *boot, phys_addr_t physical)
{
  if (!arch_pci_mmio_available(physical, PAGE_SIZE) ||
      vm_kernel_physical_overlap(physical, PAGE_SIZE) ||
      overlaps(physical, PAGE_SIZE, boot->framebuffer.physical, boot->framebuffer.size)) {
    return false;
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if (r->type != BOOT_RESERVED && overlaps(physical, PAGE_SIZE, r->base, r->length)) {
      return false;
    }
  }
  return true;
}

static bool read_bar(struct pci_address address, unsigned slot, uint64_t *base)
{
  uint32_t low = pci_read32(address, PCI_BAR_FIRST + slot * PCI_REGISTER_BYTES);
  unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
  if ((low & PCI_BAR_IO) || (type != PCI_BAR_MEMORY_32 && type != PCI_BAR_MEMORY_64)) {
    return false;
  }
  *base = low & PCI_BAR_MEMORY_ADDRESS_MASK;
  if (type == PCI_BAR_MEMORY_64) {
    if (slot + 1 >= PCI_ENDPOINT_BARS) {
      return false;
    }
    *base |= (uint64_t)pci_read32(address,
        PCI_BAR_FIRST + (slot + 1) * PCI_REGISTER_BYTES) << PCI_BAR_HIGH_SHIFT;
  }
  return *base && !(*base & (PAGE_SIZE - 1));
}

static bool device_ready(const struct pci_device *device, uint16_t *power)
{
  struct pci_address address = device->address;
  if (device->owner || device->base_class != DISPLAY_CLASS ||
      device->header_type != PCI_HEADER_ENDPOINT ||
      pci_read16(address, PCI_VENDOR_ID) != INTEL_VENDOR ||
      pci_read16(address, PCI_DEVICE_ID) != PANTHER_LAKE_DEVICE ||
      !(pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MEMORY)) {
    return false;
  }
  *power = PCI_POWER_D0;
  if (!(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return true;
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  bool found = false;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES]) {
      return false;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    if (pci_read8(address, offset) == PCI_CAP_POWER) {
      if (found || offset > PCI_CONVENTIONAL_BYTES - PCI_POWER_BYTES) {
        return false;
      }
      found = true;
      *power = pci_read16(address, offset + PCI_POWER_CONTROL);
      if ((*power & PCI_POWER_STATE_MASK) != PCI_POWER_D0) {
        return false;
      }
    }
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  return true;
}

void intel_display_inventory(const struct boot_info *boot)
{
  struct pci_device *device;
  if (pci_select_device(INTEL_VENDOR, PANTHER_LAKE_DEVICE, &device) != PCI_SELECTION_UNIQUE) {
    kinfo("intel-inventory: unavailable: unique 8086:b0a0 endpoint absent\n");
    return;
  }
  kinfo("intel-inventory: begin read-only; no PCI/GPU/GGTT/power writes\n");
  uint16_t power;
  uint64_t registers, aperture;
  if (!device_ready(device, &power) || !read_bar(device->address, 0, &registers) ||
      !read_bar(device->address, 2, &aperture) ||
      registers > UINT64_MAX - GMD_ID_DISPLAY - sizeof(uint32_t)) {
    kinfo("intel-inventory: unavailable: PCI identity/D0/memory decode/BAR prerequisite\n");
    return;
  }
  kinfo("intel-inventory: PCI=%x:%x.%u revision=%x command=%x PMCSR=%x BAR0=%lx BAR2=%lx\n",
      device->address.bus, device->address.device, device->address.function,
      device->revision, pci_read16(device->address, PCI_COMMAND), power, registers, aperture);
  kinfo("intel-inventory: GOP=%lx bytes=%zu %zux%zu pitch=%zu RGB=%u/%u/%u\n",
      boot->framebuffer.physical, boot->framebuffer.size, boot->framebuffer.width,
      boot->framebuffer.height, boot->framebuffer.pitch,
      boot->framebuffer.red_shift, boot->framebuffer.green_shift, boot->framebuffer.blue_shift);
  if (boot->framebuffer.physical >= aperture) {
    kinfo("intel-inventory: GOP-minus-BAR2=%lx; extent/translation/ownership unproven\n",
        boot->framebuffer.physical - aperture);
  }

  phys_addr_t page = (registers + GMD_ID_DISPLAY) & ~(phys_addr_t)(PAGE_SIZE - 1);
  uintptr_t window;
  if (!page_available(boot, page) ||
      vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, &window) != MM_OK) {
    kinfo("intel-inventory: unavailable: GMD page overlaps RAM/mapping or reservation failed\n");
    return;
  }
  enum mm_result result = vm_map_mmio(window, page);
  bool mapped = result == MM_OK;
  if (mapped) {
    result = vm_protect(vm_kernel_space(), window, 0);
  }
  if (result != MM_OK) {
    kinfo("intel-inventory: unavailable: read-only GMD mapping status=%u\n", (unsigned)result);
  } else {
    const volatile uint32_t *gmd = (const volatile uint32_t *)(window +
        (GMD_ID_DISPLAY & (PAGE_SIZE - 1)));
    uint32_t first = *gmd;
    uint32_t second = *gmd;
    uint64_t final_registers, final_aperture;
    uint16_t final_power;
    bool stable = first == second && device_ready(device, &final_power) &&
        power == final_power && read_bar(device->address, 0, &final_registers) &&
        registers == final_registers && read_bar(device->address, 2, &final_aperture) &&
        aperture == final_aperture;
    kinfo("intel-inventory: GMD_ID_DISPLAY=%x/%x arch=%u release=%u step=%u stable=%u\n",
        first, second, (second >> GMD_ARCH_SHIFT) & GMD_ARCH_MASK,
        (second >> GMD_RELEASE_SHIFT) & GMD_RELEASE_MASK, second & GMD_STEP_MASK, stable);
    kinfo("intel-inventory: powered-state unavailable: inherited DC/DMC accessibility unproven; no wake requested\n");
    kinfo("intel-inventory: pipe/transcoder/plane/scaler/cursor/timing/GGTT not read; scanout ownership unproven\n");
  }
  if (mapped) {
    phys_addr_t physical;
    result = vm_unmap(vm_kernel_space(), window, &physical);
    if (result != MM_OK) {
      kinfo("intel-inventory: GMD mapping retained, unmap status=%u\n", (unsigned)result);
      return;
    }
  }
  result = vm_release(vm_kernel_space(), window, PAGE_SIZE);
  if (result != MM_OK) {
    kinfo("intel-inventory: GMD reservation retained, release status=%u\n", (unsigned)result);
  }
  kinfo("intel-inventory: end partial identity inventory; firmware display preserved\n");
}

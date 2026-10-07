#include <arch/bochs_display.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>

#define BOCHS_VENDOR_ID 0x1234
#define BOCHS_DEVICE_ID 0x1111
#define BOCHS_DISPLAY_CLASS 0x03
#define BOCHS_VGA_SUBCLASS 0x00
#define BOCHS_OTHER_SUBCLASS 0x80
#define BOCHS_QEXT_REVISION 2
#define BOCHS_VRAM_BAR 0
#define BOCHS_REGISTER_BAR 2
#define BOCHS_REGISTER_BYTES 0x1000
#define BOCHS_DISPI_OFFSET 0x500
#define BOCHS_DISPI_REGISTER_BYTES 2
#define BOCHS_ID 0
#define BOCHS_WIDTH 1
#define BOCHS_HEIGHT 2
#define BOCHS_BPP 3
#define BOCHS_ENABLE 4
#define BOCHS_BANK 5
#define BOCHS_VIRTUAL_WIDTH 6
#define BOCHS_VIRTUAL_HEIGHT 7
#define BOCHS_X_OFFSET 8
#define BOCHS_Y_OFFSET 9
#define BOCHS_SAVED_REGISTERS 10
#define BOCHS_VIDEO_MEMORY 10
#define BOCHS_ID_FIRST 0xb0c0
#define BOCHS_ID_LAST 0xb0c5
#define BOCHS_ENABLED 0x01
#define BOCHS_GETCAPS 0x02
#define BOCHS_LINEAR_FRAMEBUFFER 0x40
#define BOCHS_NOCLEARMEM 0x80
#define BOCHS_MAX_WIDTH 16000u
#define BOCHS_MAX_HEIGHT 12000u
#define BOCHS_MIN_DIMENSION 64u
#define BOCHS_WIDTH_ALIGNMENT 8u
#define BOCHS_PIXEL_BITS 32u
#define BOCHS_PIXEL_BYTES 4u
#define BOCHS_VRAM_UNIT_BYTES (64u * 1024u)
#define BOCHS_QEXT_OFFSET 0x600
#define BOCHS_QEXT_SIZE 0
#define BOCHS_QEXT_BYTEORDER 4
#define BOCHS_QEXT_BYTES 8
#define BOCHS_LITTLE_ENDIAN UINT32_C(0x1e1e1e1e)
#define BOCHS_BIG_ENDIAN UINT32_C(0xbebebebe)

/* QEMU's bochs-vbe.h, vga.c and bochs-display.c define this MMIO layout.
 * Standard VGA fixes up enabled registers and derives virtual height; the
 * display-only device stores virtual height without deriving it. Neither
 * device has a DMA engine or interrupt source. */
static struct {
  struct pci_claim claim;
  struct pci_mapping registers;
  uint16_t saved[BOCHS_SAVED_REGISTERS];
  uint32_t saved_byteorder;
  bool vga;
} bochs;

bool arch_bochs_display_matches(const struct pci_device *device)
{
  return device && device->vendor_id == BOCHS_VENDOR_ID &&
    device->device_id == BOCHS_DEVICE_ID &&
    device->base_class == BOCHS_DISPLAY_CLASS &&
    (device->subclass == BOCHS_VGA_SUBCLASS || device->subclass == BOCHS_OTHER_SUBCLASS) &&
    !device->interface && device->header_type == PCI_HEADER_ENDPOINT;
}

static uint16_t read_dispi(unsigned index)
{
  return *(volatile uint16_t *)(bochs.registers.address + BOCHS_DISPI_OFFSET +
      BOCHS_DISPI_REGISTER_BYTES * index);
}

static void write_dispi(unsigned index, uint16_t value)
{
  *(volatile uint16_t *)(bochs.registers.address + BOCHS_DISPI_OFFSET +
      BOCHS_DISPI_REGISTER_BYTES * index) = value;
}

static uint32_t read_extension(unsigned offset)
{
  return *(volatile uint32_t *)(bochs.registers.address + BOCHS_QEXT_OFFSET + offset);
}

static void write_byteorder(uint32_t value)
{
  *(volatile uint32_t *)(bochs.registers.address + BOCHS_QEXT_OFFSET +
      BOCHS_QEXT_BYTEORDER) = value;
}

static enum bochs_display_result fail(const char **reason, const char *text,
    bool restoration_failed)
{
  *reason = text;
  return restoration_failed ? BOCHS_DISPLAY_RESTORE_FAILED : BOCHS_DISPLAY_REFUSED;
}

static bool restore_firmware(void)
{
  write_dispi(BOCHS_ENABLE, 0);
  if (read_dispi(BOCHS_ENABLE) != 0) {
    return false;
  }
  write_dispi(BOCHS_WIDTH, bochs.saved[BOCHS_WIDTH]);
  write_dispi(BOCHS_HEIGHT, bochs.saved[BOCHS_HEIGHT]);
  write_dispi(BOCHS_BPP, bochs.saved[BOCHS_BPP]);
  write_dispi(BOCHS_ENABLE, bochs.saved[BOCHS_ENABLE] | BOCHS_NOCLEARMEM);
  if (read_dispi(BOCHS_ENABLE) != (bochs.saved[BOCHS_ENABLE] | BOCHS_NOCLEARMEM)) {
    return false;
  }
  /* VGA's enable transition discards virtual width and both offsets. Restore
   * them afterward, while its fixup derives the original virtual height. */
  write_dispi(BOCHS_VIRTUAL_WIDTH, bochs.saved[BOCHS_VIRTUAL_WIDTH]);
  write_dispi(BOCHS_VIRTUAL_HEIGHT, bochs.saved[BOCHS_VIRTUAL_HEIGHT]);
  write_dispi(BOCHS_X_OFFSET, bochs.saved[BOCHS_X_OFFSET]);
  write_dispi(BOCHS_Y_OFFSET, bochs.saved[BOCHS_Y_OFFSET]);
  write_byteorder(bochs.saved_byteorder);
  /* Already enabled: restoring the original flags cannot clear VRAM. ENABLE
   * writes reset VGA's bank offset, so BANK must be restored last. */
  write_dispi(BOCHS_ENABLE, bochs.saved[BOCHS_ENABLE]);
  write_dispi(BOCHS_BANK, bochs.saved[BOCHS_BANK]);
  for (unsigned index = 0; index < BOCHS_SAVED_REGISTERS; ++index) {
    if (read_dispi(index) != bochs.saved[index]) {
      return false;
    }
  }
  return read_extension(BOCHS_QEXT_BYTEORDER) == bochs.saved_byteorder;
}

static bool select_mode(uint16_t width, uint16_t height, uint32_t vram_bytes)
{
  write_dispi(BOCHS_ENABLE, 0);
  if (read_dispi(BOCHS_ENABLE) != 0) {
    return false;
  }
  write_dispi(BOCHS_WIDTH, width);
  write_dispi(BOCHS_HEIGHT, height);
  write_dispi(BOCHS_BPP, BOCHS_PIXEL_BITS);
  write_byteorder(BOCHS_LITTLE_ENDIAN);
  uint16_t enable = BOCHS_ENABLED | BOCHS_LINEAR_FRAMEBUFFER | BOCHS_NOCLEARMEM;
  write_dispi(BOCHS_ENABLE, enable);
  write_dispi(BOCHS_VIRTUAL_WIDTH, width);
  write_dispi(BOCHS_VIRTUAL_HEIGHT, height);
  write_dispi(BOCHS_X_OFFSET, 0);
  write_dispi(BOCHS_Y_OFFSET, 0);
  write_dispi(BOCHS_BANK, 0);
  /* VGA stores the derived value in a 16-bit register, even when capacity
   * exceeds that range. Its scanout bounds use the full VRAM extent. */
  uint16_t virtual_height = bochs.vga ?
    (uint16_t)(vram_bytes / ((uint32_t)width * BOCHS_PIXEL_BYTES)) : height;
  return read_dispi(BOCHS_ID) == bochs.saved[BOCHS_ID] &&
    read_dispi(BOCHS_WIDTH) == width && read_dispi(BOCHS_HEIGHT) == height &&
    read_dispi(BOCHS_BPP) == BOCHS_PIXEL_BITS && read_dispi(BOCHS_ENABLE) == enable &&
    read_dispi(BOCHS_VIRTUAL_WIDTH) == width &&
    read_dispi(BOCHS_VIRTUAL_HEIGHT) == virtual_height &&
    read_dispi(BOCHS_X_OFFSET) == 0 && read_dispi(BOCHS_Y_OFFSET) == 0 &&
    read_dispi(BOCHS_BANK) == 0 &&
    read_extension(BOCHS_QEXT_BYTEORDER) == BOCHS_LITTLE_ENDIAN;
}

enum bochs_display_result arch_bochs_display_prepare(const struct boot_info *boot,
    struct pci_device *device, uint32_t width, uint32_t height,
    uintptr_t *address, const char **reason)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!bochs.claim.device);
  if (!arch_bochs_display_matches(device) || device->revision < BOCHS_QEXT_REVISION) {
    return fail(reason, "unsupported QEMU display register interface", false);
  }
  if (width < BOCHS_MIN_DIMENSION || height < BOCHS_MIN_DIMENSION ||
      width > BOCHS_MAX_WIDTH || height > BOCHS_MAX_HEIGHT ||
      width % BOCHS_WIDTH_ALIGNMENT) {
    return fail(reason, "display.size is outside exact hardware mode limits", false);
  }
  if (!(pci_read16(device->address, PCI_COMMAND) & PCI_COMMAND_MEMORY)) {
    return fail(reason, "firmware memory decoding is disabled", false);
  }
  if (!pci_claim_device(device, &bochs.claim)) {
    return fail(reason, "PCI claim failed", false);
  }
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  uint32_t saved_bars[PCI_BAR_COUNT];
  for (unsigned bar = 0; bar < PCI_BAR_COUNT; ++bar) {
    saved_bars[bar] = pci_read32(device->address, PCI_BAR_FIRST + bar * PCI_REGISTER_BYTES);
  }
  pci_write16(&bochs.claim, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
  bool decode_disabled = pci_read16(device->address, PCI_COMMAND) ==
    (command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
  bool sized = decode_disabled && pci_size_bars(&bochs.claim);
  bool bars_restored = true;
  for (unsigned bar = 0; bar < PCI_BAR_COUNT; ++bar) {
    bars_restored &= pci_read32(device->address, PCI_BAR_FIRST + bar * PCI_REGISTER_BYTES) ==
      saved_bars[bar];
  }
  pci_write16(&bochs.claim, PCI_COMMAND, command);
  if (!bars_restored || pci_read16(device->address, PCI_COMMAND) != command) {
    return fail(reason, "PCI BAR/decoding restoration failed", true);
  }
  if (!sized) {
    return fail(reason, "PCI BAR sizing failed", false);
  }
  if (bochs.claim.bars[BOCHS_REGISTER_BAR].bytes != BOCHS_REGISTER_BYTES ||
      !bochs.claim.bars[BOCHS_VRAM_BAR].bytes ||
      pci_map_bar(&bochs.claim, BOCHS_REGISTER_BAR, 0, BOCHS_REGISTER_BYTES,
        boot, &bochs.registers) != MM_OK) {
    return fail(reason, "display register BAR is unavailable", false);
  }
  uint16_t id = read_dispi(BOCHS_ID);
  if (id < BOCHS_ID_FIRST || id > BOCHS_ID_LAST ||
      read_extension(BOCHS_QEXT_SIZE) != BOCHS_QEXT_BYTES) {
    return fail(reason, "unsupported display registers", false);
  }
  uint32_t vram_bytes = (uint32_t)read_dispi(BOCHS_VIDEO_MEMORY) * BOCHS_VRAM_UNIT_BYTES;
  uint64_t bytes = (uint64_t)width * BOCHS_PIXEL_BYTES * height;
  if (!vram_bytes || bytes > vram_bytes || bytes > bochs.claim.bars[BOCHS_VRAM_BAR].bytes) {
    return fail(reason, "display.size exceeds the VRAM or BAR aperture", false);
  }
  for (unsigned index = 0; index < BOCHS_SAVED_REGISTERS; ++index) {
    bochs.saved[index] = read_dispi(index);
  }
  if (!(bochs.saved[BOCHS_ENABLE] & BOCHS_ENABLED) ||
      (bochs.saved[BOCHS_ENABLE] & BOCHS_GETCAPS)) {
    return fail(reason, "firmware DISPI mode is disabled or in capability-query state", false);
  }
  bochs.saved_byteorder = read_extension(BOCHS_QEXT_BYTEORDER);
  if (bochs.saved_byteorder != BOCHS_LITTLE_ENDIAN && bochs.saved_byteorder != BOCHS_BIG_ENDIAN) {
    return fail(reason, "unsupported framebuffer byte order", false);
  }
  if (pci_map_display_bar(&bochs.claim, BOCHS_VRAM_BAR, boot, address) != MM_OK) {
    return fail(reason, "VRAM mapping is unavailable", false);
  }
  bochs.vga = device->subclass == BOCHS_VGA_SUBCLASS;
  if (!select_mode(width, height, vram_bytes)) {
    return fail(reason, "exact mode readback failed", !restore_firmware());
  }
  return BOCHS_DISPLAY_READY;
}

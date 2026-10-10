#ifndef AMD_RENOIR_STATE_H
#define AMD_RENOIR_STATE_H
#include <kernel/pci.h>
#include "renoir_registers.h"
#define DMCUB_CACHE_WINDOWS 8
#define DMCUB_UNCACHED_WINDOWS 2
struct hubp_state {
  uint32_t config, address_config, tiling, viewport_start, viewport_size;
  uint32_t control, clock, pitch, vmid, surface, flip, flip2, crossbar;
  uint32_t aperture_low, aperture_high, tlb;
  uint64_t primary, metadata, inuse, earliest;
};
struct otg_state { uint32_t control, interlace, h_total, h_blank, v_total, v_blank, source, format; };
struct mpcc_state { uint32_t top, bottom, opp, control, status; };
struct dmcub_window { uint32_t base, top, low, high; };
struct inventory_state {
  struct hubp_state hubp[RENOIR_PIPES];
  struct otg_state otg[RENOIR_PIPES];
  struct mpcc_state mpcc[RENOIR_MPCCS];
  uint32_t mux[RENOIR_PIPES];
  uint32_t fb_base, fb_top, fb_offset, gc_offset, mc_base, mc_top, memsize;
  uint32_t context_control, context_base_high, context_base_low;
  uint32_t context_start_high, context_start_low, context_end_high, context_end_low;
  uint32_t dmcub_control, dmcub_security;
  struct dmcub_window cache[DMCUB_CACHE_WINDOWS], uncached[DMCUB_UNCACHED_WINDOWS];
};
struct framebuffer_bar {
  uint32_t low, high;
  uint64_t base;
};

/* Shared bounded read-only probe. Only the flip backend retains mappings. */
bool renoir_map_registers(const struct boot_info *boot, phys_addr_t bar);
void renoir_unmap_registers(void);
uint32_t renoir_read_register(uint32_t offset);
void renoir_snapshot(struct inventory_state *state);
bool renoir_device_ready(struct pci_device *device, unsigned *power, phys_addr_t *bar);
bool renoir_read_framebuffer_bar(struct pci_address address, struct framebuffer_bar *bar);
/* Flip owner only, before AP startup. Makes just the selected HUBP page writable. */
bool renoir_enable_flip_page(unsigned hubp);
/* Hardware allowlist enforced again at the write helper. */
void renoir_write_flip_register(unsigned hubp, uint32_t offset, uint32_t value);
#endif

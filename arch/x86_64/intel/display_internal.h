#ifndef INTEL_DISPLAY_INTERNAL_H
#define INTEL_DISPLAY_INTERNAL_H

#include <kernel/boot.h>
#include <kernel/pci.h>

/* Inherited owner, before AP startup; selected domains remain held on success. */
bool intel_flip_prepare_inherited(const struct boot_info *boot, struct pci_device *device,
    uint64_t bar, uint64_t aperture);
bool intel_display_holds_valid(void);
void intel_display_fence(const char *reason);
uint32_t intel_display_read(uint32_t offset);
void intel_display_write_surface(uint32_t surface);
void intel_display_flip_event_control(bool record);
void intel_display_clear_flip_event(void);
bool intel_display_claim(void);
bool intel_display_gtt_read(uint64_t gpu, uint64_t *value);
uint64_t *intel_display_gtt_write_window(uint64_t gpu);

#endif

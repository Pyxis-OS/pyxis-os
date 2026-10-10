#ifndef AMD_RENOIR_CURSOR_INVENTORY_H
#define AMD_RENOIR_CURSOR_INVENTORY_H

#include "renoir_state.h"
#include <kernel/boot.h>

/* Existing qualified owner/mappings only, BSP/IF=0 before AP startup. */
void renoir_cursor_inventory(const struct boot_info *boot, struct pci_address device,
    const struct inventory_state *state, unsigned hubp, size_t occupied_end);

#endif

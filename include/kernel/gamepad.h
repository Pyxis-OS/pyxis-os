#ifndef KERNEL_GAMEPAD_H
#define KERNEL_GAMEPAD_H

#include <abi/gamepad.h>
#include <stdbool.h>

/* Embedded in retained adapters; registration/publication are BSP/IF=0 and
 * allocate nothing. Lost source backing remains retained with its adapter. */
struct gamepad_source {
  struct gamepad_info info;
  struct gamepad_state physical;
  bool registered, live, valid;
};

bool gamepad_source_attach(struct gamepad_source *source,
    uint16_t vendor, uint16_t product, uint16_t version);
void gamepad_source_report(struct gamepad_source *source,
    const struct gamepad_state *state);
void gamepad_source_lost(struct gamepad_source *source);

#endif

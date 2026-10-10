#ifndef KERNEL_INPUT_SYNTHETIC_POINTER_H
#define KERNEL_INPUT_SYNTHETIC_POINTER_H

#include <stdint.h>

/* Opt-in diagnostic build only. BSP/IF=0, after real input is drained.
 * Drain the ordinary input queue again after this call. */
void pointer_synthetic_tick(uint32_t physical_buttons);

#endif

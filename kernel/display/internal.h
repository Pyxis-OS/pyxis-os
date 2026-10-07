#ifndef KERNEL_DISPLAY_INTERNAL_H
#define KERNEL_DISPLAY_INTERNAL_H

/* Normal driver work stops on panic without submitting or resetting a device. */
bool display_is_panicking(void);
/* BSP/IF=0 before AP startup: withdraw direct panic output and retire the early
 * writer before PCI decoding or mode changes. False means panic owns output. */
bool display_modeset_begin(void);

#endif

#ifndef KERNEL_DISPLAY_INTERNAL_H
#define KERNEL_DISPLAY_INTERNAL_H

/* Normal driver work stops on panic without submitting or resetting a device. */
bool display_is_panicking(void);
/* BSP/IF=0 before AP startup: withdraw direct panic output and retire the early
 * writer before PCI decoding or mode changes. False means panic owns output. */
bool display_modeset_begin(void);

/* Idle blanking (blank.c). Start is the presenter's, after display_start. A
 * frame composes normally, but its scanout copy is solid black while blanked. */
void display_blank_start(void);
bool display_blanked(void);

#endif

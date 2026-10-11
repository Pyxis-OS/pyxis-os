#ifndef REMOTE_PRESENTATION_H
#define REMOTE_PRESENTATION_H

#include "buffer.h"
#include <stdbool.h>
#include <stdint.h>
#include <terminal/style.h>
#include <terminal/text.h>

struct presentation {
  struct byte_buffer *output;
  unsigned columns;
  unsigned rows;
  unsigned host_columns;
  unsigned host_rows;
  unsigned x;
  unsigned y;
  unsigned tab_width;
  bool wrap_pending;
  bool discard_osc;
  enum { PRESENT_TEXT, PRESENT_ESCAPE, PRESENT_CSI_ENTRY,
         PRESENT_CSI, PRESENT_CSI_IGNORE, PRESENT_OSC,
         PRESENT_OSC_ESCAPE } state;
  uint16_t parameters[TERMINAL_CSI_PARAMETERS];
  uint16_t parameters_present;
  unsigned parameter_index;
  bool private_csi;
  struct terminal_style style;
  struct terminal_utf8 utf8;
};

void presentation_begin(struct presentation *screen);
/* Applied geometry changes do not restart the streamed text decoder. */
void presentation_resize(struct presentation *screen, unsigned columns, unsigned rows);
void presentation_data(struct presentation *screen, const unsigned char *data, size_t length);
void presentation_fresh_line(struct presentation *screen);
/* Intercepted OSC controls still interrupt pending text bytes. */
void presentation_text_boundary(struct presentation *screen);

#endif

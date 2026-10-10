#ifndef REMOTE_PRESENTATION_H
#define REMOTE_PRESENTATION_H

#include "buffer.h"
#include <stdbool.h>
#include <stdint.h>
#include <terminal/style.h>

struct presentation {
  struct byte_buffer *output;
  unsigned columns;
  unsigned rows;
  unsigned x;
  unsigned y;
  unsigned tab_width;
  bool wrap_pending;
  enum { PRESENT_TEXT, PRESENT_ESCAPE, PRESENT_CSI_ENTRY,
         PRESENT_CSI, PRESENT_CSI_IGNORE } state;
  uint16_t parameters[TERMINAL_CSI_PARAMETERS];
  uint16_t parameters_present;
  unsigned parameter_index;
  bool private_csi;
  struct terminal_style style;
};

void presentation_begin(struct presentation *screen);
void presentation_data(struct presentation *screen, const unsigned char *data, size_t length);
void presentation_fresh_line(struct presentation *screen);

#endif

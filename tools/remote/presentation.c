#include "presentation.h"

#include <stdio.h>

static void emit(struct presentation *screen, const char *text)
{
  buffer_append(screen->output, text, strlen(text));
}

static void position(struct presentation *screen)
{
  char text[32];
  int length = snprintf(text, sizeof(text), "\x1b[%u;%uH", screen->y + 1, screen->x + 1);
  buffer_append(screen->output, text, (size_t)length);
}

static void style(struct presentation *screen, uint8_t attributes)
{
  static const uint32_t palette[16] = TERMINAL_AARDVARK_PALETTE;
  uint32_t foreground = terminal_color_rgb(screen->style.foreground, palette,
      TERMINAL_AARDVARK_FOREGROUND);
  uint32_t background = terminal_color_rgb(screen->style.background, palette,
      TERMINAL_AARDVARK_BACKGROUND);
  if (attributes & TERMINAL_ATTR_REVERSE) {
    uint32_t previous_foreground = foreground;
    foreground = background;
    background = previous_foreground;
  }
  char text[80];
  int length = snprintf(text, sizeof(text), "\x1b[0%s%s%s;38;2;%u;%u;%u;48;2;%u;%u;%um",
                        attributes & TERMINAL_ATTR_BOLD ? ";1" : "",
                        attributes & TERMINAL_ATTR_ITALIC ? ";3" : "",
                        attributes & TERMINAL_ATTR_UNDERLINE ? ";4" : "",
                        foreground >> 16, foreground >> 8 & 255, foreground & 255,
                        background >> 16, background >> 8 & 255, background & 255);
  buffer_append(screen->output, text, (size_t)length);
}

void presentation_begin(struct presentation *screen)
{
  screen->tab_width = 8;
  screen->utf8 = (struct terminal_utf8){0};
  screen->style = (struct terminal_style){
    .foreground = TERMINAL_COLOR_DEFAULT, .background = TERMINAL_COLOR_DEFAULT
  };
  emit(screen, "\x1b[?1049h\x1b[?7l\x1b[0m");
  style(screen, 0);
  emit(screen, "\x1b[2J\x1b[4 q\x1b[?25h");
  char text[32];
  int length = snprintf(text, sizeof(text), "\x1b[1;%ur", screen->rows);
  buffer_append(screen->output, text, (size_t)length);
  position(screen);
}

static void newline(struct presentation *screen)
{
  screen->wrap_pending = false;
  screen->x = 0;
  if (screen->y + 1 < screen->rows) {
    ++screen->y;
  } else {
    position(screen);
    /* Newly blank native cells keep the colours, with no glyph attributes. */
    if (screen->style.attributes) {
      style(screen, 0);
    }
    emit(screen, "\x1b" "D");
    if (screen->style.attributes) {
      style(screen, screen->style.attributes);
    }
  }
  position(screen);
}

static void character(struct presentation *screen, uint16_t value)
{
  if (screen->wrap_pending) {
    newline(screen);
  }
  position(screen);
  char text[3];
  size_t length = terminal_character_encode(value, text);
  buffer_append(screen->output, text, length);
  if (screen->x == screen->columns - 1) {
    screen->wrap_pending = true;
  } else {
    ++screen->x;
  }
  position(screen);
}

void presentation_text_boundary(struct presentation *screen)
{
  size_t count = terminal_utf8_flush(&screen->utf8);
  for (size_t i = 0; i < count; ++i) {
    character(screen, TERMINAL_REPLACEMENT);
  }
}

void presentation_fresh_line(struct presentation *screen)
{
  presentation_text_boundary(screen);
  screen->state = PRESENT_TEXT;
  if (screen->x || screen->wrap_pending) {
    newline(screen);
  }
}

static void erase(struct presentation *screen, unsigned first, unsigned end)
{
  unsigned saved_x = screen->x;
  unsigned saved_y = screen->y;
  if (screen->style.attributes) {
    style(screen, 0);
  }
  while (first < end) {
    screen->x = first % screen->columns;
    screen->y = first / screen->columns;
    unsigned count = screen->columns - screen->x;
    if (count > end - first) {
      count = end - first;
    }
    position(screen);
    char text[32];
    int length = snprintf(text, sizeof(text), "\x1b[%uX", count);
    buffer_append(screen->output, text, (size_t)length);
    first += count;
  }
  screen->x = saved_x;
  screen->y = saved_y;
  if (screen->style.attributes) {
    style(screen, screen->style.attributes);
  }
  position(screen);
}

static void execute_csi(struct presentation *screen, unsigned char command)
{
  unsigned parameter = screen->parameters[0];
  unsigned count = parameter ? parameter : 1;
  unsigned cell = screen->y * screen->columns + screen->x;
  if (screen->private_csi) {
    if (!screen->parameter_index && parameter == 25) {
      if (command == 'h') {
        emit(screen, "\x1b[?25h");
      } else if (command == 'l') {
        emit(screen, "\x1b[?25l");
      }
    }
    return;
  }
  if (command == 'm') {
    if (terminal_sgr_apply(&screen->style, screen->parameters, screen->parameters_present,
        screen->parameter_index + 1)) {
      style(screen, screen->style.attributes);
    }
    return;
  }
  if (screen->parameter_index > (command == 'H' ? 1u : 0u)) {
    return;
  }
  switch (command) {
  case 'A': screen->y = count > screen->y ? 0 : screen->y - count; break;
  case 'B':
    screen->y = count >= screen->rows - screen->y ? screen->rows - 1 : screen->y + count;
    break;
  case 'C':
    screen->x = count >= screen->columns - screen->x ? screen->columns - 1 : screen->x + count;
    break;
  case 'D': screen->x = count > screen->x ? 0 : screen->x - count; break;
  case 'G': screen->x = count > screen->columns ? screen->columns - 1 : count - 1; break;
  case 'H': {
    unsigned column = screen->parameters[1] ? screen->parameters[1] : 1;
    screen->y = count > screen->rows ? screen->rows - 1 : count - 1;
    screen->x = column > screen->columns ? screen->columns - 1 : column - 1;
    break;
  }
  case 'K':
    if (parameter == 0) {
      erase(screen, cell, (screen->y + 1) * screen->columns);
    } else if (parameter == 1) {
      erase(screen, screen->y * screen->columns, cell + 1);
    } else if (parameter == 2) {
      erase(screen, screen->y * screen->columns, (screen->y + 1) * screen->columns);
    }
    break;
  case 'J':
    if (parameter == 0) {
      erase(screen, cell, screen->columns * screen->rows);
    } else if (parameter == 1) {
      erase(screen, 0, cell + 1);
    } else if (parameter == 2) {
      erase(screen, 0, screen->columns * screen->rows);
    }
    break;
  default: return;
  }
  screen->wrap_pending = false;
  position(screen);
}

void presentation_data(struct presentation *screen, const unsigned char *data, size_t length)
{
  for (size_t i = 0; i < length; ++i) {
    unsigned char byte = data[i];
    if (byte < 0x20 || byte == 0x7f) {
      presentation_text_boundary(screen);
    }
    if (byte == 0x1b) {
      screen->state = PRESENT_ESCAPE;
      screen->parameter_index = 0;
      screen->parameters_present = 0;
      screen->private_csi = false;
      memset(screen->parameters, 0, sizeof(screen->parameters));
      continue;
    }
    if (byte == '\n' || byte == '\r' || byte == '\b' || byte == '\t') {
      screen->state = PRESENT_TEXT;
      screen->wrap_pending = false;
      if (byte == '\n') {
        newline(screen);
      } else if (byte == '\r') {
        screen->x = 0;
      } else if (byte == '\b' && screen->x) {
        --screen->x;
      } else if (byte == '\t') {
        unsigned next = screen->x + screen->tab_width - screen->x % screen->tab_width;
        screen->x = next < screen->columns ? next : screen->columns - 1;
      }
      position(screen);
      continue;
    }
    if (screen->state == PRESENT_ESCAPE) {
      screen->state = byte == '[' ? PRESENT_CSI_ENTRY : PRESENT_TEXT;
      continue;
    }
    if (screen->state == PRESENT_CSI_ENTRY || screen->state == PRESENT_CSI ||
        screen->state == PRESENT_CSI_IGNORE) {
      if (screen->state == PRESENT_CSI_ENTRY) {
        screen->state = PRESENT_CSI;
        if (byte == '?') {
          screen->private_csi = true;
          continue;
        }
      }
      if (byte >= 0x40 && byte <= 0x7e) {
        if (screen->state == PRESENT_CSI) {
          execute_csi(screen, byte);
        }
        screen->state = PRESENT_TEXT;
      } else if (screen->state == PRESENT_CSI) {
        unsigned value = screen->parameters[screen->parameter_index];
        if (byte >= '0' && byte <= '9' && value * 10 + byte - '0' <= UINT16_MAX) {
          screen->parameters[screen->parameter_index] = (uint16_t)(value * 10 + byte - '0');
          screen->parameters_present |= (uint16_t)(1u << screen->parameter_index);
        } else if (byte == ';' && screen->parameter_index + 1 < TERMINAL_CSI_PARAMETERS) {
          ++screen->parameter_index;
        } else {
          screen->state = PRESENT_CSI_IGNORE;
        }
      }
      continue;
    }
    if (byte < 0x20 || byte == 0x7f) {
      continue;
    }
    uint16_t values[4];
    size_t count = terminal_utf8_decode(&screen->utf8, byte, values);
    for (size_t j = 0; j < count; ++j) {
      character(screen, values[j]);
    }
  }
}

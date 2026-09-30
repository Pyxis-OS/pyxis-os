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

static void style(struct presentation *screen, bool reverse)
{
  static const uint32_t palette[16] = {
    0x222734, 0xc26265, 0x52aa60, 0xad9b49, 0x487fd4, 0xaf5bd1, 0x269d9a, 0x5a6377,
    0x3a4152, 0xe48383, 0x75cf84, 0xc7b461, 0x76a8f2, 0xd58bf0, 0x52c4c0, 0xdfe5ee
  };
  uint32_t foreground = screen->foreground == 39 ? 0xb4bcca :
                        palette[screen->foreground >= 90 ? screen->foreground - 90 + 8 :
                                screen->foreground - 30];
  uint32_t background = screen->background == 49 ? 0x0f141f :
                        palette[screen->background >= 100 ? screen->background - 100 + 8 :
                                screen->background - 40];
  if (reverse) {
    uint32_t previous_foreground = foreground;
    foreground = background;
    background = previous_foreground;
  }
  char text[80];
  int length = snprintf(text, sizeof(text), "\x1b[38;2;%u;%u;%u;48;2;%u;%u;%um",
                        foreground >> 16, foreground >> 8 & 255, foreground & 255,
                        background >> 16, background >> 8 & 255, background & 255);
  buffer_append(screen->output, text, (size_t)length);
}

void presentation_begin(struct presentation *screen)
{
  screen->tab_width = 8;
  screen->foreground = 39;
  screen->background = 49;
  emit(screen, "\x1b[?1049h\x1b[?7l\x1b[0m");
  style(screen, false);
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
    /* Native scrolling clears the new row with the non-reversed background. */
    if (screen->reverse) {
      style(screen, false);
    }
    emit(screen, "\x1b" "D");
    if (screen->reverse) {
      style(screen, true);
    }
  }
  position(screen);
}

void presentation_fresh_line(struct presentation *screen)
{
  screen->state = PRESENT_TEXT;
  if (screen->x || screen->wrap_pending) {
    newline(screen);
  }
}

static void erase(struct presentation *screen, unsigned first, unsigned end)
{
  unsigned saved_x = screen->x;
  unsigned saved_y = screen->y;
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
  position(screen);
}

static void select_style(struct presentation *screen, unsigned parameter)
{
  if (parameter == 0) {
    screen->foreground = 39;
    screen->background = 49;
    screen->reverse = false;
  } else if (parameter == 7 || parameter == 27) {
    screen->reverse = parameter == 7;
  } else if (parameter == 39 || (parameter >= 30 && parameter <= 37) ||
             (parameter >= 90 && parameter <= 97)) {
    screen->foreground = parameter;
  } else if (parameter == 49 || (parameter >= 40 && parameter <= 47) ||
             (parameter >= 100 && parameter <= 107)) {
    screen->background = parameter;
  }
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
    for (unsigned i = 0; i <= screen->parameter_index; ++i) {
      select_style(screen, screen->parameters[i]);
    }
    style(screen, screen->reverse);
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
    if (byte == 0x1b) {
      screen->state = PRESENT_ESCAPE;
      screen->parameter_index = 0;
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
        } else if (byte == ';' && screen->parameter_index + 1 < 4) {
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
    if (screen->wrap_pending) {
      newline(screen);
    }
    position(screen);
    /* Native output addresses byte glyphs; keep one host cell per byte. */
    unsigned char glyph = byte < 0x80 ? byte : '?';
    buffer_append(screen->output, &glyph, 1);
    if (screen->x == screen->columns - 1) {
      screen->wrap_pending = true;
    } else {
      ++screen->x;
    }
    position(screen);
  }
}

#include "presentation.h"

#include <stdio.h>

static void emit(struct presentation *screen, const char *text)
{
  buffer_append(screen->output, text, strlen(text));
}

static void position(struct presentation *screen)
{
  unsigned x = screen->host_columns && screen->x >= screen->host_columns ?
               screen->host_columns - 1 : screen->x;
  unsigned y = screen->host_rows && screen->y >= screen->host_rows ?
               screen->host_rows - 1 : screen->y;
  char text[32];
  int length = snprintf(text, sizeof(text), "\x1b[%u;%uH", y + 1, x + 1);
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

static void save_cursor(struct presentation *screen)
{
  screen->saved[screen->application_alternate] = (struct presentation_saved_cursor){
    .x = screen->x,
    .y = screen->y,
    .style = screen->style,
    .wrap_pending = screen->wrap_pending,
  };
}

static void restore_cursor(struct presentation *screen)
{
  const struct presentation_saved_cursor *saved = &screen->saved[screen->application_alternate];
  screen->x = saved->x < screen->columns ? saved->x : screen->columns - 1;
  screen->y = saved->y < screen->rows ? saved->y : screen->rows - 1;
  screen->style = saved->style;
  screen->wrap_pending = saved->wrap_pending && screen->x == screen->columns - 1;
  style(screen, screen->style.attributes);
  position(screen);
}

static void margins(struct presentation *screen)
{
  unsigned bottom = screen->region_bottom;
  if (screen->host_rows && bottom >= screen->host_rows) {
    bottom = screen->host_rows - 1;
  }
  if (screen->region_top >= bottom) {
    emit(screen, "\x1b[r");
  } else {
    char text[32];
    int length = snprintf(text, sizeof(text), "\x1b[%u;%ur",
        screen->region_top + 1, bottom + 1);
    buffer_append(screen->output, text, (size_t)length);
  }
}

static void clear_rows(struct presentation *screen, unsigned columns, unsigned rows)
{
  if (screen->host_columns && columns > screen->host_columns) {
    columns = screen->host_columns;
  }
  if (screen->host_rows && rows > screen->host_rows) {
    rows = screen->host_rows;
  }
  if (screen->style.attributes) {
    style(screen, 0);
  }
  for (unsigned y = 0; y < rows; ++y) {
    char text[48];
    int length = snprintf(text, sizeof(text), "\x1b[%u;1H\x1b[%uX", y + 1, columns);
    buffer_append(screen->output, text, (size_t)length);
  }
  if (screen->style.attributes) {
    style(screen, screen->style.attributes);
  }
}

void presentation_begin(struct presentation *screen)
{
  screen->tab_width = 8;
  screen->region_top = 0;
  screen->region_bottom = screen->rows - 1;
  screen->utf8 = (struct terminal_utf8){0};
  screen->style = (struct terminal_style){
    .foreground = TERMINAL_COLOR_DEFAULT, .background = TERMINAL_COLOR_DEFAULT
  };
  for (unsigned i = 0; i < 2; ++i) {
    screen->saved[i] = (struct presentation_saved_cursor){.style = screen->style};
  }
  emit(screen, "\x1b[?1049h\x1b[?7l\x1b[0m");
  style(screen, 0);
  emit(screen, "\x1b[2J\x1b[4 q\x1b[?25h");
  margins(screen);
  position(screen);
}

void presentation_resize(struct presentation *screen, unsigned columns, unsigned rows)
{
  if (screen->columns == columns && screen->rows == rows) {
    return;
  }
  unsigned clear_columns = columns > screen->columns ? columns : screen->columns;
  unsigned clear_height = rows > screen->rows ? rows : screen->rows;
  screen->columns = columns;
  screen->rows = rows;
  screen->region_top = 0;
  screen->region_bottom = rows - 1;
  if (screen->x >= columns) {
    screen->x = columns - 1;
  }
  if (screen->y >= rows) {
    screen->y = rows - 1;
  }
  for (unsigned i = 0; i < 2; ++i) {
    struct presentation_saved_cursor *saved = &screen->saved[i];
    if (saved->x >= columns) {
      saved->x = columns - 1;
    }
    if (saved->y >= rows) {
      saved->y = rows - 1;
    }
  }
  screen->wrap_pending = false;
  margins(screen);
  clear_rows(screen, clear_columns, clear_height);
  position(screen);
}

/* The caller restores the host cursor to the tracked position afterward.
 * A single visible row must be cleared, not moved inside a wider host region. */
static void scroll_lines(struct presentation *screen, unsigned top, unsigned count, bool insert)
{
  unsigned bottom = screen->region_bottom;
  if (screen->host_rows && bottom >= screen->host_rows) {
    bottom = screen->host_rows - 1;
  }
  if (top > bottom) {
    return;
  }
  if (count > bottom - top + 1) {
    count = bottom - top + 1;
  }
  if (screen->style.attributes) {
    style(screen, 0);
  }
  char text[48];
  int length = snprintf(text, sizeof(text), "\x1b[%u;1H", top + 1);
  buffer_append(screen->output, text, (size_t)length);
  if (top == bottom) {
    unsigned columns = screen->columns;
    if (screen->host_columns && columns > screen->host_columns) {
      columns = screen->host_columns;
    }
    length = snprintf(text, sizeof(text), "\x1b[%uX", columns);
  } else {
    length = snprintf(text, sizeof(text), "\x1b[%u%c", count, insert ? 'L' : 'M');
  }
  buffer_append(screen->output, text, (size_t)length);
  if (screen->style.attributes) {
    style(screen, screen->style.attributes);
  }
}

static void newline(struct presentation *screen)
{
  screen->wrap_pending = false;
  screen->x = 0;
  if (screen->y == screen->region_bottom) {
    if (screen->region_top == screen->region_bottom) {
      scroll_lines(screen, screen->region_top, 1, false);
    } else if (!screen->host_rows || screen->region_bottom < screen->host_rows) {
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
  } else if (screen->y + 1 < screen->rows) {
    ++screen->y;
  }
  position(screen);
}

static void reverse_index(struct presentation *screen)
{
  screen->wrap_pending = false;
  if (screen->y == screen->region_top) {
    scroll_lines(screen, screen->region_top, 1, true);
  } else if (screen->y) {
    --screen->y;
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
  if ((!screen->host_columns || screen->x < screen->host_columns) &&
      (!screen->host_rows || screen->y < screen->host_rows)) {
    buffer_append(screen->output, text, length);
  }
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
    if ((!screen->host_rows || screen->y < screen->host_rows) &&
        (!screen->host_columns || screen->x < screen->host_columns)) {
      unsigned visible = count;
      if (screen->host_columns && visible > screen->host_columns - screen->x) {
        visible = screen->host_columns - screen->x;
      }
      position(screen);
      char text[32];
      int length = snprintf(text, sizeof(text), "\x1b[%uX", visible);
      buffer_append(screen->output, text, (size_t)length);
    }
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
    if (!screen->parameter_index && (command == 'h' || command == 'l')) {
      if (parameter == 25) {
        emit(screen, command == 'h' ? "\x1b[?25h" : "\x1b[?25l");
      } else if (parameter == 1049 && screen->application_alternate != (command == 'h')) {
        /* The connection owns the host alternate screen. Application screen
         * changes reset native margins without switching that host screen. */
        if (command == 'h') {
          save_cursor(screen);
        }
        screen->application_alternate = command == 'h';
        screen->region_top = 0;
        screen->region_bottom = screen->rows - 1;
        screen->wrap_pending = false;
        margins(screen);
        if (screen->application_alternate) {
          position(screen);
        } else {
          restore_cursor(screen);
        }
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
  if (command == 'r') {
    if (screen->parameter_index > 1) {
      return;
    }
    unsigned top = parameter ? parameter : 1;
    unsigned bottom = screen->parameters[1] ? screen->parameters[1] : screen->rows;
    if (top < bottom && bottom <= screen->rows) {
      screen->region_top = top - 1;
      screen->region_bottom = bottom - 1;
      screen->x = screen->y = 0;
      screen->wrap_pending = false;
      margins(screen);
      position(screen);
    }
    return;
  }
  if (screen->parameter_index > (command == 'H' ? 1u : 0u)) {
    return;
  }
  if (command == 's' || command == 'u') {
    if (parameter) {
      return;
    }
    if (command == 's') {
      save_cursor(screen);
    } else {
      restore_cursor(screen);
    }
    return;
  }
  bool in_region = screen->y >= screen->region_top && screen->y <= screen->region_bottom;
  switch (command) {
  case 'A': {
    unsigned limit = in_region ? screen->region_top : 0;
    screen->y = count > screen->y - limit ? limit : screen->y - count;
    break;
  }
  case 'B': {
    unsigned limit = in_region ? screen->region_bottom : screen->rows - 1;
    screen->y = count > limit - screen->y ? limit : screen->y + count;
    break;
  }
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
  case 'L':
  case 'M':
    if (in_region) {
      scroll_lines(screen, screen->y, count, command == 'L');
      screen->x = 0;
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
    /* Mux has no routed OSC side channels. Discard across records without buffering. */
    if (screen->state == PRESENT_OSC || screen->state == PRESENT_OSC_ESCAPE) {
      if (byte == 7 || (screen->state == PRESENT_OSC_ESCAPE && byte == '\\')) {
        screen->state = PRESENT_TEXT;
      } else {
        screen->state = byte == 0x1b ? PRESENT_OSC_ESCAPE : PRESENT_OSC;
      }
      continue;
    }
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
      if (byte == '7' || byte == '8') {
        screen->state = PRESENT_TEXT;
        if (byte == '7') {
          save_cursor(screen);
        } else {
          restore_cursor(screen);
        }
      } else if (byte == 'M') {
        screen->state = PRESENT_TEXT;
        reverse_index(screen);
      } else if (byte == ']' && screen->discard_osc) {
        screen->state = PRESENT_OSC;
      } else {
        screen->state = byte == '[' ? PRESENT_CSI_ENTRY : PRESENT_TEXT;
      }
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

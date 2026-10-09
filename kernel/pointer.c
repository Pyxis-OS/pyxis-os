#include <arch/smp.h>
#include <kernel/display.h>
#include <kernel/mm/heap.h>
#include <kernel/log.h>
#include <kernel/object/display.h>
#include <kernel/object/pointer.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/space.h>

enum destination_kind { DESTINATION_NONE, DESTINATION_TERMINAL, DESTINATION_GRAPHICS };
struct pointer_destination {
  struct space *space;
  enum destination_kind kind;
};

static int64_t position_x, position_y;
static uint32_t device_buttons, consumed_buttons, drag_buttons;
static struct pointer_destination drag;
static struct pointer_object *hover;
static struct pointer_object *locked_pointer;
static uint8_t arrow_pixels[16 * 24 * 4], terminal_pixels[9 * 20 * 4];

/* Black outline, white fill, transparent space. */
static const char *const arrow_shape[] = {
  "#           ",
  "##          ",
  "#.#         ",
  "#..#        ",
  "#...#       ",
  "#....#      ",
  "#.....#     ",
  "#......#    ",
  "#.......#   ",
  "#........#  ",
  "#.........# ",
  "#......#####",
  "#...#..#    ",
  "#..##..#    ",
  "#.#  #..#   ",
  "##   #..#   ",
  "#     #..#  ",
  "      #..#  ",
  "       ##   ",
};

static const char *const terminal_shape[] = {
  " ####### ",
  "#.......#",
  " ###.### ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  "   #.#   ",
  " ###.### ",
  "#.......#",
  " ####### ",
};

_Static_assert(sizeof(arrow_shape) / sizeof(arrow_shape[0]) <= 24, "arrow image rows");
_Static_assert(sizeof(terminal_shape) / sizeof(terminal_shape[0]) <= 20, "terminal image rows");

static void default_image(uint8_t *pixels, size_t width,
    const char *const *rows, size_t height)
{
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; rows[y][x]; ++x) {
      KASSERT(x < width);
      char colour = rows[y][x];
      KASSERT(colour == '#' || colour == '.' || colour == ' ');
      if (colour != ' ') {
        uint8_t *pixel = pixels + (y * width + x) * 4;
        pixel[0] = pixel[1] = pixel[2] = colour == '.' ? 255 : 0;
        pixel[3] = 255;
      }
    }
  }
}

void pointer_init(void)
{
  KASSERT(arch_cpu_index() == 0);
  const struct framebuffer *layout = display_layout();
  position_x = layout->width / 2;
  position_y = layout->height / 2;
  default_image(arrow_pixels, 16, arrow_shape,
      sizeof(arrow_shape) / sizeof(arrow_shape[0]));
  default_image(terminal_pixels, 9, terminal_shape,
      sizeof(terminal_shape) / sizeof(terminal_shape[0]));
}

static int64_t clamped_move(int64_t position, int32_t delta, size_t extent)
{
  KASSERT(extent && extent <= INT64_MAX);
  int64_t high = extent - 1;
  if (delta > 0 && position > high - delta) {
    return high;
  }
  if (delta < 0 && position < -(int64_t)delta) {
    return 0;
  }
  return position + delta;
}

static void clamp_position(void)
{
  const struct framebuffer *layout = display_layout();
  position_x = clamped_move(position_x, 0, layout->width);
  position_y = clamped_move(position_y, 0, layout->height);
  if (position_x >= (int64_t)layout->width) {
    position_x = layout->width - 1;
  }
  if (position_y >= (int64_t)layout->height) {
    position_y = layout->height - 1;
  }
}

static struct pointer_destination hit_test(void)
{
  struct space *space = space_pointer_active();
  if (position_y < (int64_t)space_pointer_content_y()) {
    return (struct pointer_destination){0};
  }
  struct display_object *display = space->display;
  if (display->visible) {
    const struct framebuffer *mapping = display_slot_layout(display);
    if (position_x < (int64_t)mapping->width &&
        position_y - (int64_t)space_pointer_content_y() < (int64_t)mapping->height) {
      return (struct pointer_destination){space, DESTINATION_GRAPHICS};
    }
    return (struct pointer_destination){0};
  }
  return (struct pointer_destination){space, DESTINATION_TERMINAL};
}

bool pointer_surface_focused(struct pointer_object *pointer)
{
  return pointer->space == space_pointer_active() &&
      (pointer_is_terminal(pointer) ? !pointer->space->display->visible :
          pointer->space->display->visible);
}

struct pointer_geometry pointer_surface_geometry(struct pointer_object *pointer)
{
  struct space *space = pointer->space;
  const struct framebuffer *mapping = display_slot_layout(space->display);
  bool terminal = pointer_is_terminal(pointer);
  return (struct pointer_geometry){
    .width = space->fb->width, .height = space->fb->height,
    .generation = space->tty->geometry_generation,
    .mapping_identity = terminal ? pointer->view_identity : space->display->mapping_identity,
    .mapping_width = terminal ? space->fb->width : mapping ? mapping->width : 0,
    .mapping_height = terminal ? space->fb->height : mapping ? mapping->height : 0,
  };
}

struct pointer_event pointer_position_event(struct pointer_object *pointer, uint32_t type)
{
  struct pointer_geometry geometry = pointer_surface_geometry(pointer);
  return (struct pointer_event){.x = position_x,
    .y = position_y - (int64_t)space_pointer_content_y(),
    .generation = geometry.generation, .mapping_identity = geometry.mapping_identity,
    .buttons = pointer->accepted, .type = type};
}

static struct pointer_object *destination_pointer(struct pointer_destination target)
{
  if (target.kind == DESTINATION_GRAPHICS) {
    return target.space->pointer;
  }
  if (target.kind == DESTINATION_TERMINAL) {
    return target.space->terminal_pointer;
  }
  return NULL;
}

static void cancel_local_drag(struct space *space)
{
  bool locked = log_begin();
  if (locked) {
    tty_selection_cancel_drag(space->tty);
  }
  log_end(locked);
}

static void clear_local_selection(struct space *space)
{
  bool locked = log_begin();
  if (locked) {
    tty_selection_clear(space->tty);
  }
  log_end(locked);
}

static void update_hover(struct pointer_destination target)
{
  struct pointer_object *next = destination_pointer(target);
  if (next && (!next->owner || !next->focused)) {
    next = NULL;
  }
  if (locked_pointer || drag.space || next == hover) {
    return;
  }
  if (hover) {
    pointer_queue_state(hover, POINTER_LEAVE);
  }
  hover = next;
  if (hover) {
    pointer_queue_state(hover, POINTER_ENTER);
  }
}

void pointer_subscription_started(struct pointer_object *pointer)
{
  KASSERT(arch_cpu_index() == 0 && pointer->owner);
  if (pointer_is_terminal(pointer)) {
    clear_local_selection(pointer->space);
    if (drag.space == pointer->space && drag.kind == DESTINATION_TERMINAL) {
      drag = (struct pointer_destination){0};
      drag_buttons = 0;
    }
  }
  update_hover(hit_test());
}

void pointer_subscription_ended(struct pointer_object *pointer)
{
  if (!pointer_is_terminal(pointer)) {
    pointer_surface_unlock(pointer, true);
  } else {
    clear_local_selection(pointer->space);
  }
  pointer->activation_ready = false;
  enum destination_kind kind = pointer_is_terminal(pointer) ? DESTINATION_TERMINAL : DESTINATION_GRAPHICS;
  if (drag.space == pointer->space && drag.kind == kind) {
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  if (hover == pointer) {
    hover = NULL;
  }
}

void pointer_space_changed(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  bool focused = pointer_surface_focused(space->pointer);
  if (!focused) {
    pointer_surface_unlock(space->pointer, true);
    space->pointer->activation_ready = false;
  }
  if (drag.space == space &&
      (space != space_pointer_active() ||
       (drag.kind == DESTINATION_GRAPHICS) != space->display->visible)) {
    cancel_local_drag(space);
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  pointer_focus(space->pointer, focused);
  pointer_focus(space->terminal_pointer, pointer_surface_focused(space->terminal_pointer));
  update_hover(hit_test());
}

void pointer_geometry_changed(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  clamp_position();
  if (space->pointer->relative) {
    pointer_queue_state(space->pointer, POINTER_GEOMETRY_CHANGED);
    return;
  }
  if (drag.space == space && drag.kind == DESTINATION_GRAPHICS) {
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  pointer_reset_input(space->pointer, POINTER_GEOMETRY_CHANGED);
  if (hover == space->pointer) {
    hover = NULL;
  }
  update_hover(hit_test());
}

void pointer_terminal_view_changed(struct pointer_object *pointer)
{
  KASSERT(arch_cpu_index() == 0 && pointer_is_terminal(pointer));
  if (drag.space == pointer->space && drag.kind == DESTINATION_TERMINAL) {
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  pointer_reset_input(pointer, POINTER_GEOMETRY_CHANGED);
  if (hover == pointer) {
    hover = NULL;
  }
  update_hover(hit_test());
}

void pointer_terminal_geometry_changed(struct space *space)
{
  struct pointer_object *pointer = space->terminal_pointer;
  if (pointer->view_identity != UINT64_MAX) {
    ++pointer->view_identity;
  }
  pointer_terminal_view_changed(pointer);
}

void pointer_surface_ended(struct space *space)
{
  pointer_end_session(space->pointer);
}

void pointer_image_release(struct pointer_image *image)
{
  KASSERT(arch_cpu_index() == 0);
  if (image) {
    KASSERT(image->references);
    if (--image->references == 0) {
      kfree(image);
    }
  }
}

void pointer_frame_snapshot(struct pointer_frame *frame)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_destination target = hit_test();
  *frame = (struct pointer_frame){.x = position_x, .y = position_y,
    .pixels = arrow_pixels, .width = 16, .height = 24,
    .visible = space_pointer_input_available() && display_available() && !locked_pointer};
  if (target.kind == DESTINATION_TERMINAL) {
    frame->pixels = terminal_pixels;
    frame->width = 9;
    frame->height = 20;
    frame->hotspot_x = 4;
    frame->hotspot_y = 10;
  }
  struct pointer_object *pointer = destination_pointer(target);
  if (pointer && pointer->owner) {
    frame->visible = frame->visible && !pointer->hidden;
    struct pointer_image *image = pointer->image;
    if (image) {
      KASSERT(image->references && image->references < SIZE_MAX);
      ++image->references;
      frame->image = image;
      frame->pixels = image->pixels;
      frame->width = image->width;
      frame->height = image->height;
      frame->hotspot_x = image->hotspot_x;
      frame->hotspot_y = image->hotspot_y;
    }
  }
}

void pointer_frame_release(struct pointer_frame *frame)
{
  pointer_image_release(frame->image);
  *frame = (struct pointer_frame){0};
}

void pointer_source_lost(uint32_t physical_buttons)
{
  KASSERT(arch_cpu_index() == 0);
  if (locked_pointer) {
    pointer_surface_unlock(locked_pointer, true);
  }
  device_buttons = physical_buttons;
  consumed_buttons = drag_buttons = 0;
  drag = (struct pointer_destination){0};
  hover = NULL;
  for (struct space *space = space_caelum(); space; space = space->next) {
    space->pointer->activation_ready = false;
    cancel_local_drag(space);
    pointer_reset_input(space->pointer, POINTER_STATE_RESET);
    pointer_reset_input(space->terminal_pointer, POINTER_STATE_RESET);
  }
}

void pointer_handle_input(const struct pointer_input_report *event)
{
  KASSERT(arch_cpu_index() == 0);
  uint32_t pressed = event->buttons & ~device_buttons & ~event->suppressed_buttons;
  if (locked_pointer) {
    device_buttons = event->buttons;
    consumed_buttons &= device_buttons;
    struct pointer_event input = pointer_position_event(locked_pointer, POINTER_INPUT);
    input.dx = event->dx;
    input.dy = event->dy;
    input.wheel = event->wheel;
    input.buttons = device_buttons & ~consumed_buttons;
    pointer_queue_input(locked_pointer, input, pressed & ~consumed_buttons, true);
    return;
  }
  const struct framebuffer *layout = display_layout();
  position_x = clamped_move(position_x, event->dx, layout->width);
  position_y = clamped_move(position_y, event->dy, layout->height);
  device_buttons = event->buttons;
  consumed_buttons &= device_buttons;
  pressed &= ~consumed_buttons;
  struct pointer_destination target = hit_test();
  if (!drag.space && (pressed & POINTER_BUTTON_LEFT)) {
    struct space *tab = space_pointer_tab(position_x, position_y);
    if (tab) {
      consumed_buttons |= POINTER_BUTTON_LEFT;
      space_pointer_select(tab);
      return;
    }
    if (target.kind == DESTINATION_GRAPHICS && target.space->pointer_activation_required &&
        !target.space->pointer->activation_ready && target.space->pointer->owner &&
        target.space->pointer->focused) {
      consumed_buttons |= POINTER_BUTTON_LEFT;
      target.space->pointer->activation_ready = true;
      pointer_queue_state(target.space->pointer, POINTER_ACTIVATED);
      return;
    }
  }
  update_hover(target);
  uint32_t buttons = device_buttons & ~consumed_buttons;
  if (!drag.space && pressed && target.space) {
    drag = target;
    drag_buttons = pressed & buttons;
  }
  struct pointer_destination recipient = drag.space ? drag : target;
  struct pointer_object *recipient_pointer = destination_pointer(recipient);
  if (recipient_pointer && recipient_pointer->owner &&
      (event->dx || event->dy || pressed || drag_buttons != (drag_buttons & buttons))) {
    struct pointer_object *pointer = recipient_pointer;
    struct pointer_event input = pointer_position_event(pointer, POINTER_INPUT);
    input.buttons = buttons;
    pointer_queue_input(pointer, input, pressed, true);
    if (drag.space) {
      drag_buttons = pointer->accepted;
    }
  } else if (recipient.kind == DESTINATION_TERMINAL && !recipient_pointer->owner) {
    bool locked = log_begin();
    if (locked) {
      tty_selection_input(recipient.space->tty, position_x,
          position_y - (int64_t)space_pointer_content_y(),
          pressed & POINTER_BUTTON_LEFT, buttons & POINTER_BUTTON_LEFT);
    }
    log_end(locked);
    if (drag.space) {
      drag_buttons = (drag_buttons & buttons) | (pressed & buttons);
    }
  } else if (drag.space) {
    drag_buttons = (drag_buttons & buttons) | (pressed & buttons);
  }
  struct pointer_object *wheel_pointer = destination_pointer(target);
  if (event->wheel && wheel_pointer && wheel_pointer->owner) {
    struct pointer_object *pointer = wheel_pointer;
    struct pointer_event input = pointer_position_event(pointer, POINTER_INPUT);
    input.wheel = event->wheel;
    pointer_queue_input(pointer, input, 0, false);
  }
  if (drag.space && !drag_buttons) {
    drag = (struct pointer_destination){0};
    update_hover(target);
  }
}

enum call_status pointer_surface_warp(struct pointer_object *pointer,
    const struct pointer_warp_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_geometry geometry = pointer_surface_geometry(pointer);
  if (request->generation != geometry.generation ||
      request->mapping_identity != geometry.mapping_identity) {
    return CALL_BUSY;
  }
  if (pointer->relative || !pointer_surface_focused(pointer) || consumed_buttons ||
      ((device_buttons | space_pointer_suppressed_buttons()) & ~pointer->accepted) ||
      (drag.space && (drag.space != pointer->space || drag.kind != DESTINATION_GRAPHICS))) {
    return CALL_DENIED;
  }
  uint64_t width = MIN(geometry.width, geometry.mapping_width);
  uint64_t height = MIN(geometry.height, geometry.mapping_height);
  if (request->x < 0 || request->y < 0 || (uint64_t)request->x >= width ||
      (uint64_t)request->y >= height) {
    return CALL_BAD_REQUEST;
  }
  /* Graphics mappings are anchored at physical x=0, below navigation. */
  position_x = request->x;
  position_y = request->y + (int64_t)space_pointer_content_y();
  update_hover(hit_test());
  pointer_queue_input(pointer, pointer_position_event(pointer, POINTER_INPUT), 0, false);
  return CALL_OK;
}

enum call_status pointer_surface_lock(struct pointer_object *pointer)
{
  KASSERT(arch_cpu_index() == 0 && pointer->owner);
  bool activated = pointer->activation_ready;
  pointer->activation_ready = false;
  if (!space_pointer_input_available()) {
    return CALL_UNAVAILABLE;
  }
  if (locked_pointer == pointer) {
    return CALL_OK;
  }
  if (locked_pointer) {
    return CALL_BUSY;
  }
  if (!pointer_surface_focused(pointer) ||
      (pointer->space->pointer_activation_required && !activated) ||
      ((device_buttons | space_pointer_suppressed_buttons()) &
       ~consumed_buttons & ~pointer->accepted) ||
      (drag.space && (drag.space != pointer->space || drag.kind != DESTINATION_GRAPHICS))) {
    return CALL_DENIED;
  }
  drag = (struct pointer_destination){0};
  drag_buttons = 0;
  hover = NULL;
  locked_pointer = pointer;
  pointer_set_lock(pointer, true);
  return CALL_OK;
}

void pointer_surface_unlock(struct pointer_object *pointer, bool require_activation)
{
  KASSERT(arch_cpu_index() == 0);
  if (locked_pointer != pointer) {
    return;
  }
  if (require_activation) {
    pointer->space->pointer_activation_required = true;
    pointer->activation_ready = false;
  }
  locked_pointer = NULL;
  pointer_set_lock(pointer, false);
  clamp_position();
  hover = NULL;
  struct pointer_destination target = hit_test();
  if (target.kind != DESTINATION_GRAPHICS || target.space != pointer->space) {
    pointer_queue_state(pointer, POINTER_LEAVE);
  }
  update_hover(target);
}

void pointer_escape(void)
{
  KASSERT(arch_cpu_index() == 0);
  if (locked_pointer) {
    pointer_surface_unlock(locked_pointer, true);
  }
}

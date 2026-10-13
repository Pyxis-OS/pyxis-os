#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/ps2.h>
#include <arch/smp.h>
#include <kernel/boot/options.h>
#include <kernel/input.h>
#include <kernel/input/touchpad.h>
#include <pointer-synthetic-config.h>
#if POINTER_SYNTHETIC_ENABLED
#include <kernel/input/synthetic_pointer.h>
#endif
#include <kernel/mouse.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/memory.h>
#include <stdatomic.h>
#include "usb/core.h"

#define INPUT_KEY_EVENTS 256
#define INPUT_POINTER_EVENTS 1024
#define USB_REPEAT_DELAY_NS UINT64_C(500000000)
#define USB_REPEAT_INTERVAL_NS UINT64_C(33000000)
#define PS2_BUTTONS (POINTER_BUTTON_LEFT | POINTER_BUTTON_RIGHT | POINTER_BUTTON_MIDDLE)

static struct input_source *sources;
static struct input_source ps2_keyboard, ps2_pointer, ps2_touchpad, ps2_guest;
static bool touchpad_initialized;
static atomic_size_t keyboard_sources, pointer_sources;
static size_t held[KEY_COUNT];
static struct input_source *repeat_owner[KEY_COUNT];
static unsigned locks;
static uint32_t mouse_scroll_direction, touchpad_scroll_direction;
static bool policy_installed;
static uint64_t last_typing_ns;
static struct key_event key_events[INPUT_KEY_EVENTS];
static size_t key_read, key_count;

struct pending_pointer {
  struct input_source *source;
  struct pointer_input_report report;
  bool loss, revoke;
};
static struct pending_pointer pointer_events[INPUT_POINTER_EVENTS];
static size_t pointer_read, pointer_count;
static bool pointer_reset_pending;
static void attach_ps2_pointer(void);
#if POINTER_SYNTHETIC_ENABLED
static uint32_t drained_physical_buttons;
#endif

static void assert_input_owner(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

void input_source_attach(struct input_source *source, bool keyboard, bool pointer)
{
  assert_input_owner();
  KASSERT(source && (keyboard || pointer));
  if (!source->registered) {
    source->next = sources;
    sources = source;
    source->registered = true;
  }
  if (keyboard && !source->keyboard_live) {
    source->keyboard_live = true;
    atomic_fetch_add_explicit(&keyboard_sources, 1, memory_order_release);
  }
  if (pointer && !source->pointer_live) {
    if (!source->scroll_denominator_x || !source->scroll_denominator_y) {
      input_source_pointer_configure(source, INPUT_POINTER_MOUSE,
          POINTER_SCROLL_UNITS_PER_DETENT, 1, POINTER_SCROLL_UNITS_PER_DETENT, 1);
    }
    source->pointer_live = true;
    atomic_fetch_add_explicit(&pointer_sources, 1, memory_order_release);
  }
  if (source != &ps2_keyboard && ps2_keyboard_available() && !ps2_keyboard.keyboard_live) {
    input_source_attach(&ps2_keyboard, true, false);
  }
  if (source != &ps2_pointer && source != &ps2_touchpad && source != &ps2_guest) {
    attach_ps2_pointer();
  }
}

static unsigned modifiers(void)
{
  unsigned value = locks;
  if (held[KEY_LEFT_SHIFT] || held[KEY_RIGHT_SHIFT]) {
    value |= KEY_MOD_SHIFT;
  }
  if (held[KEY_LEFT_CONTROL] || held[KEY_RIGHT_CONTROL]) {
    value |= KEY_MOD_CONTROL;
  }
  if (held[KEY_LEFT_ALT] || held[KEY_RIGHT_ALT]) {
    value |= KEY_MOD_ALT;
  }
  if (held[KEY_LEFT_SUPER] || held[KEY_RIGHT_SUPER]) {
    value |= KEY_MOD_SUPER;
  }
  return value;
}

static bool modifier_key(enum key_code key)
{
  return key == KEY_LEFT_SHIFT || key == KEY_RIGHT_SHIFT ||
      key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL ||
      key == KEY_LEFT_ALT || key == KEY_RIGHT_ALT ||
      key == KEY_LEFT_SUPER || key == KEY_RIGHT_SUPER;
}

static void record_typing(struct input_source *source, enum key_code key, uint64_t now)
{
  if (!source->synthetic && !modifier_key(key)) {
    last_typing_ns = now ? now : arch_monotonic_ns();
  }
}

static bool repeatable(enum key_code key)
{
  return key != KEY_NONE && !modifier_key(key) && key != KEY_CAPS_LOCK &&
      key != KEY_NUM_LOCK && key != KEY_SCROLL_LOCK && key != KEY_PAUSE &&
      key != KEY_PRINT_SCREEN;
}

static void reset_keyboard(void)
{
  memset(held, 0, sizeof(held));
  memset(repeat_owner, 0, sizeof(repeat_owner));
  locks = 0;
  for (struct input_source *source = sources; source; source = source->next) {
    for (size_t key = 1; key < KEY_COUNT; ++key) {
      source->suppressed_keys[key] = source->keys[key];
    }
    source->repeat_key = KEY_NONE;
  }
  key_read = 0;
  key_count = 1;
  key_events[0] = (struct key_event){.action = KEY_STATE_RESET};
}

static void queue_key(enum key_code key, enum key_action action)
{
  if (key_count == INPUT_KEY_EVENTS) {
    reset_keyboard();
    return;
  }
  key_events[(key_read + key_count) % INPUT_KEY_EVENTS] =
      (struct key_event){.key = key, .action = action, .modifiers = modifiers()};
  ++key_count;
}

static void change_key(struct input_source *source, enum key_code key, bool down,
    bool typematic, uint64_t now)
{
  if (source->keys[key] == down) {
    if (down && typematic) {
      /* A recognized hardware repeat is typing even when its delivery is
       * quarantined after another source loses continuity. */
      record_typing(source, key, now);
      if (!source->suppressed_keys[key] && repeat_owner[key] == source) {
        queue_key(key, KEY_REPEAT);
      }
    }
    return;
  }
  source->keys[key] = down;
  if (down) {
    source->suppressed_keys[key] = false;
    record_typing(source, key, now);
    if (!held[key]++) {
      if (key == KEY_CAPS_LOCK) {
        locks ^= KEY_MOD_CAPS_LOCK;
      } else if (key == KEY_NUM_LOCK) {
        locks ^= KEY_MOD_NUM_LOCK;
      } else if (key == KEY_SCROLL_LOCK) {
        locks ^= KEY_MOD_SCROLL_LOCK;
      }
      repeat_owner[key] = source;
      queue_key(key, KEY_PRESS);
    }
    if (source != &ps2_keyboard && repeatable(key) && !source->suppressed_keys[key]) {
      source->repeat_key = key;
      source->repeat_deadline = now + USB_REPEAT_DELAY_NS;
    }
  } else {
    if (source->repeat_key == key) {
      source->repeat_key = KEY_NONE;
    }
    if (source->suppressed_keys[key]) {
      source->suppressed_keys[key] = false;
      if (!held[key]) {
        queue_key(key, KEY_RELEASE);
      }
      return;
    }
    KASSERT(held[key]);
    if (!--held[key]) {
      repeat_owner[key] = NULL;
      queue_key(key, KEY_RELEASE);
    } else if (repeat_owner[key] == source) {
      repeat_owner[key] = NULL;
      for (struct input_source *other = sources; other; other = other->next) {
        if (other->keyboard_live && other->keys[key] && !other->suppressed_keys[key]) {
          repeat_owner[key] = other;
          if (other != &ps2_keyboard) {
            other->repeat_deadline = arch_monotonic_ns() + USB_REPEAT_DELAY_NS;
          }
          break;
        }
      }
    }
  }
}

void input_source_initial_keyboard(struct input_source *source, const bool keys[KEY_COUNT])
{
  assert_input_owner();
  KASSERT(source && source->keyboard_live && keys);
  for (size_t key = 1; key < KEY_COUNT; ++key) {
    source->keys[key] = keys[key];
    source->suppressed_keys[key] = keys[key];
    source->keyboard_initial_hold |= keys[key];
  }
  source->repeat_key = KEY_NONE;
}

static void drain_ps2_keyboard(void)
{
  if (ps2_keyboard_available() && !ps2_keyboard.keyboard_live) {
    input_source_attach(&ps2_keyboard, true, false);
  }
  struct key_event event;
  while (ps2_keyboard_read_event(&event)) {
    if (event.action == KEY_STATE_RESET) {
      reset_keyboard();
    } else if (event.key > KEY_NONE && event.key < KEY_COUNT) {
      change_key(&ps2_keyboard, event.key, event.action != KEY_RELEASE, true, arch_monotonic_ns());
    }
  }
}

void input_keyboard_snapshot(struct input_source *source, const bool keys[KEY_COUNT])
{
  assert_input_owner();
  KASSERT(source && source->keyboard_live && keys);
  drain_ps2_keyboard();
  if (source->keyboard_initial_hold) {
    source->keyboard_unresolved = false;
    bool any_held = false;
    for (size_t key = 1; key < KEY_COUNT; ++key) {
      any_held |= keys[key];
      if (source->keys[key] && !keys[key]) {
        change_key(source, key, false, false, 0);
      } else if (keys[key]) {
        source->keys[key] = true;
        source->suppressed_keys[key] = true;
      }
    }
    source->keyboard_initial_hold = any_held;
    return;
  }
  uint64_t now = arch_monotonic_ns();
  if (source->keyboard_unresolved) {
    source->keyboard_unresolved = false;
    source->repeat_deadline = now + USB_REPEAT_DELAY_NS;
    /* Rollover hid these edges; reappearance is not a fresh physical press. */
    for (size_t key = 1; key < KEY_COUNT; ++key) {
      if (keys[key] && !source->keys[key]) {
        source->keys[key] = true;
        source->suppressed_keys[key] = true;
      }
    }
  }
  for (size_t key = 1; key < KEY_COUNT; ++key) {
    if (source->keys[key] && !keys[key]) {
      change_key(source, key, false, false, now);
    }
  }
  /* Simultaneous modifier/key presses must publish the modifier first. */
  for (size_t key = 1; key < KEY_COUNT; ++key) {
    if (keys[key] && !source->keys[key] && modifier_key(key)) {
      change_key(source, key, true, false, now);
    }
  }
  for (size_t key = 1; key < KEY_COUNT; ++key) {
    if (keys[key] && !source->keys[key] && !modifier_key(key)) {
      change_key(source, key, true, false, now);
    }
  }
}

void input_keyboard_unresolved(struct input_source *source)
{
  assert_input_owner();
  KASSERT(source && source->keyboard_live);
  if (!source->keyboard_unresolved) {
    unsigned previous_locks = locks;
    reset_keyboard();
    locks = previous_locks;
    source->keyboard_unresolved = true;
  }
}

static bool keyboard_sources_resolved(void)
{
  for (const struct input_source *source = sources; source; source = source->next) {
    if (source->keyboard_live && (source->keyboard_unresolved || source->keyboard_initial_hold)) {
      return false;
    }
  }
  return true;
}

bool keyboard_available(void)
{
  return ps2_keyboard_available() ||
      atomic_load_explicit(&keyboard_sources, memory_order_acquire) != 0;
}

bool keyboard_read_event(struct key_event *event)
{
  uint64_t flags = cpu_save_interrupts();
  assert_input_owner();
  KASSERT(event);
  drain_ps2_keyboard();
  if (!key_count) {
    uint64_t now = 0;
    for (struct input_source *source = sources; source; source = source->next) {
      enum key_code key = source->repeat_key;
      if (source == &ps2_keyboard || !source->keyboard_live || source->keyboard_unresolved ||
          key == KEY_NONE || repeat_owner[key] != source || source->suppressed_keys[key]) {
        continue;
      }
      if (!now) {
        now = arch_monotonic_ns();
      }
      if (now >= source->repeat_deadline && keyboard_sources_resolved() && usb_hid_input_complete()) {
        source->repeat_deadline = now + USB_REPEAT_INTERVAL_NS;
        record_typing(source, key, now);
        queue_key(key, KEY_REPEAT);
      }
    }
  }
  bool found = key_count != 0;
  if (found) {
    *event = key_events[key_read];
    key_read = (key_read + 1) % INPUT_KEY_EVENTS;
    --key_count;
  }
  cpu_restore_interrupts(flags);
  return found;
}

bool keyboard_sync_device(void)
{
  assert_input_owner();
  bool ps2_complete = !ps2_keyboard_available() || ps2_keyboard_sync_device();
  return keyboard_available() && ps2_complete && keyboard_sources_resolved() && usb_hid_input_complete();
}

bool keyboard_input_complete(void)
{
  assert_input_owner();
  return keyboard_available() && !key_count &&
      (!ps2_keyboard_available() || ps2_keyboard_input_complete()) &&
      keyboard_sources_resolved() && usb_hid_input_complete();
}

static uint32_t pointer_buttons(bool suppression)
{
  uint32_t buttons = 0;
  for (struct input_source *source = sources; source; source = source->next) {
    if (source->pointer_live) {
      buttons |= suppression ? source->suppressed_buttons : source->buttons;
    }
  }
  return buttons;
}

static void queue_pointer(struct pending_pointer event)
{
  if (pointer_count == INPUT_POINTER_EVENTS) {
    pointer_read = pointer_count = 0;
    pointer_reset_pending = true;
    if (touchpad_initialized) {
      touchpad_reset();
    }
    for (struct input_source *source = sources; source; source = source->next) {
      source->buttons = source->pending_buttons;
      source->suppressed_buttons |= source->buttons;
      input_source_scroll_reset(source);
    }
    return;
  }
  pointer_events[(pointer_read + pointer_count) % INPUT_POINTER_EVENTS] = event;
  ++pointer_count;
}

void input_source_initial_pointer(struct input_source *source, uint32_t buttons)
{
  assert_input_owner();
  KASSERT(source && source->pointer_live);
  source->pending_buttons = buttons;
  source->suppressed_buttons = source->pending_buttons;
  source->pointer_initial_hold = source->pending_buttons != 0;
  queue_pointer((struct pending_pointer){.source = source,
      .report = {.buttons = source->pending_buttons}});
}

void input_source_scroll_reset(struct input_source *source)
{
  assert_input_owner();
  KASSERT(source);
  source->scroll_remainder_x = source->scroll_remainder_y = 0;
}

void input_source_pointer_configure(struct input_source *source,
    enum input_pointer_kind kind, uint32_t numerator_x, uint32_t denominator_x,
    uint32_t numerator_y, uint32_t denominator_y)
{
  assert_input_owner();
  KASSERT(source && (kind == INPUT_POINTER_MOUSE || kind == INPUT_POINTER_TOUCHPAD));
  KASSERT(numerator_x && numerator_x <= INT32_MAX && denominator_x && denominator_x <= INT32_MAX);
  KASSERT(numerator_y && numerator_y <= INT32_MAX && denominator_y && denominator_y <= INT32_MAX);
  source->pointer_kind = kind;
  source->scroll_numerator_x = numerator_x;
  source->scroll_denominator_x = denominator_x;
  source->scroll_numerator_y = numerator_y;
  source->scroll_denominator_y = denominator_y;
  input_source_scroll_reset(source);
}

static int32_t normalized_scroll(struct input_source *source, int32_t delta, bool horizontal)
{
  uint32_t numerator = horizontal ? source->scroll_numerator_x : source->scroll_numerator_y;
  uint32_t denominator = horizontal ? source->scroll_denominator_x : source->scroll_denominator_y;
  int64_t *remainder = horizontal ? &source->scroll_remainder_x : &source->scroll_remainder_y;
  int64_t scaled = (int64_t)delta * numerator + *remainder;
  int64_t units = scaled / denominator;
  *remainder = scaled % denominator;
  uint32_t direction = source->pointer_kind == INPUT_POINTER_TOUCHPAD ?
      touchpad_scroll_direction : mouse_scroll_direction;
  if (direction == INPUT_SCROLL_NATURAL) {
    units = -units;
  }
  return units > INT32_MAX ? INT32_MAX : units < INT32_MIN ? INT32_MIN : (int32_t)units;
}

uint64_t input_last_typing_ns(void)
{
  assert_input_owner();
  drain_ps2_keyboard();
  return last_typing_ns;
}

bool input_policy_install(uint32_t mouse_direction, uint32_t touchpad_direction)
{
  assert_input_owner();
  KASSERT(mouse_direction <= INPUT_SCROLL_TRADITIONAL &&
      touchpad_direction <= INPUT_SCROLL_TRADITIONAL);
  if (policy_installed) {
    return false;
  }
  mouse_scroll_direction = mouse_direction;
  touchpad_scroll_direction = touchpad_direction;
  policy_installed = true;
  for (struct input_source *source = sources; source; source = source->next) {
    input_source_scroll_reset(source);
  }
  return true;
}

void input_pointer_report(struct input_source *source, int32_t dx, int32_t dy,
    int32_t scroll_x, int32_t scroll_y, uint32_t buttons)
{
  assert_input_owner();
  KASSERT(source && source->pointer_live);
  source->pending_buttons = buttons;
  queue_pointer((struct pending_pointer){.source = source,
      .report = {.dx = dx, .dy = dy,
        .scroll_x = normalized_scroll(source, scroll_x, true),
        .scroll_y = normalized_scroll(source, scroll_y, false),
        .buttons = source->pending_buttons}});
}

void input_source_lost(struct input_source *source)
{
  assert_input_owner();
  KASSERT(source);
  if (source->keyboard_live) {
    source->keyboard_live = false;
    atomic_fetch_sub_explicit(&keyboard_sources, 1, memory_order_release);
    memset(source->keys, 0, sizeof(source->keys));
    memset(source->suppressed_keys, 0, sizeof(source->suppressed_keys));
    reset_keyboard();
  }
  if (source->pointer_live) {
    source->pointer_live = false;
    size_t remaining = atomic_fetch_sub_explicit(&pointer_sources, 1, memory_order_release) - 1;
    bool revoke = source->pending_buttons != 0 || !remaining;
    source->pending_buttons = 0;
    input_source_scroll_reset(source);
    queue_pointer((struct pending_pointer){.source = source, .loss = true, .revoke = revoke});
  }
}

bool input_pointer_available(void)
{
  return mouse_available() ||
      atomic_load_explicit(&pointer_sources, memory_order_acquire) != 0;
}

uint32_t input_pointer_suppressed_buttons(void)
{
  assert_input_owner();
  return pointer_buttons(true);
}

static void drain_pointer_events(void)
{
  if (pointer_reset_pending) {
    pointer_reset_pending = false;
    pointer_source_lost(pointer_buttons(false));
  }
  while (pointer_count) {
    struct pending_pointer pending = pointer_events[pointer_read];
    pointer_read = (pointer_read + 1) % INPUT_POINTER_EVENTS;
    --pointer_count;
    struct input_source *source = pending.source;
#if POINTER_SYNTHETIC_ENABLED
    if (!source->synthetic) {
      drained_physical_buttons |= source->buttons | pending.report.buttons;
    }
#endif
    if (pending.loss) {
      uint32_t previous_buttons = pointer_buttons(false) | source->buttons;
      source->buttons = 0;
      if (pending.revoke) {
        for (struct input_source *other = sources; other; other = other->next) {
          other->suppressed_buttons |= other->buttons;
        }
        pointer_source_lost(pointer_buttons(false));
      } else if (previous_buttons != pointer_buttons(false)) {
        pointer_handle_input(&(struct pointer_input_report){
            .buttons = pointer_buttons(false), .suppressed_buttons = pointer_buttons(true)});
      }
    } else if (source->pointer_live) {
      source->buttons = pending.report.buttons;
      if (source->pointer_initial_hold) {
        source->pointer_initial_hold = source->buttons != 0;
        source->suppressed_buttons = source->buttons;
      } else {
        source->suppressed_buttons &= source->buttons;
      }
      pending.report.buttons = pointer_buttons(false);
      pending.report.suppressed_buttons = pointer_buttons(true);
      pointer_handle_input(&pending.report);
    }
  }
}

static void attach_ps2_pointer(void)
{
  if (!mouse_available()) {
    return;
  }
  const struct mouse_profile *profile = mouse_profile();
  if (!profile->absolute) {
    if (!ps2_pointer.pointer_live) {
      input_source_attach(&ps2_pointer, false, true);
    }
    return;
  }
  bool initial = !touchpad_initialized;
  if (initial) {
    touchpad_init(profile);
    touchpad_initialized = true;
  }
  if (!ps2_touchpad.pointer_live) {
    input_source_pointer_configure(&ps2_touchpad, INPUT_POINTER_TOUCHPAD,
        POINTER_SCROLL_UNITS_PER_DETENT,
        profile->x_resolution * TOUCHPAD_SCROLL_MM_PER_DETENT,
        POINTER_SCROLL_UNITS_PER_DETENT,
        profile->y_resolution * TOUCHPAD_SCROLL_MM_PER_DETENT);
    input_source_attach(&ps2_touchpad, false, true);
    if (initial) {
      input_source_initial_pointer(&ps2_touchpad, profile->initial_touch_buttons);
    }
  }
}

static void report_ps2_guest(const struct mouse_event *event)
{
  if (!event->guest_buttons_valid) {
    return;
  }
  if (!ps2_guest.pointer_live) {
    input_source_attach(&ps2_guest, false, true);
    /* The first observed guest snapshot can contain an already held switch.
     * Quarantine it until release, like initial holds from USB admission. */
    input_source_initial_pointer(&ps2_guest, event->guest_buttons);
  }
  input_pointer_report(&ps2_guest, event->dx, event->dy, 0, event->wheel,
      event->guest_buttons);
}

static void lose_ps2_pointer(void)
{
  struct input_source *ps2_sources[] = {&ps2_pointer, &ps2_touchpad, &ps2_guest};
  for (size_t i = 0; i < sizeof(ps2_sources) / sizeof(ps2_sources[0]); ++i) {
    input_source_lost(ps2_sources[i]);
    ps2_sources[i]->suppressed_buttons = PS2_BUTTONS;
  }
  if (touchpad_initialized) {
    touchpad_reset();
  }
}

void input_pointer_drain(void)
{
  _Static_assert(MOUSE_BUTTON_LEFT == POINTER_BUTTON_LEFT &&
      MOUSE_BUTTON_RIGHT == POINTER_BUTTON_RIGHT &&
      MOUSE_BUTTON_MIDDLE == POINTER_BUTTON_MIDDLE, "normalized pointer button bits");
  uint64_t flags = cpu_save_interrupts();
  assert_input_owner();
  attach_ps2_pointer();
  struct mouse_event event;
  while (mouse_read_event(&event)) {
    if (event.kind == MOUSE_EVENT_RESET) {
      lose_ps2_pointer();
      continue;
    }
    attach_ps2_pointer();
    if (event.kind == MOUSE_EVENT_RELATIVE) {
      input_pointer_report(&ps2_pointer, event.dx, event.dy, 0, event.wheel, event.buttons);
    } else if (event.kind == MOUSE_EVENT_TOUCHPAD) {
      struct touchpad_motion motion;
      touchpad_report(&event.touch, input_last_typing_ns(), arch_monotonic_ns(), &motion);
      if (motion.reset) {
        input_source_scroll_reset(&ps2_touchpad);
      }
      input_pointer_report(&ps2_touchpad, motion.dx, motion.dy,
          motion.scroll_x, motion.scroll_y, event.touch_buttons_valid ?
          event.touch_buttons : ps2_touchpad.pending_buttons);
      /* Basic-W switch snapshots carry no guest motion. */
      event.dx = event.dy = event.wheel = 0;
      report_ps2_guest(&event);
    } else if (event.kind == MOUSE_EVENT_GUEST) {
      report_ps2_guest(&event);
      if (event.touch_buttons_valid) {
        input_pointer_report(&ps2_touchpad, 0, 0, 0, 0, event.touch_buttons);
      }
    }
  }
  attach_ps2_pointer();
  if (touchpad_initialized && boot_options_get()->ps2_synaptics_diagnose) {
    uint64_t now = arch_monotonic_ns();
    touchpad_service(now);
    mouse_diagnose(now);
  }
#if POINTER_SYNTHETIC_ENABLED
  drained_physical_buttons = 0;
#endif
  drain_pointer_events();
#if POINTER_SYNTHETIC_ENABLED
  pointer_synthetic_tick(drained_physical_buttons | pointer_buttons(false));
  drain_pointer_events();
#endif
  usb_hid_diagnose_tick();
  cpu_restore_interrupts(flags);
}

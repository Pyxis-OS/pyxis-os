# Mouse input and Quake

Status: **accepted, 2026-10-04.** One milestone covers mouse input and a Quake
port, so Quake gets mouse look on day one. The shareware data is redistributable
and ships in the image; the owner's retail data stays private. The owner accepted
the defaults for all three [decisions](#owner-decisions) and the tasks below. Any
decision can be revised later by the owner. Each task starts only when the owner
says so.

## Goal

`quake` in the application-space shell runs the shareware episode with keyboard
and mouse look, in QEMU and natively on the ThinkPad. A private build can use the
owner's retail data instead.

**Completion:**
- in QEMU, `timedemo demo1` completes and reports its frame rate, and E1M1 is
  playable with mouse look;
- natively on the ThinkPad, the owner plays with the TrackPoint and records the
  `timedemo` result.

## Starting point

- **Keyboard:** the [PS/2 keyboard](../devices/keyboard.md) driver deliberately
  disables the 8042's auxiliary (mouse) port. Userspace gets keys through
  exclusive keyboard sessions with focus and reset events.
- **Display:** [Doom](../userland/doom.md) already shows the pattern for a game:
  exclusive display and keyboard sessions, an integer-scaled game buffer and a
  libc file path for its data.
- **Engine:** [quakegeneric](https://github.com/erysdren/quakegeneric) does for
  Quake what doomgeneric does for Doom. It is the original software renderer
  behind a small set of platform hooks: frame and palette, keys, mouse motion and
  time.
- **Data:** the owner supplied the shareware `PAK0.PAK` (v1.06, SHA-256
  `35a9c55e…a946af`) and retail `PAK0.PAK` and `PAK1.PAK`. Retail `PAK0.PAK` is
  byte-identical to the shareware one.

## Owner decisions

Accepted 2026-10-04, with the defaults below.

1. **Where mouse input comes from.**
   - The 8042's auxiliary port, as a standard PS/2 mouse. That is
     QEMU's default mouse.
   - **On the ThinkPad it covers both built-in devices.** The owner's Fedora
     inventory lists `SynPS/2 Synaptics TouchPad` and `TPPS/2 Elan TrackPoint`,
     both PS/2; the TrackPoint sits behind the touchpad's pass-through port. In
     the plain relative mode firmware leaves the touchpad in, it reports as a
     standard PS/2 mouse, and TrackPoint motion normally arrives through the same
     stream. Native task 1 confirms which devices actually report.
   - Synaptics absolute mode (multi-finger, gestures) and a separate TrackPoint
     stream are later work.
   - Probe for the IntelliMouse wheel (4-byte packets), since Quake uses the wheel
     to change weapons.
   - **A USB HID mouse** is harder. Examples are the owner's Logitech MX Master 3S
     on its USB receiver, or any mouse in QEMU's `usb-mouse`. It needs configured
     interrupt endpoints, while xHCI currently does only endpoint-0 control
     transfers, plus a HID boot-protocol driver. USB mass storage needs the same
     endpoint work, so a USB mouse fits best after storage. It is not part of this
     milestone.
2. **What userspace receives.**
   - A **pointer session**, mirroring keyboard sessions: exclusive
     acquisition, delivery only while the owning space has focus, and focus-loss
     and reset events after which applications release held buttons.
   - Events are **relative**: raw dx, dy and wheel counts plus button state, with
     no acceleration (applications scale) and no on-screen cursor or absolute
     coordinates in v1.
   - When the queue is full, motion is merged into the last motion event instead
     of dropping events.
3. **Packaging and scope of the port.**
   - The unchanged shareware `pak0.pak` is pinned like
     `third_party/doom-shareware`, with its checksum, source and shareware license
     text, and ships in the ordinary image at `app://share/quake/id1/pak0.pak`.
   - A private `QUAKE_DATA=/path/to/id1` replaces it with retail paks, staged with
     the lowercase names Quake expects, as `DOOM_WAD` does for Doom.
   - No sound, no network multiplayer and no CD audio in v1.

## Tasks

- [ ] **1. PS/2 mouse driver** (kernel).
  - Enable the auxiliary port and route aux bytes separately from keyboard bytes
    by the controller status bit.
  - Bounded setup: reset, defaults, the wheel probe, then enable reporting.
  - Packet synchronisation (byte 0 bit 3) and overflow handling, with discards
    counted.
  - **A mouse failure leaves the mouse unavailable and must never affect the
    keyboard or boot.** That is the hard rule, given the ThinkPad's keyboard
    history.
  - **Finish when:** motion, buttons and the wheel are seen in QEMU (kernel log
    and debugger), and the owner sees TrackPoint events natively.
- [ ] **2. Pointer sessions** (ABI, kernel, libpyxis).
  - The session contract from decision 2, with the same focus and space-switch
    rules as keyboard sessions.
  - Document it beside the keyboard contract.
  - **Finish when:** a consumer receives motion, wheel and buttons only while
    focused, and releases state on focus loss or reset.
- [ ] **3. Quake port** (ports repository, plus image packaging in the parent).
  - Pinned quakegeneric with a Pyxis adapter:
    - 8-bit palette conversion and integer scaling, as in Doom;
    - key mapping;
    - mouse look from pointer sessions;
    - a monotonic clock;
    - inactive time excluded, as in Doom.
  - Pin and license the shareware data, and add the `QUAKE_DATA` override.
  - **Finish when:** the completion goal above is met, with the `timedemo demo1`
    frame rate recorded in QEMU and natively.

Task 1 can start immediately. Task 3's non-mouse parts (build, display,
keyboard, data) can proceed in parallel with tasks 1 and 2.

## Validation

- Ordinary builds and interactive QEMU (graphical display, its default PS/2
  mouse).
- The engine's own `timedemo demo1` is the frame-rate measurement, repeated for
  matched samples. No new benchmark tooling.
- Native ThinkPad checks are run by the owner.
- Keyboard behaviour must be unchanged. Check it in QEMU and natively once the
  aux port is enabled.

## Out of scope

USB HID mice, Synaptics absolute/multitouch mode, an on-screen pointer and
absolute positioning, sound, Quake networking, QuakeWorld, GL renderers and mission packs.
Mouse support in Doom is a natural small follow-up once pointer sessions exist,
but it isn't part of this milestone.

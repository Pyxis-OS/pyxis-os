# Terminal input and editing

Application terminal calls serve both framebuffer consoles and
[independent terminal sessions](terminal-sessions.md).

[`libterm`](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/term.h) builds terminal behavior on libpyxis's
console calls. A `struct terminal` borrows explicit input and output handles;
it neither allocates nor closes them. The named startup `input`/`output` console
grants are separate from libc's dedicated standard-stream handles. Redirecting
or closing a standard stream does not change these explicit terminal grants;
launchers must decide whether to delegate them. Input/output, partial writes, write-all,
size queries and control helpers preserve native call statuses. Console output
wrappers now preserve those statuses too, rather than reducing failures to -1.

The line helper takes a printable ASCII prompt and a separate caller-owned
buffer. Capacity includes the terminating NUL. Successful input excludes the
newline; cancellation, EOF, input loss and errors clear the buffer and return zero
length. Ctrl+C cancels this line locally, moves to a fresh line and returns
`TERM_LINE_CANCELLED`; it is not a signal and does not terminate the process.
The line helpers hold a [passthrough handle](#interrupt-arming-and-passthrough)
while editing, so an armed shell does not terminate a program for Ctrl+C typed
at its prompt. `term_passthrough()` exposes the same request for other
programs.
Ctrl+D on an empty line returns `TERM_LINE_EOF`; on a nonempty line it is
ignored. This is an editor result, not closure of the console input object.
Actual terminal input EOF also returns TERM_LINE_EOF, discarding any unfinished
line. The key decoder reports TERM_KEY_EOF even during an incomplete escape
sequence; hangup remains ENDPOINT_CLOSED. The shell exits successfully on EOF; Lua also accepts it at a continuation
prompt, discarding the unfinished chunk and exiting.

`term_read_line_initial` also borrows a disjoint, NUL-terminated printable ASCII
initial value. It starts with that value visible and the cursor at its end; the
caller can edit or submit it. It must fit the caller's buffer.
All non-success results discard the initial text just as they discard typed
text. The existing line helpers start empty.

Editing supports insertion, Backspace, Delete, Left/Right, Home/End and Enter.
A steady block cursor marks the editing position, including the blank cell
after the last character. The prompt and line wrap with the TTY, including when
older output scrolls upward. The helper redraws the line and uses relative row
movement to reach the editing position. It does not need the screen's absolute
cursor position or a kernel-side line buffer.

The maximum line length is `capacity - 1`. When the prompt, input and cursor
cell fit, they use the ordinary wrapped view. Otherwise the editor displays a
bounded window of the prompt/input around the logical cursor; the complete text
remains editable. Growing the terminal restores text that fits again. At the
buffer limit, further insertion is rejected, the cursor cell turns red, and the
result's `limit_reached` flag records that rejection. Editing and submission
remain available; movement or deletion restores the normal cursor appearance.

The helper first calls `term_fresh_line`: under the output lock, the console
ends an incomplete escape sequence and advances only if the cursor is not already
at column zero. This WRITE-authorized operation has no reply payload and does
not clear text or reset colors. The helper then clears from that position to
screen end and uses default colors. The caller must have exclusive input/output
use for the duration: another writer can invalidate cursor tracking. In
particular, the single-CPU development fallback shares Caelum's TTY with kernel
logs, which can visibly disrupt editing. Use an application space on a multicore
boot for an undisturbed terminal. No foreground arbitration is introduced here.

`term_read_key` decodes input incrementally, one byte per read, without a
read-ahead buffer that could steal a future foreground child's input. It blocks
for the initial byte and allows 100 ms between escape-sequence bytes. Standalone
Escape returns byte 27; incomplete or unsupported sequences return
`TERM_KEY_UNKNOWN`. Other bytes retain their values, while navigation uses the
named `TERM_KEY_*` values. These are logical terminal keys, not physical events.
The line editor ignores Escape and Page Up/Down. With a caller-owned
`struct term_history`, `term_read_line_history` and `term_read_line_marked`
recall submitted lines with Up/Down; other entry points ignore them.
Tab and non-ASCII input are ignored; Unicode widths remain later work. `CALL_INPUT_LOST` abandons the
line and returns a distinct result, so the caller can explain the loss and retry.
On an output failure, the screen/cursor may be partially updated and must not be
assumed to match the discarded line.

`term_read_key_timeout` bounds only the wait for the initial byte. Zero polls;
no initial byte returns `CALL_TIMED_OUT` with `TERM_KEY_UNKNOWN`. Once a byte
arrives, it uses the same Escape decoding rules as the blocking helper, even
when decoding extends past the caller's timeout. It never discards a partial
key merely because that initial timeout elapsed.

The resize-aware event reader retains partial Escape/CSI decoding and its
monotonic byte deadline across resize events. It reports keys and geometry
changes as distinct events. It requires an explicitly delegated clock READ
handle; stock sessions provide one. Line helpers fall back to ordinary key
reading when that resource is absent, preserving existing grants.

Kilo uses the event reader with a remaining monotonic deadline to expire
transient status messages while idle. Resize redraws the editor or active
search prompt without a key; search prompts do not expire. It retains its
minimum of two columns and three rows. No periodic redraw or polling is needed.

## TTY output controls

The TTY keeps its parser state across writes. The supported subset is:

| Bytes | Effect |
| --- | --- |
| LF / CR / BS | New row at column zero / column zero / one column left without erasing |
| HT (`\t`) | Move to the next tab stop (eight columns by default), clamped to the last column, without erasing |
| `CSI n A/B/C/D` | Move up/down/right/left, clamped to screen edges |
| `CSI n G` | Set column (one-based) |
| `CSI row;column H` | Set position (one-based) |
| `CSI 0/1/2 K` | Erase line after/before/around cursor, including its cell |
| `CSI 0/1/2 J` | Erase screen after/before/around cursor, including its cell |
| `CSI ? 25 h/l` | Show/hide the nonblinking block cursor |
| `CSI ... m` | Reset, reverse video, palette/default foreground and background |

`CSI` is Escape followed by `[`. Movement defaults to one; position defaults to
row/column one. Erasing does not move the cursor. Style parameters are 0 (reset),
7/27 (reverse on/off), 30–37/90–97 (foreground), 40–47/100–107 (background), and
39/49 (separate terminal defaults). Unsupported controls are ignored. At most
four parameters of up to 65535 are accepted; malformed or oversized CSI commands
are discarded through their final byte. A new Escape starts a fresh sequence.
This is a focused subset, not a claim of full ANSI/VT compatibility.

Horizontal tab stops are at multiples of the TTY's tab width from column zero. A tab at a
stop advances to the next one. Tabs only move the cursor: they preserve existing
cells, never wrap or scroll, and cancel pending wrap even at the right edge.
The next printable character then writes at that column, setting pending wrap
if it fills the last cell. Like CR/LF/BS, a tab ends an incomplete escape sequence.
Cat passes tabs through unchanged; Kilo still controls their display inside the
editor.

`term_set_tab_width(term, columns)` uses the output handle to set spacing on
that console's TTY through `console_set_tab_width`. `CONSOLE_SET_TAB_WIDTH`
requires WRITE authority and accepts 1–32 columns; invalid widths return
`CALL_BAD_REQUEST` without changing the setting. The fixed-size console payload
contains `console_tab_width_request`; there is no reply payload.

Each TTY starts at eight columns. The setting is shared by its writers, survives
clearing the screen and process exit, and changes only subsequent tabs. Updating
it uses the output lock and preserves existing pixels, cursor position, pending
wrap and escape-parser state. Other TTYs retain their own settings. Startup
selection comes from [session configuration](session-configuration.md); the
packaged file explicitly selects eight columns.

Writing the rightmost cell leaves the cursor there with a pending wrap. Only the
next printable character moves to the next row (scrolling at the bottom).
CR, LF, BS, HT, supported cursor movement and erasing cancel pending wrap; styling
and cursor visibility preserve it. LF still starts a new row at column zero.
This permits a full-width line followed by CR/LF without a second line advance,
and permits writing the bottom-right cell before repositioning without scrolling.

Cursor state belongs to each TTY and defaults to visible. Presentation snapshots
its position/visibility under the output lock before copying the frame. The
cursor's text row is composed off-screen with the cursor already drawn, then
copied, so the screen never shows that row without the cursor. The block uses
the scheme's cursor color for the cell's background and its cursor-text color
for the glyph, taking the cell's top-left pixel as its background. The cursor
never modifies the space framebuffer, so moving, hiding or switching spaces
leaves no saved-pixel restoration work. The framebuffer has no page flip or
vertical sync, so presentation still permits tearing.

`console_size()` returns character columns/rows and geometry generation as one
snapshot; `term_size()` queries the terminal's output handle and returns only
the dimensions. Local [display resizing](../interfaces/graphics.md#live-destination-geometry)
updates every TTY without replacing its console. It crops whole rasterized cells
without reflow, keeps the cursor visible and fills new cells with the background.
Colors, tab width and escape-parser state survive; pending wrap is cleared.
Independent sessions keep their own dimensions; attachment-authorized resize
advances their generation. The remote server does not yet request resize.
`WAIT_RESIZED` compares the interest's observed generation against this
snapshot. READ or WRITE authorizes observation; input `WAIT_READABLE` needs
READ. The blocked line editor wakes, re-queries geometry and redraws its retained
text without requiring a key. It moves back toward the old rendered origin,
clamping at the top if resize cropped that origin, and erases from there to
screen end. Normal TTY scrolling keeps the bounded view reachable without
clearing surviving rows above the editor. Other programs adapt when they opt
into readiness or query SIZE again.

The shell uses the helper for command input, then stops reading while a child
runs. It retries after cancellation or input loss and rejects submitted lines
that reached the editor limit. The native entry points and result contract live
beside the declarations in `term.h`; no global terminal or stdio stream is hidden
inside libterm, and the shell owns its history.

## Bounded console reads

`console_read`/`term_read` continue to wait indefinitely. Their `_timeout`
variants take milliseconds: zero polls, and positive values through UINT32_MAX
bound the wait. Expiry returns `CALL_TIMED_OUT` with no bytes consumed. This is
not EOF; zero-capacity reads still succeed immediately without acknowledging
input loss. Ordinary stdio input retains its blocking behavior.

The native READ request has an explicit timeout field; `CONSOLE_WAIT_FOREVER`
selects indefinite waiting. One deadline covers both waiting for read ownership
and waiting for input. Available ownership/input wins a race with expiry when
observed under the console lock. Reader ownership remains FIFO; a timed-out
queued reader removes itself before its task wait record can be reused.

Deadlines use the shared [monotonic clock](../kernel/timekeeping.md). Expiry and task
resumption can be late if interrupts or scheduling are delayed, but missing
interrupts no longer loses elapsed time. These waits do not provide a calendar
clock or an elapsed-host-time guarantee under VM pauses.
The scheduler retains its wake-before-park rule: timeout cannot enqueue a task
until its stack has been saved. Resource wait pointers are detached under the
console lock before the task prepares another wait.

## Interrupt arming and passthrough

Framebuffer console input and terminal-session input can recognize Ctrl+C as
an interrupt instead of data. [Foreground interruption](foreground-interruption.md)
records the design, and the shell
[arms it for foreground jobs](shell.md#interrupting-foreground-commands).

Authority starts at the input object. The kernel's initial per-space `input`
grant and the terminal-create input carry READ and `CONSOLE_RIGHT_INTERRUPT`.
Standard input is validated as exactly READ, so a command never receives
interrupt authority as a stream. Session setup passes the right on to session
successors and the remote root shell; every other launch narrows `input` to READ.

- `console_arm_interrupt()` needs INTERRUPT. It returns an armed handle to the
  same input object, carrying only `CONSOLE_RIGHT_ARMED`. The input stays armed
  while any armed grant exists. Closing the last one, including at process
  exit, disarms and clears the latch. Arming while armed returns `CALL_BUSY`,
  and each armed interval starts with a clear latch. BUSY can also be
  transient while another arm request is installing its handle.
- `console_passthrough()` needs READ. It returns a handle carrying only
  `CONSOLE_RIGHT_PASSTHROUGH`. While any such grant exists, Ctrl+C stays
  ordinary input. Closing it withdraws the request.

Both returned handles support no other console operation. Their rights are
never combined with READ, WRITE or INTERRUPT, so the input object counts each
kind of grant separately, as terminal attachments count HANGUP grants.

While armed without passthrough, each byte 3 from keyboard text or terminal
injection is removed from input and sets one latch, and input queued before it
is discarded. Bytes after the last Ctrl+C in the same keyboard sequence or
injection are kept. Recognition runs before the console's input-loss check and
before a terminal's capacity check, so a full queue cannot hide the interrupt.
On the console it also clears a pending input loss, since that loss described
input the interrupt discards; later bytes start a fresh stream.
The discarded prefix of an injection counts as accepted. Unarmed byte 3 is data,
as before. Raw keyboard owners receive key events that never become console
text, so they are unaffected.

`wait_many` accepts `WAIT_INTERRUPT` on an armed handle, independently of
READABLE and RESIZED on ordinary console/terminal grants. It reports the latch level-triggered
and does not consume it; there is no acknowledgement operation. A terminal
hangup also reports `WAIT_ERROR`. The latch is set on the BSP under the input
lock and published through the ordinary readiness notification.

## Local visible-cell selection

Local framebuffer terminals retain their visible 8-bit glyphs alongside the
raster. A left drag selects a linear inclusive range; release finalizes it.
Selected glyph mutation, scroll or committed resize clears selection. Unrelated
output and same-glyph colour changes preserve it. Focus/layer/stream reset
cancels active dragging, while unchanged completed selection can survive hiding.
The presenter highlights retained glyphs without writing into text or raster
backing, then composes the block caret and system pointer for display/capture.

This applies to Caelum's kernel-log terminal too. An acquired mux controller
instead owns its spatial queue and selects from its own pane cells/history.
Ordinary local TTY wheel input does not scroll: there is no kernel scrollback.
No selection publishes clipboard data or provides a Copy/Paste command. The
later clipboard contract must define an owned-text snapshot and its encoding;
arbitrary 8-bit glyphs are not advertised as UTF-8 text.

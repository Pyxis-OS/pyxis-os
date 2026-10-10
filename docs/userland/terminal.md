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

Editing supports insertion, Backspace, Delete, Left/Right, Home/End (also
Ctrl+A/Ctrl+E) and Enter.
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
recall submitted lines with Up/Down and set `recorded` when they appended the
line; other entry points ignore them. `term_history_add` applies the same
recording rules, for example to lines loaded from a file.
History-enabled readers also support [Ctrl+R reverse search](shell.md#commands-and-quoting).
Search uses a separate bounded query, keeps the original line/cursor for
Ctrl+G/Ctrl+C cancellation, and redraws its prompt on resize. Enter submits the
shown entry; Escape/navigation accepts it for editing. Entries that exceed the
caller buffer are skipped rather than truncated by search. Failed searches
retain the shown line and mark the visible text red. Quiet readers and readers
without history ignore Ctrl+R. Allocation failure leaves ordinary editing usable.
Non-ASCII input is ignored, and so is Tab unless the reader has [completion](#tab-completion); Unicode widths remain later work. `CALL_INPUT_LOST` abandons the
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

## Tab completion

`term_read_line_completing(term, prompt, history, completion, marked, buffer, capacity)`
is the history or marked reader with an optional `struct term_completion`. Tab
calls its `candidates` function with the text left of the cursor; the function
returns heap strings (ascending, unique, printable ASCII, spaces allowed) and the
offset where the word to replace starts. Each string is the full replacement for
that word, including any quoting the caller needs. One string replaces the word
and is followed by the result's `finish` text, or a space when `finish` is NULL; a
space that ends `finish` is dropped when one already follows the cursor. Several
extend the word to their common prefix if that is longer; otherwise they are
listed in columns below the finished line and the prompt and line are drawn
again. A replacement that does not fit the buffer changes nothing and sets
`limit_reached`. The editor frees the strings. Without a `completion`, in the quiet
reader and in the older entry points, Tab is ignored as before. The
[shell](shell.md#commands-and-quoting) completes command names and paths.

## TTY output controls

The TTY keeps its parser state across writes. The
[multiplexer's](multiplexer.md) pane terminal implements the same set, and the
packaged [session configuration](session-configuration.md) names it
`TERM=pyxis`, which promises exactly this table:

| Bytes | Effect |
| --- | --- |
| LF / CR / BS | New row at column zero / column zero / one column left without erasing |
| HT (`\t`) | Move to the next tab stop (eight columns by default), clamped to the last column, without erasing |
| `CSI n A/B/C/D` | Move up/down/right/left, clamped to screen edges; up/down stop at the scroll region's margins when the cursor is inside it |
| `CSI n G` | Set column (one-based) |
| `CSI row;column H` | Set position (one-based) |
| `CSI 0/1/2 K` | Erase line after/before/around cursor, including its cell |
| `CSI 0/1/2 J` | Erase screen after/before/around cursor, including its cell |
| `CSI top;bottom r` | Set the scroll region (DECSTBM), one-based and inclusive, and move to the top-left cell; missing or zero values select the screen edges |
| `CSI n L` / `CSI n M` | Insert/delete lines at the cursor row within the scroll region, moving to column one; ignored outside it |
| `ESC M` | Reverse index: move up, scrolling the region down at its top margin |
| `ESC 7` / `ESC 8`, `CSI s` / `CSI u` | Save/restore the cursor position, colours, attributes and pending wrap |
| `CSI ? 1049 h/l` | Enter/leave the alternate screen |
| `CSI ? 25 h/l` | Show/hide the nonblinking block cursor |
| `CSI 0 m` (or `CSI m`) | Reset colours and attributes |
| `CSI 1/22 m` | Bold on/off |
| `CSI 3/23 m` | Italic on/off |
| `CSI 4/24 m` | Underline on/off |
| `CSI 7/27 m` | Reverse video on/off |
| `CSI 30–37/90–97 m`, `CSI 40–47/100–107 m` | Palette foreground/background, indices 0–15 |
| `CSI 39/49 m` | Separate terminal default foreground/background |
| `CSI 38;5;n m`, `CSI 48;5;n m` | Indexed foreground/background, `n` in 0–255 |
| `CSI 38;2;r;g;b m`, `CSI 48;2;r;g;b m` | RGB foreground/background, each component in 0–255 |
| `ESC ( x`, `ESC ) x`, `ESC * x`, `ESC + x` | Character set designation: consumed and ignored |

`CSI` is Escape followed by `[`. Movement defaults to one; position defaults to
row/column one. Erasing does not move the cursor. SGR parameters can be combined
in one CSI. Palette entries 0–15 come from each TTY's active scheme (Aardvark in
every space today); 16–231 use the xterm 6×6×6 cube, with component levels
0, 95, 135, 175, 215 and 255, and 232–255 use greys 8 through 238 in steps of 10.
Bold leaves the colour index unchanged. Mux retains and emits indices or RGB;
the outer TTY resolves indices. The interactive host client resolves them with
the same shared Aardvark definitions. Scheme configuration remains deferred.

The framebuffer's 8×16 bitmap font renders underline as one foreground pixel
row at the bottom, bold as a clipped one-pixel right overstrike, and italic as
a clipped right shear of two pixels in the upper third, one in the middle and
zero in the lower third. These are synthetic styles; underline can intersect
descenders. Cells stay 8×16. The remote client emits the styles for its host
font to render.

Unsupported controls are ignored, including
`CSI ? 47/1047/1048 h/l`, origin mode, character insert/delete and other escape
sequences. At most 16 parameters of up to 65535 are accepted. SGR applies
transactionally: a truncated colour group, missing colour operand, out-of-range
component, unknown colour mode, numeric overflow or excess parameters discards
the whole CSI without changing attributes or colours. Colour operands never
become separate attributes. Only semicolon RGB/indexed forms are supported;
colon forms discard the whole CSI too. Malformed CSI is consumed through its
final byte. A new Escape
starts a fresh sequence. This is a focused subset, not a claim of full ANSI/VT
compatibility, and it is ASCII only.

**Scroll region.** LF on the region's bottom margin scrolls only the region; LF
on the screen's last row below the region does not scroll. Wrapping follows LF.
Erased, scrolled and inserted blank cells take the current colours with all
attributes off. An
invalid region (top not above bottom, or past the screen) is ignored. Switching
screens and resizing reset the region to the whole screen.

**Alternate screen.** libterm's `term_alternate_screen(term, enabled)` writes
`CSI ? 1049 h/l`. vi, less, Kilo and Links run on the alternate screen, so
quitting returns the shell's screen as it was. Entering saves the cursor as `ESC 7` does and shows a
cleared second screen; leaving restores the first screen's cells, colors and
saved cursor. Each screen has its own saved cursor. Entering while already on
the alternate screen, or leaving while not, does nothing. Selection is cleared
on either switch. Both screens keep their cells across resize, cropped like
the visible one. Each stored cell uses 12 bytes for its glyph, attributes and
tagged index/default/RGB colours. The kernel preallocates both screens with the
space: 24 bytes per grid position, or 374,400 bytes for 240×65 cells at
1920×1080. Selection staging retains glyph attributes; the block caret changes
raster colours while keeping the styled shape. In a
multiplexer pane, the alternate screen has no history; see
[multiplexer history](multiplexer.md#full-screen-programs-and-history).

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
raster. The [system pointer](../interfaces/pointer.md#terminal-control-and-selection)
routes a left press to a pending anchor. It clears the previous selection, but
selects nothing until the pointer enters a different cell while left is held.
Motion within the starting cell does not count; release without such movement
leaves no selection. A one-cell displacement selects the two inclusive endpoint
cells; longer drags span physical rows, and release finalizes the range.
Only complete cells can start selection; an anchored endpoint clamps to the
grid edges. A new selection replaces the old one. Selected glyph mutation, scroll
or committed resize clears selection. Unrelated output and same-glyph colour
changes preserve it. Focus/layer/stream reset
cancels active dragging, while unchanged completed selection can survive hiding.
The presenter highlights retained glyphs without writing into text or raster
backing, then composes the block caret and system pointer for display/capture.
Glyph allocation and cropped/no-reflow resize are BSP-owned; publication occurs
under the output lock.

This applies to Caelum's kernel-log terminal too. An acquired mux controller
instead owns its spatial queue and selects from its own pane cells/history.
Ordinary local TTY wheel input does not scroll: there is no kernel scrollback.
Selection alone does not copy. [Terminal clipboard](../interfaces/clipboard.md)
Copy freezes a completed selection as owned printable-ASCII text, LF-joins
physical rows and trims trailing spaces. Selected non-ASCII glyphs refuse the
entire operation. Ctrl+Shift+C/V uses the space-local layer; Super+Shift+C/V uses
the shared layer. Caelum's log supports Copy without an application paste target.

## Clipboard paste in line readers

Stock libterm line readers opt into an exclusive process-owned receiver for their
editing lifetime, using matching input READ/output WRITE. They release it on
every return before a child/input handoff. Other/raw readers receive no paste
fallback; an unavailable registration leaves ordinary line editing intact.
Native reads atomically declare the key decoder's boundary, and native READABLE
readiness wakes only the registered process for BEGIN/DATA/END/CANCEL records.
These records do not enter the ordinary FIFO or key/history parser.

LF/Tab become spaces at the editing cursor. Capacity exhaustion keeps the
existing `limit_reached` cue while consuming framing. Completion/cancellation
must be consumed and acknowledged before ordinary typing resumes, and submission
requires a fresh physical Enter. Cancellation retains an editable inserted prefix;
physical Ctrl+C still follows its normal line-cancel/interrupt path. Result fields
report inserted bytes, cancellation and native status, with a notice in nonquiet
editors. Pending older input refuses Paste rather than being flushed or deferred.
See the [receiver contract](../interfaces/clipboard.md#safe-receiver) and
[delivery limits](../technical-debt.md#initial-clipboard-delivery-limits).

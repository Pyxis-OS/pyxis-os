# Terminal input and editing

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
Ctrl+D on an empty line returns `TERM_LINE_EOF`; on a nonempty line it is
ignored. This is an editor result, not closure of the console input object.
The shell exits successfully on EOF; Lua also accepts it at a continuation
prompt, discarding the unfinished chunk and exiting.

Editing supports insertion, Backspace, Delete, Left/Right, Home/End and Enter.
A steady underline cursor marks the editing position, including the blank cell
after the last character. The prompt and line wrap with the TTY, including when
older output scrolls upward. The helper redraws the line and uses relative row
movement to reach the editing position. It does not need the screen's absolute
cursor position or a kernel-side line buffer.

The maximum line length is the smaller of `capacity - 1` and
`columns * rows - prompt_length - 1`. One cell holds the end cursor. Delayed
wrapping allows the last screen cell to be used without scrolling.
This keeps the whole editable span reachable even after scrolling. A prompt
that leaves no room for those cells fails with `CALL_LIMIT` before output.
At either limit, further insertion is rejected, the cursor cell turns red, and the
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
The line editor ignores Escape, Up/Down and Page Up/Down.
Tab and non-ASCII input are ignored; Unicode widths, history and a viewport for
lines larger than the screen remain later work. `CALL_INPUT_LOST` abandons the
line and returns a distinct result, so the caller can explain the loss and retry.
On an output failure, the screen/cursor may be partially updated and must not be
assumed to match the discarded line.

`term_read_key_timeout` bounds only the wait for the initial byte. Zero polls;
no initial byte returns `CALL_TIMED_OUT` with `TERM_KEY_UNKNOWN`. Once a byte
arrives, it uses the same Escape decoding rules as the blocking helper, even
when decoding extends past the caller's timeout. It never discards a partial
key merely because that initial timeout elapsed.

Kilo uses this helper with a remaining monotonic deadline to expire transient
status messages while idle. After clearing a message it blocks indefinitely
again; active search prompts do not expire. No periodic redraw or polling is
needed.

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
| `CSI ? 25 h/l` | Show/hide the nonblinking underline cursor |
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
its position/visibility under the output lock and draws it onto the display after
blitting. The cursor never modifies the space framebuffer, so moving, hiding or
switching spaces leaves no saved-pixel restoration work. Framebuffer presentation
still permits tearing; no frame transaction or resize handling is added.

The shell uses the helper for command input, then stops reading while a child
runs. It retries after cancellation or input loss and rejects submitted lines
that reached the editor limit. The native entry points and result contract live
beside the declarations in `term.h`; no global terminal, stdio stream or shell history
is hidden inside libterm.

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

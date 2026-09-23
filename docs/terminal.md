# Terminal input and editing

[`libterm`](https://git.internal/chronium/pyxis-userland/src/branch/main/include/term.h) builds terminal behavior on libpyxis's
console calls. A `struct terminal` borrows explicit input and output handles;
it neither allocates nor closes them. Input/output, partial writes, write-all,
size queries and control helpers preserve native call statuses. Console output
wrappers now preserve those statuses too, rather than reducing failures to -1.

The line helper takes a printable ASCII prompt and a separate caller-owned
buffer. Capacity includes the terminating NUL. Successful input excludes the
newline; cancellation, input loss and errors clear the buffer and return zero
length. Ctrl+C cancels this line locally, moves to a fresh line and returns
`TERM_LINE_CANCELLED`; it is not a signal and does not terminate the process.
There is no terminal EOF operation, and Ctrl+D has no special meaning.

Editing supports insertion, Backspace, Delete, Left/Right, Home/End and Enter.
The character at the editing position is highlighted; at the end, a blank cell
is highlighted instead. The prompt and line wrap with the TTY, including when
older output scrolls upward. The helper redraws the line and uses relative row
movement to reach the editing position. It does not need the screen's absolute
cursor position or a kernel-side line buffer.

The maximum line length is the smaller of `capacity - 1` and
`columns * rows - prompt_length - 2`. One cell holds the end cursor; the final
screen cell is left unused because the TTY wraps immediately after writing it.
This keeps the whole editable span reachable even after scrolling. A prompt
that leaves no room for those cells fails with `CALL_LIMIT` before output.
At either limit, further insertion is rejected, the cursor turns red, and the
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

Input sequences are decoded incrementally, one byte per read. There is no
read-ahead buffer to steal a future foreground child's input. Standalone Escape
has no action; an incomplete escape sequence waits for more input. Up/Down and
Page Up/Down are decoded but ignored. Unsupported sequences are discarded.
Tab and non-ASCII input are ignored; Unicode widths, history and a viewport for
lines larger than the screen remain later work. `CALL_INPUT_LOST` abandons the
line and returns a distinct result, so the caller can explain the loss and retry.
On an output failure, the screen/cursor may be partially updated and must not be
assumed to match the discarded line.

## TTY output controls

The TTY keeps its parser state across writes. The supported subset is:

| Bytes | Effect |
| --- | --- |
| LF / CR / BS | New row at column zero / column zero / one column left without erasing |
| `CSI n A/B/C/D` | Move up/down/right/left, clamped to screen edges |
| `CSI n G` | Set column (one-based) |
| `CSI row;column H` | Set position (one-based) |
| `CSI 0/1/2 K` | Erase line after/before/around cursor, including its cell |
| `CSI 0/1/2 J` | Erase screen after/before/around cursor, including its cell |
| `CSI ... m` | Reset, reverse video, palette/default foreground and background |

`CSI` is Escape followed by `[`. Movement defaults to one; position defaults to
row/column one. Erasing does not move the cursor. Style parameters are 0 (reset),
7/27 (reverse on/off), 30–37/90–97 (foreground), 40–47/100–107 (background), and
39/49 (separate terminal defaults). Unsupported controls are ignored. At most
four parameters of up to 65535 are accepted; malformed or oversized CSI commands
are discarded through their final byte. A new Escape starts a fresh sequence.
This is a focused subset, not a claim of full ANSI/VT compatibility.

The shell uses the helper for command input, then stops reading while a child
runs. It retries after cancellation or input loss and rejects submitted lines
that reached the editor limit. The native entry points and result contract live
beside the declarations in `term.h`; no global terminal, stdio stream or shell history
is hidden inside libterm.

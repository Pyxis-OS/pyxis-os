# Saved shell history

Proposal, 2026-10-09; not accepted. It implements the accepted follow-up in
[initial terminal editor](../technical-debt.md#initial-terminal-editor): shell
history kept across sessions and reboots, in `home://`. Today each interactive
shell keeps its last 100 lines in memory
([shell](../userland/shell.md#commands-and-quoting)).

## What constrains it

- **Shared homes.** `home://` is shared by every space that names it
  ([system layout](../userland/system-layout.md#roots)):
  - **live boots:** `development` and `remote` share one RAM home, and
    `readonly` gets it read-only;
  - **installed systems:** they have one space, `pyxis`.
- **Several shells per space.** Each space can run a local shell, mux panes
  and up to four remote sessions at once.
- **Shells don't know their space.** No shell can read its space's name. Mux
  panes and remote shells don't even receive the `space` grant, and init
  scripts can't forward a name, because the shell has no expansion.
- **No atomic append.** The file protocol has no append operation: libc's
  append is a size query then a write, so two appending shells can overwrite
  each other.
- **Atomic replace exists.** Native rename with REPLACE is atomic on RAM and
  native volumes. On native volumes it reaches a durable commit before
  returning ([filesystem mutations](../interfaces/filesystem-mutations.md)).
- **Remote shells are killed, not exited.** A remote shell ends when its
  session is terminated, and a mux pane's shell when the pane closes. A shell
  rarely exits cleanly, so saving only on exit would lose most history.

## Proposed behaviour

- **Which shells.** Interactive shells that keep history today: local, mux
  panes and remote sessions with a prompt. `--no-echo` (machine) shells and
  script mode neither load nor save.
- **Loading.** At start, a shell loads the newest 100 entries of the file
  into its in-memory history. It doesn't see lines other shells save later; a
  new shell, including a `session` successor, picks them up.
- **Saving.** Each line the shell records in memory is saved at once. The
  recording rules are unchanged: no empty or all-space lines, and no repeat of
  the shell's own previous line.
- **Format.**
  - One entry per line, ending in LF.
  - Each entry is printable ASCII (0x20–0x7E) and at most 1023 bytes, which is
    what the editor already accepts.
  - Loading silently skips anything else: control bytes, over-long lines, or a
    final line without LF.
- **Bounds.** The file keeps the newest entries up to 1000 lines and 64 KiB.
  Each save trims the oldest whole lines past either limit.
- **No writable home.** History stays in memory with no message. This covers
  a space without `home://` (including a rescue boot whose home didn't
  mount), a read-only home such as `readonly`'s, and a home whose mount failed.
  - **The first save failure** (for example a full volume) prints one line.
  - **After that,** that shell stops saving.
  - **Loading problems** are always silent.
- **Ctrl+R,** when it comes, searches the in-memory history the file
  populated.

## Owner decisions

1. **Where.**
   - **Default:** one file per home, `home://.history`, shared by every shell
     of every space that can write that home.
     - **Installed system:** that means the `pyxis` space.
     - **Live boot:** `development` and `remote` share it, which suits one
       person using both.
     - **No new plumbing.**
   - **Alternative:** one file per space, `home://.history/SPACE`. Shells
     would need their space name, either from a new read-only name query on
     the `space` grant or from a `--history NAME` option. Either way, session,
     mux and the remote terminal must pass it to the shells they launch.
2. **Concurrency and durability.**
   - **Default:** merge and replace on every saved line. The shell:
     1. reads the current file;
     2. adds its line at the end and trims;
     3. writes the result to a new temporary `.history.XXXXXX` and syncs it;
     4. renames it over `.history`.

     **Consequences:**
     - Lines from concurrent shells interleave in the order they were saved.
     - A line is lost only when two shells save within the same few
       milliseconds: the later rename wins.
     - A crash or kill loses at most the line being saved. It may leave one
       stray temporary file, as Quake and vi already can.
     - **Cost:** up to 64 KiB read and written, plus two pool commits, per
       command.
   - **Save only on exit:** cheapest, but loses everything from killed shells,
     which covers every remote session and closed mux pane.
   - **Append per line:** cheapest per command, but correct only with an
     atomic append in the file protocol, a kernel change. A lock file instead
     would go stale when its holder is killed.
3. **Privacy.**
   - **Default:** a line starting with a space is kept in memory, so Up still
     recalls it, but never written to the file, like bash's `ignorespace`.
   - **Alternative:** save everything.
   - **Alternative:** keep such lines out of memory history too.

## Validation plan

In QEMU, on a live boot and on an installed image:
- two shells and a remote session saving alternately, checking the file's
  order;
- killing a remote session mid-use, checking its saved lines;
- a reboot on the installed image, checking history survives;
- the read-only space and rescue boot staying silent;
- a damaged file loading what it can.

The native check is the owner's: whether saving makes the prompt feel slower
on the ThinkPad.

## Out of scope

Ctrl+R search, timestamps, per-directory history, sharing history live between
running shells, and history for programs other than the shell.

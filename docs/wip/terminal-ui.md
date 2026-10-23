# Raw terminals and curses applications

Design accepted and implementation assigned (chronium, 2026-10-23).
Release target: 0.0.9 (chronium, 2026-10-23).
Provide reusable raw terminal access, then one curses library and one application.
The installer remains a consumer of that library, not the prerequisite for it.

## What exists

Source inspection at Pyxis `59e8d64d`, userland `96d2430b`, ports `22c4afb6`:

- [CONSOLE READ](../../include/abi/console.h) already returns available bytes
  without line editing or echo. Canonical editing and echo belong to libterm's
  line reader, not a kernel line discipline. Escape sequences may span reads;
  timeout, EOF, input loss and hangup are distinct outcomes.
- [PASSTHROUGH](../userland/terminal.md#interrupt-arming-and-passthrough) is a
  counted grant acquired with input READ. While held, Ctrl+C is byte 3 instead
  of a foreground interrupt. Last-grant closure, including process teardown,
  removes that request. It does not change output or establish exclusive input
  ownership. The shell already stops reading during foreground execution.
- SIZE returns columns, rows and a geometry generation. WAIT_RESIZED observes
  a change without SIGWINCH. Local consoles and mux panes implement it;
  [plain Remote](../userland/remote-terminal.md#remote-multiplexer) currently
  fixes geometry at connection setup, whereas remote mux negotiates changes.
- [Neovim](../userland/neovim.md) already uses native RAW input:
  [libuv](../../ports/libuv/pyxis/stream.c) retains a PASSTHROUGH grant,
  observes geometry and releases its grant on normal/close paths. Its
  [TUI patch](../../ports/neovim/patches/0003-pyxis-tui.patch) embeds a conservative
  `pyxis` terminal description, avoiding unsupported probes. This is not an
  installed terminfo database or a general libc raw-terminal interface.
- libc has `isatty`, byte reads/writes and
  [descriptor binding](../../userspace/libc/include/pyxis/descriptor.h), but no
  termios API. An fd binding is borrowed and expires on close/replacement;
  the new layer must hold owned state across blocking calls. Go owns native
  handles independently of libc's integer descriptor table.

## Accepted terminal contract

**Input.** Expose a small native terminal API, shared by libterm and language
adapters: terminal detection, scoped raw acquisition/restoration, geometry and
readiness. Raw acquisition retains PASSTHROUGH for the actual supplied input;
reads stay byte-oriented and unechoed. There is no new cooked mode in the kernel.
Applications choose their own line editor. Ctrl+C/Ctrl+D/Escape are data in raw
mode; they do not become EOF or a signal. System keys, mux prefixes and Remote's
host disconnect key retain their existing ownership.

Use the terminal selected by the caller, not an ambient `input` fallback when
stdin is a pipe/file. Detection, geometry and raw access check the relevant
rights independently. The C adapter binds a saved state to its held open object,
not a reusable fd number; Go binds to its actual native file handle. Native
release closes only its owned acquisition, preserving others. Runtime-owned
raw state is private, not exported through the environment or child launch.
Closing/replacing an fd cannot apply an old Restore to its replacement: it must
return an error without changing the replacement. Normal exit and forced process
retirement release owned acquisitions. This does not revoke a grant an
application deliberately delegated elsewhere.

**Adapters.** C gets a native libc/libterm interface. Go's
[`x/term` functions](https://pkg.go.dev/golang.org/x/term) (`IsTerminal`,
`GetState`, `MakeRaw`, `Restore`, `GetSize`) use the same native semantics,
without libc or Unix ioctls. Port its Pyxis platform implementation at a pinned
source revision; merely adding the GOOS to Unix build tags is wrong. Its
[unsupported platform implementation](https://go.dev/src/cmd/vendor/golang.org/x/term/term_unsupported.go)
currently returns errors. For the state-setting adapter, use one private mode
owner per supplied terminal: GetState snapshots whether that owner holds raw;
MakeRaw returns the preceding snapshot and ensures its hold exists; Restore
sets that owner's hold to the snapshot, never another owner's. Snapshots do not
acquire raw. Repeated Restore is idempotent; nested saves restore normally in
reverse order, and out-of-order restoration applies the selected snapshot
(last call wins), rather than guessing a stack unwind. This is native request
state, not a global Unix tty-mode query. Resize subscription is a separate native
event API; `GetSize` itself does not subscribe. Libuv should consume the shared contract
rather than grow a second mode owner. Unsupported termios controls, baud rates,
signals, job control and arbitrary tty devices are outside this profile.
A thin libc `tcgetattr`/`tcsetattr`/`cfmakeraw` layer may map raw on/off to this
native scope when a first port actually needs it (chronium, 2026-10-23). This is
libc compatibility, not kernel tty state, and unsupported settings must refuse.

**Keys and waits.** Preserve short reads and decoder state across resize.
Use the documented [Pyxis key/output repertoire](../userland/terminal.md), not
an `xterm-256color` claim. Unknown sequences remain unsupported, never guessed
function keys. Distinguish polling from Escape ambiguity: today's
`term_read_key_timeout(..., 0)` can still wait up to 100 ms between sequence
bytes. A curses readiness hook must instead keep partial decoding and its
clock deadline across nonblocking checks. Input loss cancels partial decoding
and requires application rebaseline; it must not look like a key or clean EOF.

**Geometry.** Re-query SIZE after RESIZED, use the new generation in the next
wait, and redraw for the current size; notifications may coalesce. Reuse the
existing remote-mux resize negotiation for plain interactive Remote so host
window changes reach its application terminal too. Machine mode retains its
explicit configured geometry. No application gains attachment RESIZE authority,
no polling loop or synthetic SIGWINCH is needed.

**Restoration.** Input restoration is last-grant cleanup; presentation is a
separate responsibility. Ordinary shell foreground commands currently wait for
direct stages; only selected capture/output/script paths own a cleanup subgroup
([source](../../userspace/shell/launch.c)). Therefore this proposal must establish
an owned foreground subgroup for commands receiving interactive terminal grants,
using the existing native launch/group core, before claiming descendant-safe
restoration. It is not an existing guarantee. Preserve declared child grants;
no broker or extra launcher authority. Include matched launch-cost checks.
**Selective normalization (accepted, chronium, 2026-10-23).** Keep owned groups
for every interactive command; the measured nested group-only cost of about
0.2 ms/command is accepted. Normalize only if the completed subtree admitted
PASSTHROUGH authority at any point, or ended abnormally (fault, forced stop,
Ctrl+C escalation or hangup). Current-hold counts are insufficient after raw
release, so completion retains sticky subtree observations. Captures and
machine/no-echo roots remain excluded from automatic presentation cleanup.
A normal non-raw program leaving SGR colours or a hidden cursor is intentionally
not cleaned up, as with ordinary Linux shells. `reset` supplies the same
normalization on demand; `clear` erases the screen and supported scrollback,
then homes the cursor. Both use the named terminal output grant, reject missing
or non-terminal output without writing controls, and take no arguments.

Normal raw full-screen exit leaves the alternate screen, resets the scroll
region/styles and shows the cursor. `atexit` cannot handle a
fault or forced stop. The foreground supervisor must normalize its owned
terminal before the next prompt after the terminal-using foreground scope has
actually retired, including descendants; the session/mux/Remote owner applies
its existing cleanup boundary on disconnect or pane closure. Do not reset a
live child's terminal or wait only for the launcher process while its children
still write. Use explicitly held terminal output, never a redirected file or
pipe. A normalization failure is reported, not treated as a restored screen.
No app-supplied cleanup script, extra launcher right or emergency kernel writer.
This restores terminal usability, not arbitrary screen contents or lost edits.

**Cancellation (accepted, chronium, 2026-10-23).** Ordinary direct stages keep
the cooperative first-press request. Remaining descendants are stopped after
those stages finish; a second press force-stops the whole subtree. PASSTHROUGH
stays byte 3. Existing script, capture and output-helper scopes retain their
forceful cleanup policy; acquiring an ordinary foreground cleanup scope does
not remove the cooperative first request.

## Curses choice (decided, chronium, 2026-10-23)

Development `.pc` metadata uses `sdk://include` and `sdk://lib`; hosted Clang
gets no `pkg://` authority. Preserve libraries/other compiler flags while
omitting redundant default SDK search flags (chronium, 2026-10-23).

Use **ncursesw with a terminfo entry for `TERM=pyxis`**, reusing the terminal
profile from [Neovim task 3](../../ports/neovim/patches/0003-pyxis-tui.patch).
Hexcurse remains the first application unless the actual ncursesw probe gives
a concrete reason to select another listed consumer; report that reason first.
The installer consumes this library later.

The entry describes only supported controls, colours and keys. It must not
advertise unsupported probes, character insert/delete, mouse protocols or full
xterm behaviour. Ncurses needs native input/mode, descriptor and resize
integration as well as terminfo; its default Unix termios/SIGWINCH path is not
the Pyxis implementation. Use native events for KEY_RESIZE. Preserve real
input loss, closure and output failures rather than returning successful updates.
The initial 16-colour/default-colour profile and single-cell repertoire follow
Neovim's definition. A wide build does not add unsupported glyphs or combining
cells to the renderer.

Ncurses has a permissive notice-preserving licence. Audit the selected pin and
arrange its source mirror/cache before building. References:
[initialization](https://invisible-island.net/ncurses/man/curs_initscr.3x.html),
[resize](https://invisible-island.net/ncurses/man/resizeterm.3x.html) and
[licence](https://invisible-island.net/ncurses/ncurses.faq.html#howcanitbedistributed).
PDCurses was considered for its native platform hooks, but is not selected;
port neither library's successful stubs or a second curses implementation.

## Delivery gates (assigned in order, chronium, 2026-10-23)

1. [x] **Native raw API.** Scoped raw acquisition/restoration,
   terminal detection and SIZE/RESIZED over the existing console contract,
   with an ordinary C caller. Check close/replacement, repeated scopes, pipes,
   denied rights and Ctrl+C bytes qualified in QEMU on one/four CPUs, with
   actual mux resize and GDB lifetime inspection. Input loss/hangup propagation
   is source-inspected; lifecycle coverage continues in gate 2. Existing IPC and
   launch checks are recorded in the [experiment](../development/experiments/terminal-raw.md).
   No new kernel line discipline.
   **After this task chronium can:** run a C byte-input program locally and in
   a pane without echo or waiting for Enter, then restore ordinary interaction.
2. [x] **Adapters and lifecycle.** Go x/term and libuv integration,
   foreground subgroup ownership and crash/termination presentation cleanup,
   and plain interactive Remote resize.
   Explicitly qualify normal exit, fault, forced stop, nested children, pane
   close and Remote disconnect before claiming crash restoration.
   [QEMU qualification](../development/experiments/terminal-lifecycle.md) covers
   one/four CPUs, cooperative/forced completion, raw input, plain/mux resize,
   live-descendant retirement, pane close and Remote disconnect. Go provides
   size and nonblocking observation; blocking resize integration needs a real
   Go event-loop consumer. Native qualification remains gate 5.
   **After this task chronium can:** use raw Go programs and editors through
   local/mux/Remote terminals with geometry events and a usable terminal afterward.
3. [ ] **ncursesw and terminfo.** Pin/probe it, implement native terminal-driver
   integration and required common libc gaps, then build its window/input
   demonstration with the `pyxis` terminfo entry.
   Qualify resize while an Escape is partial, output backpressure/failure,
   small dimensions, default colours, supported characters and endwin/re-entry.
   **After this task chronium can:** run a real curses window demonstration in
   a local space, mux pane or Remote session.
4. [ ] **One application.** First consumer: hexcurse, first in
   chronium's list below. Confirm its actual API/build gaps at a pin, retain
   normal file authority and honest save failures, package outside the archive.
   This does not assign the remaining candidates or the installer's page UI.
   **After this task chronium can:** inspect/edit a delegated file in hexcurse.
5. [ ] **Qualification and closure.** Repeat matched input/redraw/wake checks;
   measure stripped library/tool size and memory rather than estimate budgets.
   QEMU local/mux/plain Remote/remote mux, then a short coordinated native check.
   Fold implemented contracts into terminal references when complete.
   **After this task chronium can:** use the first qualified curses application
   without mode, resize or crash-cleanup differences between terminal paths.

## Candidates (chronium's order)

Quick upstream licence/dependency look only (2026-10-23), not port feasibility
probes or binary-size measurements. Footprint labels are qualitative source/
dependency weight. These are candidates, not assignments; no further editors
are excluded by this list. Existing Links already provides a custom terminal
browser ([reference](../userland/links.md)); it is not a new curses port.

### Practical applications

| Candidate | Licence | Broad dependencies / Pyxis prerequisites; qualitative weight |
| --- | --- | --- |
| [hexcurse](https://github.com/LonnyGomes/hexcurse) | GPL-2.0-or-later ([source](https://github.com/LonnyGomes/hexcurse/blob/master/src/hexcurse.c)) | C, curses, file I/O, resize/signals; raw/curses first; small. |
| [ncdu 1.x](https://dev.yorhel.nl/ncdu) | MIT | C LTS, curses, filesystem identity/allocation metadata, optional shell launch; raw/curses and honest apparent-size scanning first; small. |
| [nnn](https://github.com/jarun/nnn) | BSD-2-Clause | C, curses, pthreads; fork/exec/signals and external openers/utilities; native jobs and thread-profile audit; medium. |
| [calcurse](https://github.com/lfos/calcurse) | BSD-2-Clause | C, curses, pthread cancellation/autosave, timers, external editors/hooks; native jobs and synchronization-profile adaptation; medium. |
| [vis](https://github.com/martanne/vis) | ISC + bundled notices | C/POSIX, curses frontend, optional Lua/LPeg; mapping, jobs and signals need native adaptation; medium. |
| [tig](https://github.com/jonas/tig/blob/master/INSTALL.adoc) | GPL-2.0-or-later | C, ncurses, Git subprocesses/pipes; usable Git CLI plus native command execution before full browser; medium. |
| [htop](https://github.com/htop-dev/htop) | GPL-2.0-or-later | C, ncurses, OS task/CPU/memory data and process controls; needs a native process-info backend, no pretend procfs; medium. |
| [Lynx](https://www.invisible-island.net/lynx/lynx.html) | GPL-2.0-or-later + bundled notices | C, curses/PDCurses, network/DNS/TLS, waiting and external helpers; raw/curses then provider/network/job integration; large. |

### Native (first-party)

| Candidate | Licence | Broad dependencies / Pyxis prerequisites; qualitative weight |
| --- | --- | --- |
| [Capability and object explorer](later-os-directions.md#developer-tools) | Pyxis MPL-2.0 | Native terminal/curses and held-capability descriptions; richer snapshots require a separate authority/API proposal, not arbitrary kernel-object access; scope-dependent. |

### Audio

| Candidate | Licence | Broad dependencies / Pyxis prerequisites; qualitative weight |
| --- | --- | --- |
| [cmus](https://github.com/cmus/cmus) | GPL-2.0-or-later | C, ncurses, codecs, worker threads, select/signals; needs a Pyxis PCM output backend and native waits; medium core, optional codecs add weight. |

### Games and entertainment

| Candidate | Licence | Broad dependencies / Pyxis prerequisites; qualitative weight |
| --- | --- | --- |
| [Angband](https://angband.readthedocs.io/en/stable/hacking/compiling.html) | GPL-2.0 or Angband licence | C, curses GCU frontend, game data/saves; termios/suspend adaptation, raw/curses; medium plus data. |
| [vitetris](https://github.com/vicgeralds/vitetris/blob/master/config.mk) | BSD-2-Clause | C, optional curses backend, timers/input; optional network/joystick/Xlib kept separate; small. |
| [cmatrix](https://github.com/abishekvashok/cmatrix) | GPL-3.0-or-later | C, curses, timers/resize/signals; Linux font-loading command is separate from terminal animation; small. |
| [nudoku](https://github.com/jubalh/nudoku) | GPL-3.0-or-later | C, ncurses, gettext build infrastructure, optional Cairo export; raw/curses, RNG/save access; small. |
| [moon-buggy](https://github.com/seehuhn/moon-buggy) | GPL-3.0 (current COPYING) | C, curses, select/signals, high-score/config files; native input/timers/storage; small. |

These licence labels are a quick look, not the eventual pinned package's full
notice audit. No candidate was cross-compiled or timed for this proposal.

## Decisions (accepted, chronium, 2026-10-23)

1. **Raw contract:** byte CONSOLE reads and owned PASSTHROUGH; native C/Go
   adapters bound to the supplied terminal, SIZE/RESIZED including Remote.
   No kernel line discipline or signal emulation. A thin libc termios mapping
   may be added for the first port that needs it, with no fake unsupported modes.
2. **Restoration:** owned subgroups for interactive foreground commands, then
   input-grant cleanup and selective normalization before the next prompt.
   Normalize raw/abnormal scopes; `reset` covers ordinary presentation leftovers
   and `clear` erases screen/history. Wait for actual subtree retirement; never restore over live descendants.
   Ordinary direct stages retain cooperative first Ctrl+C; stop remaining
   descendants after they finish, with a second press forcing the whole subtree.
   PASSTHROUGH remains byte 3 (chronium, 2026-10-23).
3. **Library and consumer:** ncursesw plus `TERM=pyxis`, reusing Neovim's terminal
   profile; hexcurse first, installer pages later. Implement in that order.

# Neovim task 6 groundwork

**Owner accepted the three groundwork defaults on 2026-10-10. Working path and environment
are implemented in slice 1; proved realpath and fdopen/dup are implemented in
slice 2; calendar, encoding and the numeric/string closure are implemented for
slice 3. The editor recipe and QEMU qualification are delivered in slice 4; owner native
qualification follows as slice 5.**
This splits [task 6](neovim-libuv.md#tasks); task 5's Lua/luv delivery remains a
prerequisite. The shared libc work also serves [hosted Clang](hosted-clang.md)
and [Git](git-on-pyxis.md). Existing first-slice exclusions remain in force.

## Delivered slices

The [process-state reference](../userland/process-state.md) describes shared libc
`chdir`/`getcwd`, copied mutable environment and explicit child snapshots, with
shell and port consumers updated together. [Qualification](../development/experiments/process-state/README.md)
records the fresh-main baseline, matched launch costs and QEMU child-snapshot run.
Slice 2 delivers [proved realpath](../userland/paths.md#proved-realpath) and
[fdopen/shared descriptor objects](../userland/stdio.md#opening-and-ownership),
including the libuv realpath adapter. `freopen` remains separate; slice 4 adds
`dup2`, `setbuf` and explicit output buffering without changing the default
unbuffered streams.
Slice 3 delivers [`mktime`](../userland/timezones.md#c-interface) with the
accepted fold/gap rule, the reduced musl [`iconv`](../kernel/userspace.md#foundational-libc),
and the integer/string/math rows of the inventory: `strtoimax`, `atol`,
`strtok_r`, `strcoll`, `trunc`, `isnan` and `isinf`, plus the `E2BIG` errno.
Its [qualification](../development/experiments/calendar-encoding/README.md)
compares a table of times and conversions with host glibc. The filesystem
row's `EINTR`, `NAME_MAX`, `O_NOFOLLOW` and permission constants are not part
of this slice. Slice 4 adds `EINTR`. A later libc step adds `NAME_MAX`, accepts
`O_NOFOLLOW` and reports ELOOP and ENAMETOOLONG
([name limits and links](../userland/libc-portability.md#name-limits-and-symbolic-links));
permission constants and `umask` stay out.
The probe inventory below describes its original baseline, not current
availability of the delivered slice 1–3 APIs.

## Slice 4 contract (accepted 2026-10-10)

Implemented in the [native editor reference](../userland/neovim.md), with
[QEMU qualification](../development/experiments/neovim-first-slice/README.md).
Real Lua 5.1/luv/LPeg/libuv, utf8proc and tree-sitter archives link; iconv is libc.
Generators and help tags run on native host Lua, never the target editor.

1. **Internal child authority: explicit native spawn delegation.**
   Neovim's `ui_client_start_server` starts the same image with `--embed`.
   Its loop requires pipe/create, but ordinary libuv children deliberately
   receive only memory/clock/launcher. Add a native spawn extension with explicit
   caller-selected child resource grants; Neovim selects pipe/create for this
   internal server, leaving ordinary `uv_spawn` attenuation unchanged. Directional
   pipes use slots 0–2 and flags zero; redirected stdin needing fd 3 is refused
   before launch. Qualify both loops and
   child cleanup; never silently delegate the grant to ordinary jobs.

2. **Cached metadata and overwrite: held reference and explicit force.**
   Keep one reference per loaded file buffer until unload; comparison metadata
   includes validity. Compare the actual held writable target before truncation,
   retaining the original throughout. Ordinary `:w` refuses replacement or
   unavailable comparison metadata; `:w!` explicitly selects the current native
   target when freshness cannot be established. Never treat unknown zero IDs or
   times as equality, or retain freshness caches with unknown mtime. Qualify replacement, unavailable
   metadata and reference cleanup. This supplies no mutation lease: names and
   contents can change after sampling, and equal mtime never proves equal bytes.

Comparison metadata follows the backing, not the scheme spelling:

| Current path/backing | Held identity | Modification time and ordinary `:w` |
| --- | --- | --- |
| `home://` on a live image; `tmp://` RAM | Valid | Recorded when wall time is available; files whose sampled time is unavailable need `:w!`. |
| `home://` on an installed image; other npfs volumes | Valid | Per-inode modified-time validity; records without it need `:w!`, not every npfs file. |
| `host://` VirtIO-FS | Valid within the live mount/session | Fresh GETATTR supplies mtime; normal same-object comparisons work. Query failures remain errors. |
| `boot://` archive | Valid | Authored mtime unavailable, but the archive is read-only: `:w!` cannot overwrite it; save to a writable path. |

None of the writable backends always lacks identity or mtime. RAM without a
valid wall-clock sample and npfs records with unavailable modified time require
explicit force. A valid zero timestamp still participates in comparison.

Source evidence: libuv `pyxis/process.c` resource selection and `core.c` loop
admission; Neovim `ui_client.c`, `event/libuv_proc.c`, `os/fs.c`, `fileio.c` and
`bufwrite.c`; [live metadata lifetime](../interfaces/file-metadata.md).

Existing contracts cover native observer termination/exit reasons, raw console
input with a held passthrough grant, and generation-based resize without Unix
signals. Numeric PID operations remain unsupported; PID metadata is omitted.
Keep the current 16-colour profile, suppress probes and shutdown query waits,
set NVIM_NOTTYFAST before startup, and leave COLORTERM unset. A delegated runtime
root survives internal-child launch without forwarding the parent's app root.
Both defaults are accepted and implemented; native owner qualification remains.

## Probe and limits

Base: Pyxis `d01fe5996d971662ef60f175644e528c240916fd`, userland
`fbce73bfaae710ed5dcaa0e5fd3f7fb8416befd0`, ports
`4326582e4bfaad3833b8311f66aedc1f5308f90d`, filesystem `b427df29`.
A fresh clean SDK and the merged libuv recipe built using the existing
LLVM 23.1.3 builder (`pyxis-llvm23.1.3-49e2c1a`). No compiler rebuild.

All source downloads used owner mirrors. Neovim is commit
`5885a30e1e1225349079e7a1c4a3848aa8e43e42` (0.12.5); its mirror archive SHA-256 is
`314bb8d8695cc2c1c9b69e6c93df8c75108ca66588dfffb81c42369d2f85c90a`.
utf8proc 2.11.3, tree-sitter 0.26.13 and libiconv 1.17 matched the
[milestone manifest hashes](neovim-libuv.md#mirrors-the-owner-must-provide).

Host generators used a native build of the manifest's LuaJIT commit `fbb36bb6`
and native `nlua0`/LPeg 1.1.0/mpack. Target Lua 5.1-compatible headers came from
that LuaJIT source, plus luv 1.52.1-0's real header; **empty target Lua, luv and
LPeg archives are task 5 placeholders**, not working libraries. PUC Lua 5.1.5's
attempted `raw-lua` mirror URLs returned 404. No target LuaJIT was built.

| Build/link observation | Result |
| --- | --- |
| utf8proc | Static archive and whole-archive target link succeeded; no new libc gaps. |
| tree-sitter, Wasm off | Strict build failed on endian selection, `dup`, `fdopen`. A real compiler-endian branch plus diagnostic-only implicit-declaration warning downgrades produced an archive; whole-archive target link reported exactly `dup` and `fdopen`. Those pointer-conversion objects are unsafe to execute. |
| libiconv conversion library | Autoconf needed explicit `a.out` naming (the target defaults to `a.pxe`) and recognition of Pyxis in `config.sub`. Real configuration/header generation succeeded. Core compilation failed on `E2BIG`; no archive was manufactured. Localcharset/relocation objects compiled. The iconv command and broader gnulib closure were not built. |
| Neovim, strict | Configured with native generators, `PREFER_LUA=ON`, bytecode/LTO/unibilium/gettext/Wasmtime off. Shared `os/unix_defs.h:6` stopped compilation at `netdb.h`. |
| Neovim, diagnostic continuation | Removed unavailable includes from a scratch copy, without replacement types or API definitions. All generators ran; 157/201 target translation units compiled, 44 failed. The inventory below covers the reported independent gaps, grouping cascading incomplete-type errors. **Final Neovim link was not reached.** |
| Compiled-object symbol inspection | Besides task 5 symbols and unbuilt Neovim objects, current libraries lack `uv_get_total_memory`, `uv_print_all_handles`, `__stack_chk_guard`, `__stack_chk_fail`. This is a partial-object audit, not an exhaustive final link. |

The unmerged [probe branch](https://git.internal/PyxisOS/pyxis-os/src/branch/probe/neovim-task6)
keeps source checksums and build commands; raw logs stay local. Reproduce with
`make -j16 sdk`, the current libuv recipe (`lua build.lua libuv --sdk <sdk>`
from `ports`), then
that branch's `probe/README.md`. No Neovim executable, QEMU/native run, performance
claim or syntax-highlighting screenshot resulted. Additional errors may appear
once these blockers and task 5 are resolved; repeat the full build/link then.

## Reported gaps and call sites

Paths below refer to the exact Neovim source pin unless prefixed with a dependency.
Line numbers refer to upstream sources; generated-header positions refer to this
probe. A listed function can have additional callers.

| Subsystem | Missing functions/headers/constants and representative call sites | Owning action |
| --- | --- | --- |
| Configure/header profile | `netdb.h`, `netinet/in.h`, `pthread.h`, `sys/param.h`, `sys/socket.h`: `os/unix_defs.h:6–10`. Configure also reports absent `langinfo.h`, `sys/utsname.h`, `termios.h`, `sys/uio.h`, `sys/sdt.h`, `execinfo.h`, `pwd.h`, `sys/file.h`; failed `readv`, `readlink`, `strptime`, `dirfd`/`flock`, `getpwent`/`getpwuid`/`getpwnam` and `FD_CLOEXEC` checks (`cmake.config/CMakeLists.txt:40–91`). | A Pyxis platform profile; do not import Unix header families merely to select their branches. `HAVE_FORKPTY` is wrongly enabled unconditionally for unknown platforms. Optional configure checks are not all first-slice requirements. |
| Integer/string/locale | `strtoimax` (`charset.c:1101`); `atol` (`diff.c:3751`, `search.c:1174`, `eval.c:749`, `ex_cmds.c:2474/3195`, `ex_docmd.c:6111`, `quickfix.c:1362–1508`); `strtok_r` (`os/lang.c:263/268`, `os/stdpaths.c:115/129`); `strcoll` (`eval/typval.c:1239`, `ex_cmds.c:379`). | Shared libc, real conversions and reentrant tokenization; `strcoll` follows the existing C-only locale, not invented locale state. |
| Math/calendar | `isnan`/`isinf` (`cjson/lua_cjson.c:899–918`, `lua/executor.c:479`); `exp`, `cosh`, `sinh`, `tanh`, `asin`, `acos`, `trunc` (generated `funcs.generated.h:48–102`, source declarations `eval.lua`); `mktime` (`eval/funcs.c:7268`). | Extend the pinned libc math/calendar subset; task 5's Lua time calls need the same calendar contract. |
| Stdio | `fdopen` (`os/fs.c:484`, `runtime.c:2011`, `quickfix.c:1078`, `undo.c:1282`; tree-sitter `lib/src/tree.c:128`, `parser.c:2048`); `freopen` (`channel.c:176`); `setbuf` (`main.c:402`); `setvbuf`/`_IOFBF` (`profile.c:961`). | Real descriptor association/rebinding and buffering policy in libc. No ignored successful buffering request. |
| Descriptor sharing | `dup` (`os/fs.c:541`, `fileio.c:1694`, `ui_client.c:79`; tree-sitter `tree.c:124`); `fcntl`/`F_DUPFD_CLOEXEC`, `dup2` (`channel.c:594–596`). PTY additionally uses `fcntl`/`F_GETFL`/`F_SETFL`/`O_NONBLOCK` (`os/pty_proc_unix.c:197–203`). | Shared descriptor references with one cursor/read-ahead state, not independently copied FILE handles. Child stdio remains explicit native delegation. CLOEXEC is a launch-adapter concern, not a kernel fd flag. |
| Filesystem/encoding | `O_NOFOLLOW` (`bufwrite.c:753/1766`, `memfile.c:774`, `undo.c:1250`); `S_IREAD`/`S_IWRITE` (`memfile.c:622/776`); `NAME_MAX` (`fileio.c:2456`); `umask` (`fileio.c:3316`); `EINTR` (`fileio.c:2510/2522`, `runtime.c:2856`, `quickfix.c:798–848`); `E2BIG` (`mbyte.c:2520/2566`; libiconv `lib/loop_unicode.h:173/204/250/357/409/495/515`). | Honor existing no-follow lookup and capability rights; remove mode/umask assumptions, do not synthesize permission state. Export native component bound where meaningful. Add real errno vocabulary/translation without pretending signal interruption exists. |
| Tree-sitter endian | Unsupported platform in `lib/src/portable/endian.h:238`; `le16toh`/`be16toh` in `unicode.h:18/29`. | Ordinary portable-source endian selection using compiler definitions; no new ABI. |
| Network | `struct addrinfo`, `sockaddr_storage`, `sockaddr_in`, `sockaddr_in6`; `AF_UNSPEC`, `AF_INET`, `SOCK_STREAM`, `AI_NUMERICSERV` (`event/socket.c:75–162/317–354`). | Excluded socket/server features return unsupported through a bounded port adaptation; native TCP integration is later work. |
| Process/signals | `getpid` (`os/env.c:391`), `pthread_exit` (`lua/executor.c:287`); `signal`, `sigemptyset`, `pthread_sigmask`, `sigset_t`, `SIG_SETMASK` (`os/signal.c:44–46`); `SIGTERM`, `SIGHUP`, `SIGKILL`, `SIGINT` (`event/proc.c:227–270`), `SIGWINCH` (`tui/tui.c:170`). | Explicit process observation/termination, native console interrupt and resize; no fake PID, signals or pthread lifecycle. Excluded luv threads must fail before entering thread-only bodies. |
| Unix PTY | `sys/ioctl.h`, `sys/wait.h`, `poll.h`, `pty.h`; `pid_t`, `struct winsize`, `struct termios`; `forkpty`, `kill`, `waitpid`, `ptsname`, `ioctl`, `killpg`, `setsid`, `_exit`, `execvp`, `cfsetispeed`, `cfsetospeed` (`os/pty_proc_unix.c:174–450`). `TIOCSWINSZ`, `WNOHANG`, `WUNTRACED`, `WCONTINUED`, `WIFSTOPPED`, `WIFCONTINUED`, `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`; `SIGCHLD`, `SIGCONT`, `SIGQUIT`, `SIGALRM`, `SIG_DFL`. | `:terminal`/PTY jobs remain excluded, with explicit errors rather than compiling an invented Unix substrate. |
| Termios/key input | `termios.h`, `poll.h`, `tcgetattr`, `tcsetattr`, `tcflag_t`, `TCSANOW`, `_POSIX_VDISABLE`, `VQUIT`, `VSUSP`, `VMIN`, `VTIME` (`tui/termkey/termkey-internal.h:13/56`, `termkey.c:502–569`). PTY also references `ICRNL`, `IXON`, `OPOST`, `ONLCR`, `CS8`, `CREAD`, `ISIG`, `ICANON`, `IEXTEN`, `ECHO`, `ECHOE`, `ECHOK`, `VINTR`, `VERASE`, `VKILL`, `VEOF`, `VEOL`, `VEOL2`, `VSTART`, `VSTOP`, `VREPRINT`, `VWERASE`, `VLNEXT`. | Adapt first-slice input/modes to the existing console/libuv native stream and key decoder. Do not bypass stream ownership or install termios-shaped kernel calls. |
| Partial symbols/build policy | `uv_get_total_memory` (`os/mem.c:12`); `uv_print_all_handles` (`log.c:207`); stack-protector symbols in compiled objects. | Implement genuine libuv handle diagnostics or remove that optional log dependency. Use an explicit editor memory budget; SYSTEM_INFO_MEMORY is allocator accounting, not installed RAM. Follow SDK stack-protector flags instead of upstream automatically enabling an unavailable runtime. |

Platform-specific Windows/Darwin headers, sanitizer headers, gettext and Wasm
headers in inactive branches are not target gaps. Secondary pointer-conversion,
`mask`, `init_termios` and incomplete-type errors follow the missing declarations
above, rather than requiring additional APIs.

### Original baseline behaviour despite declarations

These are **source-inspected**, not target runtime failures:

- `uv_chdir`, environment mutation/enumeration and `uv_fs_realpath` return
  unsupported; `uv_cwd` and default child launch use startup state, and spawn
  rejects `options.cwd`. Neovim uses them in `os/fs.c:96/112/1338`,
  `os/env.c:174/211/225–375`, and `path.c:2305`. Direct `environ` enumeration
  needs the explicit-store adapter.
- Neovim's slash-root, colon-separated PATH and `/tmp` assumptions need native
  scheme parsing and the launcher path rules (`path.c:1825`, `os/unix_defs.h`,
  `os/stdpaths.c`); string normalization must never substitute for lookup.
  Identity/time users (`os/fs.c:1293–1326`, file read/save checks) must consume
  libuv's native validity bits rather than treating unknown zero fields as IDs
  or modification stamps. Permissions, symlinks and ownership are not POSIX data.
- Signals initialize unconditionally in `event/loop.c`; raw TTY mode calls
  currently return unsupported. Neovim needs real console mode/interrupt/resize
  adaptation. Pool, async filesystem, watches, sockets, modules, jobs and PTYs
  retain their existing exclusion, not fake successful handles.

## Accepted contract (owner, 2026-10-10)

1. **Working path: one mutable libc context backed by retained native
   directory capabilities**, shared by all libc path functions and libuv.
   `:cd`, `:lcd` and `:tcd` use a real process-working-path operation without
   a kernel cwd.
   Preallocate the new chain and descriptive path, perform `path_change`, then
   publish atomically; failure preserves the previous state/rights. `getcwd`
   returns the tracked normalized `scheme://` description, not reconstructed
   physical ancestry. Unknown launch spelling is an error until an explicit
   scheme change establishes it; external rename can leave that description
   stale while held capabilities remain valid. Do not make `PWD` authoritative.
   Children receive a snapshot of the current chain/path; an explicit child cwd
   is resolved in a separate temporary context. The bounded `realpath`
   profile must prove the target exists through capability lookup,
   re-resolve the candidate scheme spelling and compare valid live identity
   with the held original target before returning it. Refuse providers,
   unavailable identity and stale/unknown ancestry; aliases are not collapsed.
   Neovim's cwd-text fallback after failed realpath needs the same distinction:
   a descriptive name must not be treated as proved canonical resolution.

2. **Environment: copied mutable libc store with explicit child snapshots.**
   `getenv`/`setenv`/`unsetenv` share one store seeded from startup; names are
   case-sensitive, nonempty and exclude `=`; empty values differ from absence.
   Allocation failure leaves state unchanged, `overwrite` is honored, and an
   absent removal succeeds. Borrowed `getenv` values may be invalidated by a
   successful mutation. Native enumeration produces launcher `startup_variable`
   arrays; do not add a second writable `environ` authority or `putenv` aliasing.
   Default launch explicitly forwards the current snapshot; supplied environment
   replaces it, including an empty one. Startup accessors stay immutable.
   Libuv and each port must use these helpers rather than the original block.

3. **Shared library closure: bounded real userland libc extensions, including
   the existing pinned musl subset.** No kernel POSIX layer. Contracts:

   - `fdopen` validates mode and allocates before taking ownership; failure
     leaves the descriptor with its caller, successful `fclose` closes it.
     Preserve position/read-ahead; `w` does not truncate. Retain one FILE per
     descriptor, reject an existing association, and add FILE access selection
     so `fdopen` of an O_RDWR descriptor with `r` cannot write. Append retains
     the existing non-atomic append limit. `dup` needs a shared descriptor
     object; `freopen` and buffering follow as separate small libc steps.
   - `mktime` normalizes calendar fields and inverts the selected real TZif zone
     using the pinned calendar arithmetic. UTC only when `TZ` is absent/empty;
     missing/invalid zones fail as current localtime does. The bounded DST
     rule is: `tm_isdst` may select a unique fold candidate; ambiguous input
     without a unique selection and nonexistent gap times fail ENOTSUP as a stated
     profile restriction, not as overflow. Overflow and failure preserve the
     input `tm`; success updates it. Mutable `TZ` follows the current lazy reload
     and borrowed-zone-name lifetime; no system timezone configuration is added.
   - `iconv` imports audited conversion routines/tables from the existing musl
     1.2.5 pin (`0784374d`), preserving notices. Initial repertoire: UTF-8,
     ASCII, ISO-8859-1 and explicit-endian UTF-16LE/BE; other names fail EINVAL.
     Implement streaming pointer/count updates, reset, E2BIG, incomplete EINVAL
     and invalid-sequence EILSEQ. Valid characters unrepresentable in the
     destination also fail EILSEQ; audit/adapt upstream substitution behavior.
     No byte-copy substitute, transliteration, locale-based encoding choice
     or dynamic converter plugins. The GNU probe establishes build gaps, not approval to import its full library. Recheck
     Neovim/Git conversion-name usage against this repertoire before delivery.

## Highlighted C in the first slice

**Delivered by slice 4; the original probe did not demonstrate it.**
Legacy syntax does not need tree-sitter parsers. Upstream enables filetypes and
legacy syntax during ordinary startup (`main.c:485–501`,
`runtime/syntax/syntax.vim`); `runtime/syntax/c.vim:579–636` links C token groups.
Package the runtime Vimscript and core Lua modules under a delegated native
`VIMRUNTIME` path, with configuration under the agreed `home://` location.
Retain runtime search/source/autocommand support; `.h` defaults to C++ unless
`g:c_syntax_for_h` selects C. Merged task 5 supplies core Lua/filetype loading.

Both [kernel TTY and mux](../userland/terminal.md#tty-output-controls) support
256 palette entries, semicolon RGB, defaults, reverse and synthetic
bold/italic/underline. The bounded SGR interpreter is shared with the interactive
remote client; malformed colour groups change no attributes.

Upstream has no `pyxis` built-in terminfo and falls back to eight-colour ANSI
(`tui/terminfo.c:71–125`). Slice 4 deliberately keeps its first-run static entry
at sixteen colours.
A separate small follow-up can advertise indexed/RGB controls and rendered
styles, then select `termguicolors`; a small Vimscript scheme can assign
Comment/Statement/Type/PreProc/Constant/String groups explicitly. Upstream's
default dark groups do not all assign a terminal foreground.

Suppress unsupported Pyxis DCS/OSC probes (`tui/tui.c:482–495`): both current
parsers otherwise draw query payload as text. Set upstream's `NVIM_NOTTYFAST=1`
**before startup**, since setting `nottyfast` in user configuration is too late;
leave `COLORTERM` unset. This is a bounded port profile, not a renderer expansion.
Later terminal OSC/DCS consumption and a broader Unicode repertoire remain separate work.

## Remaining delivery slices

- [x] Stream rebinding/output buffering and native editor recipe, full target
  link, manual tab/pane editing, legacy C syntax, save, cwd and cleanup.
- [ ] Owner native qualification (slice 5): run the bundle in a tab and configured
  mux pane, edit/save/reopen C, inspect syntax, use `:cd`, resize and quit. Confirm
  ordinary replacement refusal and explicit force on the selected native backing.
- [ ] Separate profile follow-up after the first sixteen-colour run: advertise
  the merged RGB and bold/italic/underline support. No implementation assignment yet.

## Terminal SGR follow-up

Accepted and implemented 2026-10-10.

- [x] Deliver the shared SGR profile in the kernel TTY, mux pane emulator and
  interactive pyxis-remote, with matched before/after qualification.

The [sequence table](../userland/terminal.md#tty-output-controls) now advertises
bold, italic, underline, 256 palette indices and semicolon RGB, enabling the
Neovim static profile to use `termguicolors`. Colon forms are rejected, the
16-parameter bound remains, and malformed groups change no attributes.
Per-TTY palette entries 0–15 use the active scheme (Aardvark everywhere today,
shared with the host); the remaining entries use the xterm cube and greys.
The bitmap styles are synthetic and stay within 8×16 cells.

Cells use tagged index/default/RGB colours in 12 bytes; mux retains 1,024
history rows per pane and the existing eager creation/resize rollback.
[Qualification and measured costs](../development/experiments/terminal-sgr/README.md)
cover tab, pane, remote transfer and live resize, including the backing increase
and small measured output costs. Allocation-failure rollback is inspected,
not injected. Scheme configuration and a broader Unicode repertoire remain deferred.

## Terminal UTF-8 first slice

Owner decisions accepted and implemented 2026-10-10.

- [x] Deliver bounded UTF-8 in kernel TTY, mux and interactive remote, UTF-8
  selection Copy and tree box drawing, with matched terminal qualification.

Keep the Bizcat atlas and advertise only the exact
[terminal repertoire](../userland/terminal.md#tty-output-controls). Its 19
missing Latin-1 scalars use replacement. Native U+FFFD maps to the atlas's
outlined placeholder, distinct from `?`. Partial sequences survive writes;
unsupported scalars produce one cell, invalid bytes one replacement each, and
raw controls retain their meaning. Supported scalars stay in 12-byte cells;
Copy exports UTF-8 under the existing byte limit. Safe Paste and line editing
stay ASCII-only. Tree defaults to UTF-8, with explicit `--ascii` branches for
byte-oriented less wrapping. [Qualification and measured costs](../development/experiments/terminal-utf8/README.md)
include the existing mux receiver-availability limitation. The Neovim Unicode
profile change is a separate follow-up.

Far-future owner direction, 2026-10-10 (unassigned): scalable fonts, choosing
TTF/OTF/WOFF by implementation ease, for example an stb_truetype-style rasterizer
with a glyph cache. This slice adds no font source, dependency or scalable-font
contract.

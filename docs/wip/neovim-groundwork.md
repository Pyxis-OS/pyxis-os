# Neovim task 6 groundwork

**Owner accepted the three defaults on 2026-10-10. Working path and environment
are implemented in the first slice; later slices remain unassigned.**
This splits [task 6](neovim-libuv.md#tasks); task 5's Lua/luv delivery remains a
prerequisite. The shared libc work also serves [hosted Clang](hosted-clang.md)
and [Git](git-on-pyxis.md). Existing first-slice exclusions remain in force.

## First implementation slice

The [process-state reference](../userland/process-state.md) describes shared libc
`chdir`/`getcwd`, copied mutable environment and explicit child snapshots, with
shell and port consumers updated together. [Qualification](../development/experiments/process-state/README.md)
records the fresh-main baseline, matched launch costs and QEMU child-snapshot run.
The bounded proved `realpath` is deliberately the next slice, requiring live
identity validation of the returned spelling. Stdio, calendar, encoding and the
Neovim recipe remain later work. The probe inventory below describes its original
baseline, not current availability of the delivered cwd/environment APIs.

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

### Behaviour still missing despite declarations

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

**Feasible after the closure and TUI adaptations; not demonstrated by this probe.**
Legacy syntax does not need tree-sitter parsers. Upstream enables filetypes and
legacy syntax during ordinary startup (`main.c:485–501`,
`runtime/syntax/syntax.vim`); `runtime/syntax/c.vim:579–636` links C token groups.
Package the runtime Vimscript and core Lua modules under a delegated native
`VIMRUNTIME` path, with configuration under the agreed `home://` location.
Retain runtime search/source/autocommand support; `.h` defaults to C++ unless
`g:c_syntax_for_h` selects C. Task 5 is still needed for core Lua/filetype loading.

Both [kernel TTY and mux](../userland/terminal.md#tty-output-controls) support **16 palette
colours, defaults and reverse**, not 256-colour/RGB SGR or bold/italic/underline.
Extended SGR is not safely interchangeable: `38;5;7` can activate reverse and
five-parameter RGB sequences exceed the four-parameter parser bound.

Upstream has no `pyxis` built-in terminfo and falls back to eight-colour ANSI
(`tui/terminfo.c:71–125`). Add a truthful static entry matching task 3, including
bright SGR and alternate screen, with no unsupported attributes/colour capability.
Use `notermguicolors` and a small 16-colour Vimscript scheme explicitly assigning
Comment/Statement/Type/PreProc/Constant/String groups with `cterm=NONE`; upstream's
default dark groups do not all assign a terminal foreground.

Suppress unsupported Pyxis DCS/OSC probes (`tui/tui.c:482–495`): both current
parsers otherwise draw query payload as text. Set upstream's `NVIM_NOTTYFAST=1`
**before startup**, since setting `nottyfast` in user configuration is too late;
leave `COLORTERM` unset. This is a bounded port profile, not a renderer expansion.
Later terminal OSC/DCS consumption, 256/truecolour and Unicode rendering remain
separate work.

## Proposed delivery slices

After separate implementation assignments: shared working path/explicit
environment; stdio/descriptor association; calendar/encoding and
remaining numeric/string closure; Neovim native platform/TUI/runtime recipe;
then the full target link and manual tab/pane qualification. Re-run against task
5's actual libraries, with no placeholders, before claiming editor delivery.
Keep save durability/identity validity, child pipe authority, unsupported features
and cleanup in that qualification. No task 6 implementation starts in this PR.

## Later work

**Queued owner direction, 2026-10-10; not assigned:** grow SGR in both the kernel
TTY and mux panes: underline for diagnostics, bold and italic as the font allows,
and a 256-colour palette. Entries 0–15 come from the active scheme (the owner
uses Aardvark from the terminal colour-scheme collection); 16–255 use the standard
colour cube and greys. Once implemented, the Neovim profile can advertise these
capabilities. True colour requires a separate owner decision.

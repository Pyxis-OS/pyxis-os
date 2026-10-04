# Links port investigation

Status: investigation complete; no implementation is selected or authorized.
Links is next in the [queue](application-ports.md) (vi, then Links, then less)
for reading HTML documentation offline, such as the Java SE 8 Virtual Machine
Specification, without leaving Pyxis. This document scopes a first slice that
reads local files only and lists the decisions to make before porting. HTTP
and HTTPS are later slices.

## Source and evidence

[Links](http://links.twibright.com/) 2.30 (2024-07-28) is the newest release.
Its release archive `links-2.30.tar.bz2` has SHA-256
`c4631c6b5a11527cdc3cb7872fc23b7f2b25c2b021d596be410dadb40315f166`. Each source
file says it is "released under GPL", and `COPYING` contains GPL version 2. The
licence is GPL, as for Doom and Quake, so the
[ports licensing](../../ports/LICENSING.md) precedent applies. A recipe should
record the exact upstream wording rather than add "or later".

How the evidence was gathered:

- **Host build (measured).** I built Links natively on Fedora x86-64 with glibc,
  in text mode only. SSL, compression, GPM, libevent and all graphics drivers
  were off. The stripped binary is 1.41 MB. `nm` lists 129 imported symbols.
  Of these, 49 already exist in Pyxis libc (userland `877d04f`) and 77 do not.
- **Compiler finding.** Under GCC 16 with glibc, `ftp.c:771` fails to compile
  even with `-std=gnu17`, because glibc's `strchr` now preserves `const`. My
  scratch copy needed a one-line cast. Pyxis libc's `strchr` returns `char *`,
  so the Pyxis build is not affected.
- **Source inspection** covers the event loop, the platform layers, URL and
  file handling, terminal output and the TLS dependency.

Nothing was cross-compiled, linked, packaged or booted.

## How Links is built

Links isolates each platform in `os_dep.h` and `os_depx.h`.

- **Single-process ports exist.** The DOS (DJGPP) and OpenVMS ports keep one
  process with no fork. Through `os_depx.h` they redirect `read`, `write`,
  `pipe`, `close` and `select` to their own code, using the in-process
  `vpipe.inc` virtual pipes. `dos_select()` checks those pipes, polls the
  keyboard and honours timer deadlines.
- **Feature switches.** `os_dep.h` turns off asynchronous DNS, fork-on-exit,
  file-security checks and SMB, and selects drive-prefixed paths for DOS.
- **Directory listings.** `file.c` shows rights, link counts and owners only
  when a platform defines `FS_UNIX_RIGHTS`, `FS_UNIX_HARDLINKS` and
  `FS_UNIX_USERS`. Dates need `strftime`. Without those, a listing is kind,
  size and name.
- **Optional facilities.** The `tsearch` cache index, AF_UNIX session sharing,
  locale functions and `mallopt` all have upstream fallbacks or are compiled
  only on platforms that enable them.
- **Terminal output.** The "dumb" frame mode draws with `| + -`. Links emits
  cursor addressing, `CSI 2J`, `CSI 0m` and 8-colour SGR, all inside the
  [Pyxis VT subset](../userland/terminal.md#tty-output-controls). The alternate
  screen, DECSC/DECRC and mouse mode would be ignored.
- **Keyboard.** Links parses raw escape sequences itself and uses a timer to
  recognize a standalone Escape.
- **Character sets.** Links converts page text to the terminal's charset. With
  an ASCII terminal it substitutes approximations for non-ASCII characters, so
  UTF-8 specifications stay readable on today's renderer.

## The Pyxis gap

The 77 missing symbols fall into three groups:

| Group | Symbols | First-slice treatment |
| --- | --- | --- |
| Platform layer, as on DOS | `select`, `poll`, `pipe`, `dup`/`dup2`, `fcntl`, `fork`/`exec*`/`waitpid`/`system`/`setpgid`/`kill`/`raise`, signals, termios, `ioctl`, rlimits, `sysconf`/`getpagesize`, `uname`/`gethostname`/`getpid`, `mallopt`/`malloc_trim`, locale | Redirect or switch off in a Pyxis platform block |
| Networking | `socket`, `connect`, `bind`, `listen`, `accept`, `get/setsockopt`, `getsockname`, `getaddrinfo`/`freeaddrinfo`, `inet_ntop`/`inet_pton` | Deferred to the HTTP slice |
| Local files and time | `opendir`/`readdir`/`closedir`/`dirfd`, `stat`/`lstat`/`fstat`, `getcwd`/`chdir`, `lseek`, `unlink`, `fsync`, `access`, `readlink`, `tempnam`, `strcspn`, `strftime`, `timegm`, `clock_gettime`/`gettimeofday`, plus download-only `chmod`, `utimes` and `fallocate` | Needed, or patched around (below) |

Relevant native facts:

- **Waiting.** [`wait_many`](../devices/tcp.md) waits on TCP, terminal
  attachments, processes and groups. Console and terminal input accept only
  INTERRUPT, on an armed handle, so a program cannot wait for keystrokes and
  sockets in one call. Console reads do take a deadline, through libterm's
  timed read.
- **Directory metadata.** Native enumeration returns each name with a kind:
  file, directory, symlink, other or unknown. Files report their size. There
  are no timestamps, owners or permission bits.
- **Working directory.** It is a retained chain of directory capabilities. The
  startup display path is a description, not authority.

## Proposed first slice: local files

**Platform.** Add a `PYXIS` block to `os_dep.h`/`os_depx.h`, keyed on the
compiler's `__pyxis__`, and a `pyxis.c` modelled on `dos.c`:

- **Event loop.** In-process `vpipe.inc` pipes. `pyxis_select()` returns ready
  virtual pipes at once. Otherwise it blocks in a timed libterm console read
  until the next Links timer, keeping the byte it reads. There is no busy
  polling.
- **Terminal.** Reads from fd 0 and writes to fd 1 go to the named console
  grants through libterm, holding [Ctrl+C passthrough](../userland/foreground-interruption.md)
  as Kilo and vi do. The size comes from `term_size`; there is no resize
  notification.
- **Switched off.** Fork, signals, AF_UNIX sharing, async DNS, SMB and file
  security. File loading stays synchronous.

**Files.** Use Links' existing `file://` handling, with the URL mapping from
decision 1. Directory listings show kind, size and name only.

**Configuration.** `~/.links2` needs `HOME`, `mkdir` and save-by-rename. Either
store it under `home://` or run without saved configuration. This is a routine
choice, settled during implementation.

**Limits to state.**

- **Content:** ASCII display, with non-ASCII characters approximated; no images.
- **Network:** no HTTP, HTTPS or downloads.
- **External programs:** none.
- **Screen size:** fixed; no resize notification.

**Validation.** An ordinary build, then interactive QEMU browsing of a local
HTML tree on `host://`: links, tables, frames, back/forward, search and
directory listings. Use the remote terminal and the framebuffer, as with vi,
and record the image size.

**Sizing (estimate).** Links has 73 C files. The platform file is new; the
core patches are the platform headers, `file.c`'s date column and the
command-line URL translation. For scale, `dos.c` is 852 lines including its
PC-specific screen and mouse code.

## Later slices

- **HTTP.** Needs libc sockets over the native TCP endpoints, and a way to wait
  for console input and sockets together. Without a native extension, the
  loop would alternate a zero-timeout console poll with short `wait_many`
  deadlines, adding latency and wake-ups. That native question belongs with
  the [event-wait direction](neovim-libuv.md#proposed-bounded-native-milestones).
- **HTTPS.** Links uses about 100 distinct OpenSSL symbols, mainly in `https.c`
  (904 lines) and `connect.c`. An Mbed TLS backend over userland `libtls` is a
  substantial rewrite of its own.

## Alternatives

Lynx needs curses (PDCurses on libterm would come first), w3m needs the Boehm
garbage collector, and ELinks is larger and also built on `select`. None of
them removes the event-loop or metadata questions, so these were not
re-measured.

## Decisions before implementation

1. **Local URL mapping.** Proposed default: Links keeps standard `file://` URLs,
   and the platform layer translates `file:///ROOT/rest` to the Pyxis path
   `ROOT://rest` at `open`/`opendir`/`stat`. Command-line arguments such as
   `links host://jvms/index.html` are translated the same way, and relative
   arguments use the startup display path. Every `file://` special case,
   including security checks and relative-link joining, keeps working
   unchanged.
   - **Alternative A:** register Pyxis roots as Links protocols, so native URLs
     such as `host://jvms/index.html` appear. That needs changes wherever Links
     special-cases `file://`.
   - **Alternative B:** embed Pyxis paths inside `file://`, as DOS does with
     drive letters. Path normalization would then need patching for the inner
     `//`.
2. **File metadata in libc.** Proposed default:
   - **Directories:** add `opendir`/`readdir`/`closedir` with `d_name` and
     `d_type` from native enumeration.
   - **`stat`/`fstat`:** add them with a deliberately narrow `struct stat` that
     holds only `st_mode` file-type bits and `st_size`. There are no permission,
     owner, link or time fields, so a consumer needing them fails to compile
     instead of reading invented values. Links' date column is then patched to
     stay blank.
   - **Alternative:** keep `stat` out of libc and patch `file.c` onto native
     lookups.
   - **Also:** the routine additions `strcspn`, `lseek`, `unlink` and `fsync`.
3. **Event model and scope.** Proposed default: ship the local-files slice with
   the DOS-style single-process loop, and defer HTTP until console-plus-socket
   waiting is decided natively. The alternative is to settle that native wait
   first and port local and HTTP browsing together.

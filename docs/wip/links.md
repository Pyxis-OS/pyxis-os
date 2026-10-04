# Links port

Status: investigation complete. The [decisions](#agreed-decisions) were agreed
on 2026-10-04; the port has not started. Links is next in the
[queue](application-ports.md) (vi, then Links, then less). The goal is reading
HTML documentation such as the Java SE 8 Virtual Machine Specification without
leaving Pyxis.

In this port Links loads every page through libc `fopen`, GET only. Local files,
`system://` and the `http://`/`https://` providers all use that one path. Links'
own sockets, DNS and OpenSSL are not used.

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
- **Protocols.** `url.c` maps each scheme to a handler. `file://` is
  free-syntax, so everything after `file://` is path data. Relative links are
  joined by `join_urls`.
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
- **TLS.** HTTPS in Links uses about 100 distinct OpenSSL symbols, mainly in
  `https.c` (904 lines) and `connect.c`. This port does not need them.

## The Pyxis gap

The 77 missing symbols fall into three groups:

| Group | Symbols | Treatment |
| --- | --- | --- |
| Platform layer, as on DOS | `select`, `poll`, `pipe`, `dup`/`dup2`, `fcntl`, `fork`/`exec*`/`waitpid`/`system`/`setpgid`/`kill`/`raise`, signals, termios, `ioctl`, rlimits, `sysconf`/`getpagesize`, `uname`/`gethostname`/`getpid`, `mallopt`/`malloc_trim`, locale | Redirect or switch off in a Pyxis platform block |
| Networking | `socket`, `connect`, `bind`, `listen`, `accept`, `get/setsockopt`, `getsockname`, `getaddrinfo`/`freeaddrinfo`, `inet_ntop`/`inet_pton` | Not needed: the `http://` and `https://` providers fetch through `fopen` |
| Local files and time | `opendir`/`readdir`/`closedir`/`dirfd`, `stat`/`lstat`/`fstat`, `access`, `getcwd`/`chdir`, `lseek`, `unlink`, `fsync`, `readlink`, `tempnam`, `strcspn`, `strftime`, `timegm`, `clock_gettime`/`gettimeofday`, plus download-only `chmod`, `utimes` and `fallocate` | Added to libc ([decision 2](#agreed-decisions)) or patched around |

Relevant native facts:

- **Waiting.** [`wait_many`](../devices/tcp.md) cannot wait for console input;
  console and terminal input accept only INTERRUPT, on an armed handle.
  Console reads do take a deadline, through libterm's timed read. With
  networking behind `fopen`, Links only ever waits for keys and timers.
- **Directory metadata.** Native enumeration returns each name with a kind:
  file, directory, symlink, other or unknown. Lookup opens only files and
  directories and never follows symlinks. Files report their size. There are
  no timestamps, owners or permission bits.
- **Providers.** [Scheme providers](../userland/http-fetch.md) open their full
  URI through the namespace. The HTTP(S) provider returns bytes only for
  status 200 (204 is empty) and never follows redirects.
- **Working directory.** It is a retained chain of directory capabilities. The
  startup display path is a description, not authority.

## Agreed decisions

Agreed on 2026-10-04, after the [review](https://git.internal/PyxisOS/pyxis-os/pulls/397):

1. **Every page load goes through libc `fopen`, GET only.** This replaces
   the earlier URL-mapping proposals.
   - **One handler:** a generic Pyxis protocol handler opens the URL with
     `fopen` and reads its bytes, so `host://`, `home://`, `system://`, `http://`
     and `https://` behave alike. Links' own `http`/`https`/`ftp` handlers, DNS,
     sockets and OpenSSL are not used.
   - **Unknown schemes:** any `scheme://` missing from Links' protocol table
     goes to this handler and is treated as hierarchical, so relative links
     resolve against it.
   - **Directories:** a native directory is listed through libc `opendir` and
     `stat`, using Links' existing listing generator.
   - **No content type:** HTML is detected by sniffing for `<html` or similar,
     falling back to the file extension.
2. **libc metadata.**
   - **Directories:** `opendir`/`readdir`/`closedir` with `d_name` and
     `d_type` from native enumeration.
   - **`stat`/`fstat`:** a deliberately narrow `struct stat` holding only
     `st_mode` file-type bits and `st_size`. There are no permission, owner,
     link or time fields, so a consumer needing them fails to compile instead
     of reading invented values. Links' date column is patched to stay blank.
   - **`access`:** `F_OK`, `R_OK` and `W_OK`, implemented as opens with the
     matching rights. A later vi follow-up can then drop its two adapter probes.
   - **Also:** `strcspn`, `lseek`, `unlink` and `fsync`.
   - **`lstat` and `readlink`:** lookup never follows symlinks, so `lstat`
     behaves exactly as `stat`, and both fail for a symlink entry with the
     native error. `readdir` still reports such entries as `DT_LNK`. `readlink`
     fails with a real error, because there is no native link-reading
     operation.
3. **Event model.** Use the DOS-style single-process loop. Local and HTTP(S)
   browsing ship together, since both are `fopen`.

## Port scope

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
  security.

**Loading.** The Pyxis handler runs synchronously in the main loop. It checks
whether a path names a directory: if so, it uses the listing generator;
otherwise it reads through `fopen`. Provider URIs do not support directory
operations, so they always go through `fopen`.

**Configuration.** `~/.links2` needs `HOME`, `mkdir` and save-by-rename. Either
store it under `home://` or run without saved configuration. This is a routine
choice, settled during implementation.

**Limits to state.**

- **Blocking loads:** every load blocks the UI, network fetches included. Only
  the HTTP provider's own deadlines bound them.
- **HTTP behaviour:**
  - redirects and statuses other than 200/204 surface as open errors
    (see [HTTP redirects](../technical-debt.md#http-redirects));
  - GET forms work as URLs with a query string; there is no POST;
  - there are no cookies or custom request headers.
- **Content type:** detected by sniffing or extension. A charset comes from
  the page's own `<meta>` or Links' default.
- **Display:** ASCII, with non-ASCII characters approximated; no images.
- **Programs:** no downloads to external programs.
- **Screen size:** fixed; no resize notification.

**Validation.** Browse a local mirror of the Java SE 8 JVM specification HTML,
for example on `host://`, interactively through the remote terminal and the
framebuffer, as with vi. Also open an `https://` page, a native directory
listing and an error case such as a redirect. Record the image size.

**Sizing (estimate).** Links has 73 C files. The platform file and the Pyxis
protocol handler are new. The core patches are the platform headers, the
protocol table's fallback, `file.c`'s date column and command-line URL
handling. For scale, `dos.c` is 852 lines including its PC-specific screen and
mouse code.

## Alternatives

Lynx needs curses (PDCurses on libterm would come first), w3m needs the Boehm
garbage collector, and ELinks is larger and also built on `select`. None of
them removes the event-loop or metadata questions, so these were not
re-measured.

## Known limit: response metadata

`fopen` gives a program no response metadata: no media type, status or
redirect target. The native OPEN reply already carries an optional media type.
Exposing that kind of metadata to programs in a way that fits Pyxis is future
design work; see [technical debt](../technical-debt.md#response-metadata-through-fopen).

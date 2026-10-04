# Links

The image includes [Links](http://links.twibright.com/) 2.30 in text mode at
`app://links.pxe`. Its GPL licence is at `app://share/licenses/links/COPYING`,
and the shell resolves `links` to it. It reads HTML documentation such as the
Java SE 8 Virtual Machine Specification without leaving Pyxis. The
[recipe notes](../../ports/links/README.md) record the source pin, patches and
platform layer.

```text
links host://jvms/index.html
links index.html
links https://example.com/
links -dump host://notes.html
```

Links draws on the named `input`/`output` console grants through libterm. It
runs on the framebuffer console and through the [remote terminal](remote-terminal.md).
It holds [Ctrl+C passthrough](foreground-interruption.md) while it runs, and quits
on Ctrl+C itself, as it does upstream. `q` asks before quitting.

## Loading pages

Every page loads through libc, GET only. Links' own sockets, DNS and OpenSSL
code is compiled but never reached.

| URL | How it loads |
| --- | --- |
| `index.html`, `docs/a.html` | Becomes `file://index.html`, a path relative to the inherited working directory |
| A startup root such as `host://`, `home://` or `app://` | Links' file loader over libc `stat`, `open` and `opendir`/`readdir` |
| A directory in either form | Links' listing: kind, size and name, linked relative to the directory |
| `http://`, `https://` and any other `scheme://` | Opened with `fopen` through its namespace provider and read to the end |

Native directories and roots are decided by the program's startup roots. A
scheme Links does not know is treated as hierarchical with no host part, so
relative links resolve against the path. Links decodes `%XX` escapes in local
URLs, as it does for `file://`. A provider receives the URL unchanged.

Providers report no media type to `fopen`. A page that starts like HTML
(`<!doctype html`, `<html`, `<head`, `<body`, `<title` or `<!--`) is shown as
HTML; anything else is typed by its extension, like a local file. A charset
comes from the page's `<meta>` or Links' default.

## How the port maps onto Pyxis

The port follows Links' DOS model: one process, with in-process virtual pipes
between Links' internal threads.

- **Event loop.** `pyxis_select` returns ready pipes at once. Otherwise it
  waits for console input with a timed libterm read bounded by Links' next
  timer, and keeps the bytes it read for the next `read`. With no terminal
  input to wait for, it sleeps on the named clock. There is no busy polling,
  because nothing else in the process can make a pipe ready while it waits.
- **Terminal.** The size is read when the terminal starts. Mouse-mode
  sequences are not sent, and on exit the screen is cleared with the cursor at
  the top, because the terminal has no saved cursor.
- **Configuration.** None. Options, bookmarks and history are not saved, and
  saving options reports an inaccessible home directory. `home://` is
  RAM-backed, and Links' save path needs exclusive creation and private file
  modes.
- **Switched off.** Fork, signals, other programs, asynchronous DNS, SMB and
  file-security checks.

Userland libc gained the functions Links calls:

- `opendir`/`readdir`/`closedir` over native enumeration;
- `stat`/`fstat` with a `struct stat` holding only file-type bits and size;
- `lseek`, `fsync`, `unlink` and `strcspn`.

The [libc metadata limits](../technical-debt.md#narrow-libc-file-metadata)
record what those functions cannot report.

## Limits

- **Blocking loads:** every load blocks the interface, network fetches
  included. Only the HTTP provider's own deadlines bound them.
- **HTTP:**
  - a redirect or any status other than 200/204 is an open error, and a
    redirect reads "Operation not supported" (see
    [HTTP redirects](../technical-debt.md#http-redirects));
  - GET forms work as URLs with a query string; there is no POST, and no
    cookies or request headers;
  - HTML is detected by sniffing (see
    [response metadata](../technical-debt.md#response-metadata-through-fopen)).
- **Local files:**
  - symlink entries are listed with an unknown type but cannot be opened;
  - directories show size 0 and the date column is blank;
  - listings have no `..` entry.
- **Other protocols and downloads:** `ftp://` and `finger://` need Links' own
  name lookup and sockets; lookup always fails, so they report "Host not
  found". Downloads to disk fail with "Invalid argument", because they need
  exclusive creation.
- **Display:** ASCII, with non-ASCII characters approximated, and no images.
- **Screen size:** fixed, with no resize notification.
- **Programs:** Links starts no other programs.

See [technical debt](../technical-debt.md#links-port-limits) for revisit points.

## Decisions

Agreed by the owner on 2026-10-04, after the investigation in
[#397](https://git.internal/PyxisOS/pyxis-os/pulls/397):

1. **Every page load goes through libc `fopen`, GET only.** Local files
   and the HTTP(S) providers use that one path. Unknown schemes are
   hierarchical. Native directories are listed through libc with Links'
   listing generator, and HTML is detected by sniffing, falling back to the
   extension.
2. **libc metadata.** Directory reading with `d_type`, a narrow `stat`/`fstat`
   with no invented fields, and `strcspn`, `lseek`, `unlink` and `fsync`.
3. **Event model.** The DOS-style single-process loop, with local and HTTP(S)
   browsing shipped together.

Two parts of decision 2 were not needed, so they were not added:

- **`lstat`/`readlink`.** Links uses them only when a platform defines
  `FS_UNIX_SOFTLINKS`, which the Pyxis block does not. A symlink entry
  therefore shows an unknown type through `stat`.
- **`access`.** Links never calls it, so it waits for a consumer, such as a
  vi follow-up.

Configuration storage was left to implementation. Links runs without saved
configuration (see above).

## Why Links

Links 2.x does its own terminal handling, draws within the
[Pyxis VT subset](terminal.md#tty-output-controls), and already has a
single-process platform model in its DOS and OpenVMS ports. Lynx needs curses
(PDCurses on libterm would come first), w3m needs the Boehm garbage collector,
and ELinks is larger and also built on `select`.

## Validation

Pyxis main `6623675` with this change, userland `7178b06` and ports `274ef70`
were built with `make -j16 image`. Links builds with no compiler warnings under
the recipe's flags, which silence upstream's unused-parameter, unused-variable
and similar classes. The image booted four CPUs under nested KVM, with patched QEMU
10.2.2, virtio-net and a virtio-fs export holding a local mirror of the JVMS SE8
HTML chapters. The following was exercised:

- **Remote terminal (100×29):**
  - the JVMS index and chapters from `host://`, including search, link
    following, fragment positions and Back;
  - the 1 MB, 323-page chapter 4, which loaded in about a quarter of a second;
  - `links index.html` relative to `cd host://jvms`, shown as
    `file://index.html`, and `-dump`;
  - directory listings of `host://` and `host://jvms` with the trailing-slash
    redirect, a name containing a space, a long name and a symlink entry;
  - `https://example.com/` from the command line and through Go to URL;
  - a redirecting `http://github.com/`, a missing file, `ftp://`, a download
    and Save options, all reporting errors;
  - Ctrl+C and `q` quitting, with a cleared screen and the prompt at the top.
- **Framebuffer console (QEMU `sendkey`):** drawing with reverse-video link
  selection, arrow keys, search, following a link and quitting.
- **libc directly:** a probe compiled in the guest with TCC exercised `stat`
  on files, directories, roots, a symlink, a provider URI and missing paths,
  plus `fstat`, `lseek`, `fsync`, `unlink`, `strcspn` and `readdir` with a
  name longer than its initial buffer.

`links.pxe` is 1,356,559 bytes.

# Links

The image includes [Links](http://links.twibright.com/) 2.30 in text mode at
`bin://links.pxe`. Its GPL licence is at `boot://share/licenses/links/COPYING`,
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
| A startup root such as `host://`, `tmp://` or `boot://` | Links' file loader over libc `stat`, `open` and `opendir`/`readdir` |
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
- **Configuration.** Links keeps `links.cfg`, `html.cfg`, bookmarks, cookies and
  the URL history in `home://links/`, made at startup, and loads them from there.
  Links' own save writes an exclusive temporary file, syncs it and renames it over
  the old one; the port only creates the temporary with mode 0666. `home://` is
  RAM on a live boot, so the files last until reboot, and persist on an installed
  system. If the directory cannot be found or made, nothing is loaded and saving
  reports "Home directory inaccessible".
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
  included. Only the HTTP provider's 30-second budget bounds them. In review
  under nested KVM, a server that never answered left a blank screen for 32 s
  before "Operation timed out", and a Ctrl+C pressed meanwhile quit Links only
  after the open returned.
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
- **Other protocols:** `ftp://` and `finger://` need Links' own name lookup and
  sockets; lookup always fails, so they report "Host not found".
- **Downloads:** the Download dialog saves to any path the program's roots allow,
  relative to the inherited working directory, through exclusive creation. An
  existing file offers Continue, Overwrite, Rename or Cancel; Overwrite truncates
  in place and a download is written under its final name, so neither is atomic.
  A root without CREATE reports its error, such as "Permission denied" in `host://`
  of the read-only space.
- **Saves and crashes:** libc has no directory sync, so after a crash a saved
  configuration may keep its old contents, and a crash can leave a temporary
  file such as `links.c0`.
- **Local links from remote pages:** a page fetched over HTTP(S) can link to
  `host://`, `home://` or `system://`, and following the link opens the local
  object. A page cannot script or submit what it opens, but desktop browsers
  refuse this navigation.
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

Configuration storage was left to implementation. It now lives in `home://links/`
(see above).

## Why Links

Links 2.x does its own terminal handling, draws within the
[Pyxis VT subset](terminal.md#tty-output-controls), and already has a
single-process platform model in its DOS and OpenVMS ports. Lynx needs curses
(PDCurses on libterm would come first), w3m needs the Boehm garbage collector,
and ELinks is larger and also built on `select`.

## Validation

Pyxis main `6623675` with this change, userland `7178b06` and ports `274ef70`
were built with `make -j16 image`. The pins later became userland `a3eb8d2` and
ports `36d952e`, which only merge later main changes (net0 selection, installer
updates and the BusyBox mirror fetch) into those commits. Links builds with no
compiler warnings under the recipe's flags, which silence upstream's
unused-parameter, unused-variable and similar classes. The image booted four
CPUs under nested KVM, with patched QEMU 10.2.2, virtio-net and a virtio-fs
export holding a local mirror of the JVMS SE8 HTML chapters. The following was
exercised:

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

### Saved configuration and downloads (2026-10-09)

An ordinary `make -j16 image` with the patch changes booted under QEMU 10.2.2 (KVM, four CPUs, virtiofsd export), driven on the framebuffer console
through `sendkey` and screenshots, with the remote shell for listings:

- **Before:** with the old patch, Setup > Save options and a Download both failed with
  "Invalid argument" from the unsupported `O_EXCL` sentinel. That was the only cause.
- **Options:** Save options wrote `links.cfg` with the changed left margin. After quitting
  and restarting Links the page was indented by it and the dialog showed 7. Save html
  options wrote `html.cfg`, and quitting added `cookies.txt` and `links.his`. No temporary
  file remained.
- **Downloads:** the 5,120-byte `data.bin` from a `host://` page and from a local HTTP
  server (`http://10.0.2.2:8000`) saved to `home://`, and each matched the original with
  `cmp` after copying back. A repeated download offered Continue, Overwrite, Rename and
  Cancel, and Overwrite produced an identical file.
- **Read-only space:** a download to `host://` there failed with "Could not create file
  host://ro.bin: Permission denied". With `home://links` made a file, Links started with
  no configuration and Save options reported "Home directory inaccessible".

Not exercised: a read-only `home://` root, a full or failing write, a crash, and
Continue on an interrupted download.

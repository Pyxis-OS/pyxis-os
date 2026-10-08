# Ports

Pyxis pins [pyxis-ports](https://git.internal/PyxisOS/pyxis-ports) at `ports`.
Its host Lua runner fetches an exact upstream commit or a checksum-pinned release
archive or standalone data file from the internal mirrors, applies ordered patches,
builds against the exported SDK and stages executables with their licenses.
Recipes are trusted build code. They do not modify the SDK or resolve/install
dependencies. Source pins, licenses, host/Pyxis dependencies, patch order and
output names live in each recipe's metadata; `ports.lua` only lists recipes.

## Ports and native commands

Agreed 2026-10-05. Ports bring familiar tools to Pyxis; they must not become
its userland. Everything that makes Pyxis itself stays native, and ported tools
keep it working.

**Native-owned.** Anything that creates or shows a Pyxis concept is a native
userland program, never a port:

- the shell, init and sessions; spaces, titles and affinity;
- grants, mounts and services;
- the installer and Update;
- network configuration;
- the core file commands, such as `ls`, `cat`, `head`, `mkdir`, `mv`, `rm`,
  `rmdir` and `sync`, where scheme roots and grants show up.

No ported command duplicates a native one, so there is no BusyBox `sh`, `init`,
`mount`, `ls`, `ps` or `ifconfig`. A tool that lists or inspects system state,
such as a `ps` or a `find` over scheme roots, is native too.

**Ported leaf tools.** A port is a tool whose value is familiar behaviour, such
as an editor, pager, archiver, checksum, text filter or browser. Each BusyBox
applet or sbase tool is added on its own, as a recorded decision, never by
enabling a default configuration. If removing a port would also remove a Pyxis
concept, that tool should have been native.

**Pyxis behaviour keeps working.** A ported tool:

- accepts scheme paths such as `system://` and `http://` through libc;
- runs with only the grants it was given, with no ambient authority;
- fails explicitly instead of faking success. For example, it refuses links or
  unsupported metadata rather than pretending to create them.

Every port's finish criteria include at least one check of this behaviour. For
BusyBox `tar`, that includes extracting a `tmp://` archive into `system://` and
a clear refusal without write access. Platform support follows the
[libc portability rules](../userland/libc-portability.md): standard libc
functions built on native constructs, not POSIX-shaped kernel mechanisms.

## Building and packaging

```sh
git submodule update --init userspace ports
make ports                 # export SDK and stage the selected ports
make image                 # also package userspace and assemble the ISO
make run CPUS=4
```

The host needs Lua 5.4 (`LUA=lua5.4` selects its executable), Git, GNU Make 4.3+,
GNU coreutils, CMake 3.21 or newer, curl, tar, bzip2 and the Pyxis target toolchain, plus access to the
internal mirrors (`git.internal/mirrors` and `repo.internal`) to fetch source on a port rebuild. See [SDK/repository setup](sdk-and-repositories.md).
The build container includes Lua; the owner publishes container updates.

`make ports` exports the SDK before checking recipe and SDK dependencies.
Unchanged inputs reuse each port's `build/ports/<name>/stage`. Recipe or SDK
changes rebuild the affected port in a fresh work directory, replacing its
fetched source and intermediate outputs. Make source changes in the recipe/patches, not that disposable copy.
For port development with a separately managed work directory, use the
[standalone runner](../../ports/README.md).

Ordinary programs live in `bin://`; live boots bind `bin://` to the boot
archive, and installed systems keep them on the pool, with only the rescue set in
`boot://` (see the [system layout](../userland/system-layout.md)). Packaged
licenses and data stay under `boot://share`.

The normal image includes Kilo at `bin://kilo.pxe` and its BSD-2-Clause
license at `boot://share/licenses/kilo/LICENSE`. The shell resolves the bare
command `kilo` to that executable. The ports-owned `install.lua` selects guest
payloads into a dedicated tree, which the root [archive manifest](boot-archive.md)
combines with userland and the guest SDK. Intermediate and host outputs stay out.
Fresh staging removes obsolete files and preserves unchanged output timestamps. Kernel-only
`make` and `make sdk` do not build ports. `make userspace` now consumes the
Lua, HTTP-parser and TLS development files from the ports build, in addition to the SDK.

The root workflow has a separate ports job consuming the SDK job's artifact.
It publishes a [bundle](build-bundles.md) containing separate boot and
development trees. The userland job consumes its libraries/headers; the image
job consumes only its boot tree. Both can reuse it without compiling ports again. Source checkout uses
`PYXIS_SOURCE_READ_TOKEN`. Cross-repository dispatch remains future work.

## zlib development library

The [zlib recipe](../../ports/zlib/README.md) builds a static core library from
the checksum-pinned 1.3.2 release archive, fetched through the owner's mirror.
It exports `libz.a`, `zlib.h` and `zconf.h` under `build/ports-dev/zlib`, outside
the base and guest SDK. Consumers include that prefix's headers and link its
archive before the SDK runtime libraries. Only the upstream license, port
notice and archive provenance enter the boot payload.

The profile retains compression/decompression streams, checksums, default
allocators and buffer convenience APIs. It omits `gz*` file helpers, contrib,
shared libraries and upstream programs/tests. Public headers are unchanged,
so using an omitted helper fails at link time. The library adds no file,
network or display authority. The [screenshot milestone](../wip/screenshots.md)
will qualify it at runtime through the PNG consumer; archive compilation is
not that qualification.

## TLS development libraries

The [Mbed TLS recipe](../../ports/mbedtls/README.md) verifies the official 4.1.1
release archive before extraction, including its bundled TF-PSA-Crypto 1.1.1.
It exports three static archives, upstream headers, the client configuration
and `share/mbedtls.mk` under `build/ports-dev/mbedtls`. Consumers use that make
fragment's configuration flags and ordered libraries. Native clock, entropy,
allocation and TCP integration lives in userland's `libtls`.

Only licenses and provenance from the TLS library recipe enter the boot archive.
The base/guest SDK stays independent of TLS.

## PCI ID database

The `pciids` data recipe stages `pci.ids` from the PCI ID Project unchanged at
`boot://share/hwdata/pci.ids`. The pinned upstream commit is recorded in
[its metadata](../../ports/pciids/metadata.lua), and the source, commit and
database version are written to `boot://share/pciids/source.txt`. Pyxis uses the
3-clause BSD option of the database's GPL-2.0-or-later or BSD-3-Clause license.
The upstream repository has no separate license file, so the recipe supplies the
BSD text with the copyright holders named in the database header. It and a
notice explaining that choice are installed under `boot://share/licenses/pciids/`.
[lspci](../userland/lspci.md) reads the file, and so can ordinary text tools.
Updating means picking a new commit and rebuilding; nothing should depend on
fixed line counts or particular entries. See the
[recipe notes](../../ports/pciids/README.md).

## Public CA roots

The `ca-certificates` data recipe installs curl's Mozilla-derived 2026-09-25
snapshot at `boot://share/ca-certificates/cacert.pem`, with its SHA-256 and
provenance beside it. MPL-2.0 and retained notices are installed under
`boot://share/licenses/ca-certificates/`. The selected bundle contains 121
certificates in 188,900 bytes, pinned by SHA-256
`a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`.
The PEM export omits Mozilla's additional trust-store constraints; it is not the
full browser trust policy. See the [recipe notes](../../ports/ca-certificates/README.md).

Updates are explicit: select a dated snapshot, review certificate changes,
verify the published checksum, update ports metadata/notices, rebuild the image
and restart providers. No boot-time download or automatic trust update occurs.
Custom bundles augment these roots per instance under the same verification
rules; configuration, platform integration and limits are described in
[HTTPS trust](../userland/https.md), with launch commands in [fetching](../userland/http-fetch.md).

## Checksums with sbase cksum

The image includes `bin://cksum.pxe`, resolved as `cksum` by the shell. The
[sbase recipe](../../ports/sbase/README.md) pins the task-1 source revision and
builds cksum, a restricted tee, uniq and sha256sum with their helpers. Ordered
patches narrow private util.h, restore the declarations uniq needs, and adapt
tee's options and descriptor lifetimes; cksum, uniq, sha256sum and helper bodies
remain unchanged and use conventional libc I/O calls. The full license/contributor list, arg.h notice and
OpenBSD strtonum notice are packaged at `boot://share/licenses/sbase/LICENSE`,
`boot://share/licenses/sbase/arg.h` and `boot://share/licenses/sbase/strtonum.c`.

```text
cksum host://hello.c
cat host://hello.c | cksum
cksum < host://hello.c > home://checksum.txt
cksum host://first - host://last < host://input
```

Results contain the CRC and byte count, followed by the name for named inputs.
Relative paths use the inherited working directory. With no operands, cksum
reads stdin; an explicit `-` uses the same stdin and prints `<stdin>` as its label.
Multiple operands are processed in order, continuing after missing/unreadable inputs
with an aggregate nonzero exit status. Detected output errors also fail.

Results require EOF. Use a finite file redirect or pipeline for stdin; the
framebuffer console has no EOF operation. Independent terminal attachments can
explicitly end input after queued bytes drain.
See the [accepted terminal limit](../technical-debt.md#console-input-completion).

## Copying streams with sbase tee

The shell resolves `tee` to `bin://tee.pxe`:

```text
cat host://input | tee home://first home://second | cksum
```

Tee copies stdin to stdout and each named output, creating or truncating files
through the caller's native grants. Its 0666 creation argument selects native
policy; it does not implement Unix permissions. Options `-a` and `-i` are
unsupported and rejected before opening files. Use `--` before names beginning
with a dash.

An output failure is reported and that descriptor is closed while surviving
outputs continue. If none remain, tee stops reading and closes stdin so an
upstream writer can observe closure. Open/read/write/close errors produce a
nonzero status. Missing stdin is rejected before files are touched; missing
stdout still allows named copies but reports failure. Output may be partial on
error, and naming the input as an output can destroy its contents. Console EOF
has the same limit as cksum above. See the [recipe notes](../../ports/sbase/README.md)
for the exact upstream adaptations.

## Adjacent duplicates with sbase uniq

The shell resolves `uniq` to `bin://uniq.pxe`:

```text
uniq host://input
cat host://input | uniq -c
uniq -d -f 1 host://input home://duplicates
```

Uniq collapses runs of adjacent identical lines with upstream `-c`, `-d`, `-u`,
`-f N` and `-s N`, and `[input [output]]` operands where `-` selects stdin or
stdout. File and pipe input is fetched in blocks by
[stdio read-ahead](../userland/stdio.md#input-read-ahead); console stdin remains
[one native read per byte](../technical-debt.md#console-line-input). See the
[uniq reference](../userland/uniq.md) for behavior, limits and validation, and the
[recipe notes](../../ports/sbase/README.md) for the port adaptation.

## SHA-256 digests with sbase sha256sum

The shell resolves `sha256sum` to `bin://sha256sum.pxe`:

```text
sha256sum host://image.raw home://notes.txt
cat host://input | sha256sum
sha256sum a b > home://SHA256SUMS
sha256sum -c home://SHA256SUMS
```

Output lines contain the lowercase digest, two spaces and the name, with stdin
and `-` labelled `<stdin>`. Unopenable operands are reported and later operands
continue with status 1. With `-c`, manifests from operands, `-` or stdin accept
`digest  name` and `digest *name` lines; listed names resolve against the working
directory. Each file prints OK or FAILED, and malformed lines, unreadable files
and mismatches are counted and give status 1. Files are hashed in BUFSIZ reads;
manifest lines use line input through [stdio read-ahead](../userland/stdio.md#input-read-ahead).
See the [recipe notes](../../ports/sbase/README.md) for remaining upstream limits.

## Editing in Pyxis

Select the Development space with Super+Right. The shell starts at `home://`:

```text
kilo hello.c
cat hello.c
kilo hello.c
```

Enter text, save with Ctrl-S and quit with Ctrl-Q before the next command.
Ctrl-F searches; arrows select matches, Enter accepts and Escape cancels.
Arrows, Home/End, Page Up/Down, Backspace and Delete provide normal editing.
Kilo uses the inherited working directory and terminal grants, libterm for
input/output and size, and libc streams for files. It retains C syntax
highlighting. It keeps Ctrl+C as editor input for its whole session, so an armed
shell cannot terminate it; Ctrl-Q quits. See the [recipe notes](../../ports/kilo/README.md) for the source pin
and local patches, and [terminal behavior](../userland/terminal.md) for shared facilities.

The current editor is ASCII, redraws at the new size when the terminal is
[resized](../kernel/display.md), accepts LF/CRLF
and saves LF with a final newline per row. Saves truncate before writing:
failures can leave partial files. Ordinary status messages expire after five
seconds using the inherited monotonic clock, including while idle; an active
search prompt remains visible until the search ends. Actual terminal input EOF
exits cleanly for an unchanged buffer and reports failure if unsaved edits are
lost. Allocation failure reports an error and exits, losing unsaved edits.
Installed systems keep `home://` on the npfs pool; on live boots `home://` and
`tmp://` are RAM-backed, and `boot://` is always read-only. An optional `host://`
mount persists files in its host export, subject to host permissions and the
[virtiofs setup](../devices/virtio-fs.md). Processes receive a fixed 1 MiB
stack with a reserved, unmapped guard page below it and no automatic growth.

The same editor runs through the [remote host client](../userland/remote-terminal.md).
Pyxis has [atomic replacement](../interfaces/filesystem-mutations.md), but Kilo
still saves in place.
[BusyBox vi](../userland/vi.md) is packaged at `boot://vi.pxe` with its GPL-2.0-only
license at `boot://share/licenses/busybox/LICENSE`, and the shell resolves `vi`
to it. It is a modal alternative to Kilo, with the same terminal grants and
Ctrl+C passthrough; see the [recipe notes](../../ports/busybox/README.md).
[Links](../userland/links.md) is packaged at `bin://links.pxe` with its GPL
license at `boot://share/licenses/links/COPYING`. It is a text web browser that
loads local files and HTTP(S) pages through libc; see the
[recipe notes](../../ports/links/README.md).

The [edit/build/run walkthrough](edit-build-run.md) combines Kilo and TCC;
[guest Lua](../userland/lua.md) is independent of the host recipe runner.

## Paging and archives with BusyBox

[BusyBox less](../userland/less.md) and [uncompressed tar](../userland/tar.md)
are selected separately in the existing BusyBox recipe and packaged at
`bin://less.pxe` and `bin://tar.pxe`, with the shared GPL-2.0-only license.
No default BusyBox configuration or native-command replacement is enabled.

```text
less boot://share/hwdata/pci.ids
ls boot:// | less
tar tf tmp://manuals.tar
tar xf tmp://manuals.tar
tar cf tmp://backup.tar system://project
```

Less reads keys from the named console even when content comes from a pipe.
The [shell's final-stage grant](../userland/shell.md#foreground-pipelines) adds
READ alone when stdin is a pipe/file and stdout is a console, without keyboard,
pointer or interrupt-arming authority. Tar validates its full input snapshot
before extracting, refuses unrepresentable/unsafe members, and ignores metadata
that Pyxis does not expose. Host archives use `tar --format=ustar`; the
[recipe notes](../../ports/busybox/README.md) record memory and format limits.

## TCC and the guest SDK

The normal image includes `bin://tcc.pxe`; the shell resolves `tcc` to it.
The [recipe](../../ports/tcc/README.md) builds TCC with the prebuilt Pyxis Clang and
exports the guest executable, target libtcc1, compiler-private headers, licenses
and ordered patch provenance. No compiler-container rebuild is needed.

`boot://sdk` contains:

- `usr/include`: shared libc/libpyxis/libterm, ABI/P1F and npfs format headers.
- `usr/lib`: `crt0.o`, libc, libterm, libpyxis, npfs format and the compiler
  runtime archive (compiler-rt builtins).
- `lib/tcc`: libtcc1 and private `stddef.h`, `stdarg.h`, `stdbool.h`, `float.h`.
- `share`: TLSF/musl/TCC/npfs licenses and notices, TCC source pin and patches, and
  the toolchain's fork revision and runtime licensing.
- `manifest.txt`: SDK provenance plus the ports bundle's source and dependency record.

From `home://`, compile a saved C source with `tcc hello.c -o hello.pxe`, then
launch `./hello.pxe`. TCC supports `-E`, ELF object output with `-c`, and static
P1F linking; the port notes list supported options and limits. The compiler uses
inherited root grants, such as read-only `boot` and writable `home`, and needs
no launch authority.
The SDK packages target runtime files, not host compilers or a host converter.
Clang on the host remains the compiler for the OS and maintained applications.

See the [edit/build/run walkthrough](edit-build-run.md) and
[TCC contract and limits](../userland/tcc.md).

## Fastfetch

The image includes `bin://fastfetch.pxe` and notices under
`boot://share/licenses/fastfetch`. Run `fastfetch` for native system information
and the Pyxis ASCII logo, or `fastfetch --json` for structured output. See the
[usage and limits](../userland/fastfetch.md) and
[recipe reference](../../ports/fastfetch/README.md).

## Guest Lua

The image includes `bin://lua.pxe` and the upstream MIT notice at
`boot://share/licenses/lua/lua.h`. The first interpreter accepts one expression
chunk through the shell:

```text
lua -e 'print("Hello from Lua", 2 ^ 0.5)'
```

`lua file.lua [args...]` runs a script using the inherited working directory or
an explicit URI such as `home://scripts/hello.lua`. Lua's `arg` table and `...`
carry script arguments. `loadfile` and `dofile` require filenames and use the
same path rules, without changing to the script's directory or searching modules.

With no arguments, `lua` starts a libterm REPL with expression results,
multiline statements and persistent globals. Ctrl+C discards pending input;
Ctrl+D on an empty line exits, including at a continuation prompt. Language
errors are reported without leaving the REPL; allocation/terminal failures exit
nonzero. The parent shell can terminate an executing chunk with Ctrl+C;
Lua has no cooperative signal hook.

Base, coroutine, table, string, UTF-8, io, bounded os and pure-Lua package
libraries are available. Module lookup uses exact `LUA_PATH` when set, otherwise
the script's directory and then `boot://share/lua/`. Native `pyxis` helpers run
argv lists, enumerate directories and hash files through Mbed TLS PSA. Lua's
recipe therefore requires an explicit `--mbedtls` development prefix in addition
to the SDK; the embedding archive stays independent of crypto and the bridge.
Script and `-e` errors return a nonzero process status with runtime tracebacks.
Stdin scripts, shebang handoff, debug, full math, process CPU time, buffer controls,
shell execution and dynamic modules remain deferred.
This does not replace the host Lua used by build recipes. See the
[port notes](../../ports/lua/README.md) and
[Lua runtime reference](../userland/lua.md).

## Timezone data

The `tzdata` recipe builds matching host zic and the pinned IANA database. The
[packaged data](../userland/timezone-data.md) includes all standard zones/aliases and notices
under `boot://share`, with no dependency on the host's installed timezone version.
It travels in the same ports bundle as the executable ports. Libc provides
[local-time conversion](../userland/timezones.md); absent or empty `TZ` defaults to UTC.

## USB ID database

The `usbids` data recipe stages the pinned upstream text unchanged at
`boot://share/hwdata/usb.ids`. The [metadata](../../ports/usbids/metadata.lua)
records its commit; `boot://share/usbids/source.txt` records provenance. Pyxis elects
the USB ID Project's BSD database grant, independently of the mirror repository's
GPL license. Terms and NOTICE are packaged under `boot://share/licenses/usbids/`.
[lsusb](../userland/lsusb.md) uses the data only for descriptive names and falls
back to numeric IDs when names are unavailable. Updating the database requires
no compiler-container rebuild. See [recipe notes](../../ports/usbids/README.md).

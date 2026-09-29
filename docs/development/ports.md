# Ports

Pyxis pins [pyxis-ports](https://git.internal/PyxisOS/pyxis-ports) at `ports`.
Its host Lua runner fetches an exact upstream commit or a checksum-pinned release
archive or standalone data file, applies ordered patches,
builds against the exported SDK and stages executables with their licenses.
Recipes are trusted build code. They do not modify the SDK or resolve/install
dependencies. Source pins, licenses, host/Pyxis dependencies, patch order and
output names live in each recipe's metadata; `ports.lua` only lists recipes.

## Building and packaging

```sh
git submodule update --init userspace ports
make ports                 # export SDK and stage the selected ports
make image                 # also package userspace and assemble the ISO
make run CPUS=4
```

The host needs Lua 5.4 (`LUA=lua5.4` selects its executable), Git, GNU Make 4.3+,
GNU coreutils, CMake 3.20.2 or newer, curl, tar, bzip2 and the Pyxis target toolchain, plus network access to fetch
upstream source on a port rebuild. See [SDK/repository setup](sdk-and-repositories.md).
The build container includes Lua; the owner publishes container updates.

`make ports` exports the SDK before checking recipe and SDK dependencies.
Unchanged inputs reuse each port's `build/ports/<name>/stage`. Recipe or SDK
changes rebuild the affected port in a fresh work directory, replacing its
fetched source and intermediate outputs. Make source changes in the recipe/patches, not that disposable copy.
For port development with a separately managed work directory, use the
[standalone runner](../../ports/README.md).

The normal boot archive includes Kilo at `app://kilo.pxe` and its BSD-2-Clause
license at `app://share/licenses/kilo/LICENSE`. The shell resolves the bare
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

## TLS development libraries

The [Mbed TLS recipe](../../ports/mbedtls/README.md) verifies the official 4.1.1
release archive before extraction, including its bundled TF-PSA-Crypto 1.1.1.
It exports three static archives, upstream headers, the client configuration
and `share/mbedtls.mk` under `build/ports-dev/mbedtls`. Consumers use that make
fragment's configuration flags and ordered libraries. Native clock, entropy,
allocation and TCP integration lives in userland's `libtls`.

Only licenses and provenance from the TLS library recipe enter the boot archive.
The base/guest SDK stays independent of TLS.

## Public CA roots

The `ca-certificates` data recipe installs curl's Mozilla-derived 2026-09-25
snapshot at `app://share/ca-certificates/cacert.pem`, with its SHA-256 and
provenance beside it. MPL-2.0 and retained notices are installed under
`app://share/licenses/ca-certificates/`. The selected bundle contains 121
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

The image includes `app://cksum.pxe`, resolved as `cksum` by the shell. The
[sbase recipe](../../ports/sbase/README.md) pins the task-1 source revision and
builds cksum and a restricted tee with their helpers. Ordered patches narrow
private util.h and adapt tee's options and descriptor lifetimes; cksum and helper
bodies remain unchanged and use conventional libc I/O calls.
The full license/contributor list and arg.h notice are packaged at
`app://share/licenses/sbase/LICENSE` and `app://share/licenses/sbase/arg.h`.

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
console has no EOF operation, so terminal-only input cannot finish normally.
See the [accepted terminal limit](../technical-debt.md#console-input-completion).

## Copying streams with sbase tee

The shell resolves `tee` to `app://tee.pxe`:

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

## Editing in Pyxis

Select CPU 1 with Super+Right on a multicore boot. The shell starts at `home://`:

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
highlighting. See the [recipe notes](../../ports/kilo/README.md) for the source pin
and local patches, and [terminal behavior](../userland/terminal.md) for shared facilities.

The current editor is ASCII, uses fixed terminal dimensions, accepts LF/CRLF
and saves LF with a final newline per row. Saves truncate before writing:
failures can leave partial files. Status messages persist until replaced;
no userspace clock is assumed. Allocation failure reports an error and exits,
losing unsaved edits. `home://` remains volatile across reboot, while `app://`
is read-only. Processes receive a fixed 1 MiB stack with a reserved, unmapped
guard page below it and no automatic growth.

Persistent storage, atomic replacement and timekeeping remain separate work.
The [edit/build/run walkthrough](edit-build-run.md) combines Kilo and TCC;
[guest Lua](../userland/lua.md) is independent of the host recipe runner.

## TCC and the guest SDK

The normal image includes `app://tcc.pxe`; the shell resolves `tcc` to it.
The [recipe](../../ports/tcc/README.md) builds TCC with the prebuilt Pyxis GCC and
exports the guest executable, target libtcc1, compiler-private headers, licenses
and ordered patch provenance. No compiler-container rebuild is needed.

`app://sdk` contains:

- `usr/include`: shared libc/libpyxis/libterm and ABI/P1F headers.
- `usr/lib`: `crt0.o`, libc, libterm, libpyxis and target libgcc archives.
- `lib/tcc`: libtcc1 and private `stddef.h`, `stdarg.h`, `stdbool.h`, `float.h`.
- `share`: TLSF/musl/TCC licenses and notices, TCC source pin and patches, and
  the selected toolchain's hashes, patches and runtime licensing.
- `manifest.txt`: SDK provenance plus the ports bundle's source and dependency record.

From `home://`, compile a saved C source with `tcc hello.c -o hello.pxe`, then
launch `./hello.pxe`. TCC supports `-E`, ELF object output with `-c`, and static
P1F linking; the port notes list supported options and limits. The compiler uses
inherited read-only `app` and writable `home` grants and needs no launch authority.
The SDK packages target runtime files, not host compilers or a host converter.
GCC remains the compiler for the OS and maintained applications.

See the [edit/build/run walkthrough](edit-build-run.md) and
[TCC contract and limits](../userland/tcc.md).

## Guest Lua

The image includes `app://lua.pxe` and the upstream MIT notice at
`app://share/licenses/lua/lua.h`. The first interpreter accepts one expression
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
nonzero. Cancellation cannot interrupt an executing chunk.

Base, coroutine, table, string and UTF-8 libraries are available. Script and
`-e` errors return a nonzero process status; runtime errors include a traceback.
Stdin scripts, shebang handoff, package loading, io/os, debug
and the full math library remain deferred, as does signal-driven interruption.
This does not replace the host Lua used by build recipes. See the
[port notes](../../ports/lua/README.md) and
[Lua runtime reference](../userland/lua.md).

## Timezone data

The `tzdata` recipe builds matching host zic and the pinned IANA database. The
[packaged data](../userland/timezone-data.md) includes all standard zones/aliases and notices
under `app://share`, with no dependency on the host's installed timezone version.
It travels in the same ports bundle as the executable ports. Libc provides
[local-time conversion](../userland/timezones.md); absent or empty `TZ` defaults to UTC.

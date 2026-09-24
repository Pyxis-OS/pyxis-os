# Ports

Pyxis pins [pyxis-ports](https://git.internal/chronium/pyxis-ports) at `ports`.
Its host Lua runner fetches an exact upstream commit, applies ordered patches,
builds against the exported SDK and stages executables with their licenses.
Recipes are trusted build code. They do not modify the SDK or resolve/install
dependencies. Source pins, licenses, host/Pyxis dependencies, patch order and
output names live in each recipe's metadata; `ports.lua` only lists recipes.

## Building and packaging

```sh
git submodule update --init userspace ports
make ports                 # export SDK, build and stage Kilo and TCC
make image                 # also package userspace and assemble the ISO
make run CPUS=4
```

The host needs Lua 5.4 (`LUA=lua5.4` selects its executable), Git, GNU Make 4.3+,
GNU coreutils and the Pyxis target toolchain, plus network access to fetch
upstream source on a port rebuild. See [SDK/repository setup](sdk-and-repositories.md).
The build container includes Lua; the owner publishes container updates.

`make ports` exports the SDK before checking recipe and SDK dependencies.
Unchanged inputs reuse each port's `build/ports/<name>/stage`. Recipe or SDK
changes rebuild the affected port in a fresh work directory, replacing its
fetched source and intermediate outputs. Make source changes in the recipe/patches, not that disposable copy.
For port development with a separately managed work directory, use the
[standalone runner](../ports/README.md).

The normal boot archive includes Kilo at `app://kilo.pxe` and its BSD-2-Clause
license at `app://share/licenses/kilo/LICENSE`. The shell resolves the bare
command `kilo` to that executable. Packaging has an explicit entry list; it
enumerates only selected payloads and the freshly assembled guest SDK, never
other files left in build output. The SDK staging tree removes obsolete files
and preserves its timestamps when contents are unchanged. Kernel-only
`make`, `make sdk` and `make userspace` do not build ports.

The existing integrated workflow checks out both submodules with
`PYXIS_SOURCE_READ_TOKEN` and runs `make image`. Separate ports CI, SDK exchange
and dispatch orchestration remain future work; there is no package manager.

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
highlighting. See the [recipe notes](../ports/kilo/README.md) for the source pin
and local patches, and [terminal behavior](terminal.md) for shared facilities.

The current editor is ASCII, uses fixed terminal dimensions, accepts LF/CRLF
and saves LF with a final newline per row. Saves truncate before writing:
failures can leave partial files. Status messages persist until replaced;
no userspace clock is assumed. Allocation failure reports an error and exits,
losing unsaved edits. `home://` remains volatile across reboot, while `app://`
is read-only. Processes receive a fixed 64 KiB stack without automatic growth.

Persistent storage, atomic replacement and timekeeping remain separate work.
The [edit/build/run walkthrough](edit-build-run.md) combines Kilo and TCC;
[guest Lua](wip/lua-port.md) is independent of the host recipe runner.

## TCC and the guest SDK

The normal image includes `app://tcc.pxe`; the shell resolves `tcc` to it.
The [recipe](../ports/tcc/README.md) builds TCC with the prebuilt Pyxis GCC and
exports the guest executable, target libtcc1, compiler-private headers, licenses
and ordered patch provenance. No compiler-container rebuild is needed.

`app://sdk` contains:

- `usr/include`: shared libc/libpyxis/libterm and ABI/P1F headers.
- `usr/lib`: `crt0.o`, libc, libterm, libpyxis and target libgcc archives.
- `lib/tcc`: libtcc1 and private `stddef.h`, `stdarg.h`, `stdbool.h`, `float.h`.
- `share`: TLSF/musl/TCC licenses and notices, TCC source pin and patches, and
  the selected toolchain's hashes, patches and runtime licensing.
- `manifest.txt`: SDK build provenance plus the ports revision and dirty state.

From `home://`, compile a saved C source with `tcc hello.c -o hello.pxe`, then
launch `./hello.pxe`. TCC supports `-E`, ELF object output with `-c`, and static
P1F linking; the port notes list supported options and limits. The compiler uses
inherited read-only `app` and writable `home` grants and needs no launch authority.
The SDK packages target runtime files, not host compilers or a host converter.
GCC remains the compiler for the OS and maintained applications.

See the [edit/build/run walkthrough](edit-build-run.md) and
[TCC contract and limits](tcc.md).

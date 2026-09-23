# Ports and Kilo

Pyxis pins [pyxis-ports](https://git.internal/chronium/pyxis-ports) at `ports`.
Its host Lua runner fetches an exact upstream commit, applies ordered patches,
builds against the exported SDK and stages executables with their licenses.
Recipes are trusted build code. They do not modify the SDK or resolve/install
dependencies. Source pins, licenses, host/Pyxis dependencies, patch order and
output names live in each recipe's metadata; `ports.lua` only lists recipes.

## Building and packaging

```sh
git submodule update --init userspace ports
make ports                 # export SDK, build and stage Kilo
make image                 # also package userspace and assemble the ISO
make run CPUS=4
```

The host needs Lua 5.4 (`LUA=lua5.4` selects its executable), Git, GNU Make 4.3+,
GNU coreutils and the Pyxis target toolchain, plus network access to fetch
upstream source on a port rebuild. See [SDK/repository setup](sdk-and-repositories.md).
The build container includes Lua; the owner publishes container updates.

`make ports` exports the SDK before checking recipe and SDK dependencies.
Unchanged inputs reuse `build/ports/kilo/stage`. Changes rebuild in a fresh
`build/ports/kilo` work directory, replacing its fetched source and intermediate
outputs. Make source changes in the recipe/patches, not that disposable copy.
For port development with a separately managed work directory, use the
[standalone runner](../ports/README.md).

The normal boot archive includes Kilo at `app://kilo.pxe` and its BSD-2-Clause
license at `app://share/licenses/kilo/LICENSE`. The shell resolves the bare
command `kilo` to that executable. Packaging has an explicit entry list; it
does not recursively include other files left in build output. Kernel-only
`make`, `make sdk` and `make userspace` do not build ports.

The existing integrated workflow checks out both submodules with
`PYXIS_SOURCE_READ_TOKEN` and runs `make image`. Separate ports CI, SDK exchange
and dispatch orchestration remain future work; there is no package manager.

## Editing in Pyxis

Select CPU 1 with Alt+Right on a multicore boot. The shell starts at `home://`:

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
The next compiler investigation belongs to the [edit/build/run milestone](wip/edit-build-run.md);
[guest Lua](wip/lua-port.md) is independent of the host recipe runner.

## TCC target work in progress

The ports catalog also contains a [TCC recipe](../ports/tcc/README.md). It currently
builds a host-running compiler that emits Pyxis ELF objects, plus the target
support archive. Invoke it explicitly through the standalone runner; `make ports`
and the normal image still select Kilo only. It does not modify the consumed SDK
or require rebuilding the compiler container. Guest TCC, native P1F linking and
boot-archive packaging follow the [TCC task list](wip/tcc-port.md).

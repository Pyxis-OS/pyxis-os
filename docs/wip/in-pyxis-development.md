# Developing inside Pyxis

Status: **experiment, recorded 2026-10-05.** The program written here is the
owner's own project, not a Pyxis milestone. The tooling tasks below start only
when the owner says so; nothing here authorizes code or placeholder APIs.

## Goal

Write a program on an installed Pyxis system using only what Pyxis provides:
edit with [vi](../userland/vi.md), build with [TCC](../userland/tcc.md), and
read documentation offline in [Links](../userland/links.md).

The first program is a small, interpreted Java SE 8 virtual machine in C.
This document covers only the Pyxis side: the tools and configuration that the
work needs.

## Setup

- **System:** the installed Pyxis on the ThinkPad's USB stick, not the live image.
  See the [installer](../userland/installer.md).
- **Documentation:** the Java SE 8 Virtual Machine Specification as HTML, about
  400 linked pages, read with Links from `system://`.
- **Class files:** compiled on a host with `javac --release 8`. Pyxis has no Java
  compiler. Class files arrive over HTTP today, for example
  `cat http://HOST:PORT/Foo.class > Foo.class` from a host file server. Later
  they can arrive through [remote file transfer](remote-file-transfer.md).

## Proposed target

This is a proposal, not agreed. Run a small terminal game, such as Snake,
compiled by host `javac`. It would use a minimal class library written for this
JVM and one native console class. "Hello, world" through a native print method
comes first.

## Tooling

### 1. BusyBox tar

- [x] BusyBox `tar`, without compression: [implemented behavior and validation](../userland/tar.md).

- **Use:** pack the HTML documentation on a host as a plain `.tar` file. Then
  fetch it with `cat https://HOST/jvms.tar > jvms.tar`, or over HTTP from a host
  file server, and run `tar xf jvms.tar` in `system://`.
- **Port:** it extends the existing [BusyBox port](../../ports/busybox/README.md)
  and shares its support library with vi and `less`.
- **Scope:** archive members are regular files and directories. Hard and
  symbolic links, devices, absolute paths and `..` components are refused.
  Extraction ignores owners, permissions and timestamps; creation writes
  uid/gid zero, 0644/0755 modes and mtime zero (owner decision, 2026-10-06).
- **Not now:** `gzip`, `unzip` and zlib wait until jars are needed. At that
  point, the BusyBox applets would cover the commands and zlib the library. See
  [application ports](application-ports.md).

### 2. Extra spaces on the installed system

[Boot init](../userland/init.md#boot-configuration) reads `system://config/boot.lua` on
installed boots, so extra spaces need no ESP edit and survive Update. The
existing archive inits receive `system://` from that configuration instead of
mounting it, so the two new inits this section once proposed are not needed:

```lua
return {
  spaces = {
    { name = "docs", title = "Docs", init = "boot://init-readonly",
      roots = { system = "read-only" } },
    { name = "remote", title = "Remote", init = "boot://init-remote",
      roots = { system = "read-write" } },
  },
}
```

The default `pyxis` space stays the network owner; `init-remote` waits for its
address.

- **One pool instance.** A second mount of the same partition reuses the open
  pool (`kernel/fs/npfs.c`), so every space shares one filesystem instance and
  cache. Files unpacked in the main space appear in the read-only space at once.
- **Remote exposure.** The [remote terminal](../userland/remote-terminal.md) has
  no authentication or encryption. Anyone on the LAN who can reach port 2323
  gets a shell with writable `system://`, so use it only on a trusted network.
- **Finish when:** on the installed ThinkPad, with that override:
  - the read-only space reads `system://` and cannot write to it;
  - the remote space accepts `pyxis-remote` after the main space has
    configured the network;
  - a single-space installation behaves as before.

### 3. Moving files

- [x] [File transfer through the remote terminal](../userland/remote-terminal.md#explicit-file-transfer)
  uploads class files and downloads backups by explicit command. Drag-and-drop
  upload is implemented and awaits the owner's
  [GUI-drop check](remote-file-transfer.md#tasks).

### 4. Lua for build scripts

- [x] [Lua build runtime](../userland/lua.md): io/os, pure-Lua require and native
  `pyxis.run`, `pyxis.dir` and `pyxis.sha256`.

The owner writes the build tool in the JVM project; Pyxis supplies its runtime
only. Agreed 2026-10-06/07 and implemented with the following bounded contracts:

- Programs start from an argument list through the native launch API.
  `os.execute` and `io.popen` remain absent. Normal exit returns its actual
  integer status; launch/wait failures, faults and termination raise errors.
  Interruption remains process-only, so a child may outlive interrupted Lua.
- Launch delegation is opt-in per space with `launch = true`. Live Development
  and installed `pyxis` opt in, Read-only and default Remote do not. A separate
  LAUNCH-only grant travels through init/session/shell to ordinary foreground
  commands; no administrative launcher right is added.
- Live C standard streams are inherited, with closed streams omitted.
  Lua default-file rebinding remains local, and cursors/read-ahead stay private.
- Rebuild detection belongs to the owner's content-hashing build tool.
  No modification times are added to native files or `stat`.
- `require` uses exact `LUA_PATH` when set; otherwise the script directory then
  `boot://share/lua/`. Only preload and pure-Lua file searchers are installed.
- `os.tmpname` reserves an exclusive empty `tmp://` file; the caller removes it.
  `io.tmpfile` creates and unlinks a real file immediately. `file:setvbuf` stays
  absent. No stale temporary names are automatically deleted.
- `os.time()` reads wall time; calendar tables are rejected. `os.date` uses
  real C-locale `strftime` and actual UTC/TZif designations. `os.clock`,
  `os.setlocale` and dynamic modules remain absent.

[Runtime limits](../technical-debt.md#lua-build-runtime-limits) record the
consequences and revisit points. The port's PSA SHA-256 adapter consumes the
configured Mbed TLS development prefix; embedding liblua stays independent
of the native bridge and does not open these libraries in configuration files.

## Out of scope

A JIT, a Java compiler on Pyxis, porting a Java class library, and compression
before jars are needed. The build tool is the owner's own code, not Pyxis work.

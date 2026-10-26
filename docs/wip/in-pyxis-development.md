# Developing inside Pyxis

Status: **experiment, recorded 2026-10-05.** The program written here is the
owner's own project, not a Pyxis milestone. The remaining tooling task below starts
only when the owner says so; nothing here authorizes code or placeholder APIs.

## Goal

Write a program on an installed Pyxis system using only what Pyxis provides:
edit with [vi](../userland/vi.md), build with [TCC](../userland/tcc.md) (retired
since; hosted Clang now), and
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
  they can arrive through [remote file transfer](../userland/remote-terminal.md).

## Proposed target

This is a proposal, not agreed. Run a small terminal game, such as Snake,
compiled by host `javac`. It would use a minimal class library written for this
JVM and one native console class. "Hello, world" through a native print method
comes first.

## Tooling

Delivered: BusyBox `tar` without compression
([reference](../userland/tar.md); `gzip`, `unzip` and zlib wait until jars are
needed, see [application ports](application-ports.md)), explicit
[file transfer through the remote terminal](../userland/remote-terminal.md#explicit-file-transfer)
(drag-and-drop upload included),
and the [Lua build runtime](../userland/lua.md) for the owner's build scripts
(its limits are in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1471)).
The build tool is the owner's own code in the JVM project; Pyxis supplies only
the runtime.

### Extra spaces on the installed system

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

## Out of scope

A JIT, a Java compiler on Pyxis, porting a Java class library, and compression
before jars are needed. The build tool is the owner's own code, not Pyxis work.

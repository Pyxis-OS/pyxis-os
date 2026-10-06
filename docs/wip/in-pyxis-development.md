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

**Agreed 2026-10-05:** BusyBox `tar`, without compression.

- **Use:** pack the HTML documentation on a host as a plain `.tar` file. Then
  fetch it with `cat https://HOST/jvms.tar > jvms.tar`, or over HTTP from a host
  file server, and run `tar xf jvms.tar` in `system://`.
- **Port:** it extends the existing [BusyBox port](../../ports/busybox/README.md)
  and shares its support library with vi and `less`.
- **Scope:** archive members are regular files and directories. Hard and
  symbolic links, devices, absolute paths and `..` components are refused.
  Which metadata (owner, mode, time) is ignored is settled in the port's
  investigation.
- **Not now:** `gzip`, `unzip` and zlib wait until jars are needed. At that
  point, the BusyBox applets would cover the commands and zlib the library. See
  [application ports](application-ports.md).

### 2. Extra spaces on the installed system

[Boot init](system-layout.md#tasks) reads `system://config/boot.lua` on
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

[Remote file transfer](remote-file-transfer.md) covers uploading class files and
downloading backups of the work. It is accepted and not yet started.

### 4. Lua for build scripts

**Agreed 2026-10-06.** The owner writes the build tool, in Lua, as part of this
experiment; it lives in the JVM project until it grows further. Pyxis supplies
only the runtime underneath, as a small milestone after the
[system layout](system-layout.md):

1. **Lua's `io` and `os` libraries and pure-Lua `require`,** on libc. The gaps
   found by the earlier [io/os audit](later-os-directions.md#lua-follow-ups)
   are filled in libc, or the affected function stays absent. For example,
   `os.clock` needs process CPU time, not wall time, and there are no successful
   stubs.
2. **A small native Pyxis module** for what libc does not cover:
   - running a program from an argument list and waiting for its exit status,
     with the same explicit grants the shell would give it;
   - listing a directory;
   - hashing a file.

Decisions:

- **Starting programs** uses that argument-list module, not a standard
  `system()` through the shell, so `os.execute` stays absent.
- **Rebuild detection** compares content hashes of inputs and commands. No
  modification times are added to the native filesystem or `stat`.
- **Timing:** its own milestone after the system layout, in the order above.

## Out of scope

A JIT, a Java compiler on Pyxis, porting a Java class library, and compression
before jars are needed. The build tool is the owner's own code, not Pyxis work.

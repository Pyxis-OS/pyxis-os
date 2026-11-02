# Developing inside Pyxis: setup record

Chronium's experiment, begun 2026-10-05, uses an installed ThinkPad system to
write a small interpreted Java SE 8 virtual machine in C. The JVM is his own
project; this record does not claim it is implemented or qualified by Pyxis.

The original setup used vi, TCC and offline Java specification pages. TCC is
retired; current development uses [hosted Clang and LLD](../hosted-tools.md),
[vi](../../userland/vi.md) or [Neovim](../../userland/neovim.md), and
[Links](../../userland/links.md) for local documentation. Host-compiled Java
class files can arrive through [Remote file transfer](../../userland/remote-terminal.md#explicit-file-transfer).
The delivered [tar](../../userland/tar.md) and [Lua build runtime](../../userland/lua.md)
supply the archive and script tooling; the build scripts belong to the JVM
project. Lua's limits remain [separate](https://git.internal/PyxisOS/pyxis-os/issues/1471).

Installed boot configuration can declare extra spaces using the existing
[init configuration](../../userland/init.md#boot-configuration) and
[space editor](../../userland/spaces.md). It delegates each root explicitly;
extra spaces need no separate ESP init or second independent pool. Network and
Remote authority follow the current configuration contracts, not this historical
experiment's original network-owner assumption.

The installed extra-space read-only/Remote check remains
[unrun](https://git.internal/PyxisOS/pyxis-os/issues/1705). Tool delivery is not evidence that a particular
installed configuration preserved its restrictions or reached Remote. The check
must verify a read-only space cannot write, the Remote space connects over the
selected interface, and the normal single-space setup still works.

No JIT, Java compiler, class library or JVM application result is established by
this setup record. The proposed terminal-game target remains chronium's project.

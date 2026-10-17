# Everyday gaps

Status: **running list, started 2026-10-07.** Small things that are missing or
awkward during real use of Pyxis. An entry here is a reminder, not a decision:
each one still needs a place in a milestone and the usual discussion before work
starts. Add a line when something is noticed, and remove it when it is done.

Commands follow the [ports and native commands](../development/ports.md#ports-and-native-commands)
boundary: anything that shows or changes a Pyxis concept is native, and familiar
text tools may be ports.

| Gap | Noticed | Likely kind | Notes |
| --- | --- | --- | --- |
| User command directory | 2026-10-17, owner, Clang in `home://toolchain/clang` | Native (shell lookup) | Bare names search only `bin://`, then the rescue set; there is no PATH. A plain directory such as `home://bin/` searched after `bin://` would make installing your own programs a copy. Today: `cc=home://toolchain/clang/payload/clang.pxb`, then `$cc file.c -o file.pxe`. |
| Script arguments | 2026-10-17, owner, a `cc.pxs` wrapper | Native (shell) | Scripts cannot use their own arguments (no positional expansion), so a wrapper cannot forward options. |
| Shell startup file | 2026-10-17 | Native (shell) | No per-user startup file, so variables such as `cc` must be set in each session. |
| xfer inside a remote mux pane | 2026-10-12, owner, during JVM work | Native (remote client + mux) | Refused by design (`PYXIS_REMOTE_MUX=1`, see [remote-terminal.md](../userland/remote-terminal.md#remote-multiplexer)); workaround: a separate non-mux connection. Pane-aware transfer is the [initial remote multiplexer limits](../technical-debt.md#initial-remote-multiplexer-limits) debt item; owner: no rush. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.
The [multiplexer](../userland/multiplexer.md) now supports splitting in the
focused root shell's cwd and Ctrl+B digit focus (owner requests, 2026-10-11).

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](installer-tui.md#configuration-checker).

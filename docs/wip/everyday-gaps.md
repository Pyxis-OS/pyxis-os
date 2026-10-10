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
| Reboot at the end of installation | 2026-10-07, owner, after a successful native install | Native, in the [installer](../userland/installer.md) | The installer could finish with "press Enter to restart". `init-install` receives the kernel's `power` resource but does not forward it to the installer, so this needs a small authority decision: forward restart only. |
| Moving directories | 2026-10-10, owner, after `mv` learned to move into a directory | Native, with libc `rename` | `mv` moves files only; libc `rename` and the native rename are file-only. A directory move needs an agreed native contract first, with no copy-and-delete fallback. |
| Tab completion of arguments and paths | 2026-10-10, owner | Native, in the shell | The shell completes command names only. Paths need the caller's roots and directory handles, and a decision on quoting names with spaces. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](boot-configuration-checker.md).

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
| Moving directories | 2026-10-10, owner, after `mv` learned to move into a directory | Native, with libc `rename` | `mv` moves files only; libc `rename` and the native rename are file-only. A directory move needs an agreed native contract first, with no copy-and-delete fallback. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](boot-configuration-checker.md).

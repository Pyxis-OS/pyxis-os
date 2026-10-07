# Everyday gaps

Status: **running list, started 2026-10-07.** Small things that were missing or
awkward during real use of Pyxis. An entry here is a reminder, not a decision:
each one still needs a place in a milestone and the usual discussion before work
starts. Add a line when something is noticed, and remove it when it is done.

Commands follow the [ports and native commands](../development/ports.md#ports-and-native-commands)
boundary: anything that shows or changes a Pyxis concept is native, and familiar
text tools may be ports.

| Gap | Noticed | Likely kind | Notes |
| --- | --- | --- | --- |
| `echo` | 2026-10-07, while testing `home://`: `echo hi > note.txt` reported `echo: Not found` | Native, a shell builtin or a small core command | `cat boot://share/hello.txt > FILE` was the workaround. |
| `ls` options | 2026-10-07: `ls -a` was taken as a path (`ls: -a: Not found`) | Native, extending the existing `ls` | `ls` accepts only paths. It already lists every entry, so `-a` itself is not needed; a long listing with sizes (`-l`) is the useful one. |
| Running shell scripts as commands | 2026-10-07, from the shell docs while planning the Lua runtime | Native, shell launch authority | Ordinary foreground commands receive no launcher, so a shell script launched as a command fails its resource check. The per-space `launch = true` setting planned with the Lua runtime should cover it. |
| Colored `ls` entries | 2026-10-07, owner request | Native, in the existing `ls` | Color entries by kind: directories, regular files, programs (`.pxe`, since Pyxis has no execute bit) and scripts with a `#!` line, or whatever else fits. Only when output is a terminal, never in pipes. |
| `cp` | 2026-10-07, while planning everyday commands: no copy command exists | Native, a small core command | `cat SRC > DST` is the workaround. Copies should work across roots. |

The agreed [everyday commands](everyday-commands.md) milestone picks up
`echo`, `cp` and a better `ls`.

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](boot-configuration-checker.md).

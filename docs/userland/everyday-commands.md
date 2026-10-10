# Everyday commands

The boot image packages native `echo`, `cp`, `ls`, `tree` and `mv`. The installed
rescue archive retains `echo`, `cp`, `ls` and `mv`, so configuration text can be
written, files copied or moved and directories inspected while repairing the
system. See
[system layout](system-layout.md#programs) for `bin://` and rescue fallback.
The text filters `grep`, `wc`, `tail`, `sort` and `uniq` are sbase ports in the normal image.

| Command | Behavior | Reference |
| --- | --- | --- |
| `echo [-n] [ARG...]` | Space-separated arguments and a newline; an exact first `-n` suppresses the newline. No escape processing. | [Shell quoting and streams](shell.md#commands-and-quoting) |
| `cp [--] SOURCE... DESTINATION` | Files within or across roots; several sources require an existing destination directory. Exclusive sibling staging followed by replacement rename. | [cp authority and recovery limits](cp.md) |
| `ls [-1l] [--] [DIRECTORY...]` | Byte-sorted names, terminal columns and kind colors; plain names in pipes/files. `-l` adds kinds and sizes, with no timestamps. | [ls options and limits](ls.md) |
| `tree [--ascii] [-L N] [--] [DIRECTORY...]` | Recursive listing with UTF-8 box branches (`--ascii` for byte-oriented consumers), sorted and colored like `ls`; `-L` limits the depth. | [tree](tree.md) |
| `mv [--] SOURCE... DESTINATION` | Renames files, or moves them into a destination directory. Files only. | [mv](mv.md) |
| `grep [-EFHchilnqrsvwx] [-e PATTERN] [-f FILE] [PATTERN] [FILE...]` | Prints matching lines; `-r` searches directories through the native listing. Status 0 selected, 1 none, 2 error. An sbase port, not native. | [grep](grep.md) |

```text
echo hi > home://note.txt
cp home://note.txt home://copy.txt
ls -l home://
ls boot:// | less
grep -rn TODO home://src
```

## Shell scripts as commands

A file starting with `#!boot://shell.pxe` can run as a foreground command in a
space configured with `launch = true`. The packaged Development and installed
`pyxis` profiles opt in; Read-only and Remote do not. The shell delegates
LAUNCH alone, while the interpreter reads the held script file and uses its
explicit streams, memory and roots. No execute bit is required.

This delegation reaches one ordinary foreground command: the script receives
`launcher`, but not `child_launcher`. Its native commands can run, while another
ordinary shell script launched from it fails the required-resource check.
Background commands receive no launcher. See [script mode](shell.md#script-mode),
[startup authority](shell.md#startup-and-child-authority) and
[script launch](script-launch.md) for the resource and parsing contracts.

## Validation

Ordinary image builds and manual nested QEMU/KVM checks on 2026-10-07 used q35,
four CPUs, 512 MiB, OVMF and VirtIO network/random devices. Command-specific
copy/listing checks are recorded in [cp](cp.md#validation) and
[ls](ls.md#validation).

A foreground shebang script ran `echo` and `cp` in a Remote space with a temporary
`launch = true` validation override; both files contained `script-ok` and the
script exited zero. With the packaged Remote policy, the same script failed its
required-resource check and created neither file. An ordinary nested script
failed too, matching the one-step delegation limit. The override was restored
before publication. The guest rescue manifest contained both `echo.pxe` and
`cp.pxe`. Installed rescue boot and owner-run ThinkPad checks remain unperformed.

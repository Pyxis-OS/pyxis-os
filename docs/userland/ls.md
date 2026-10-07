# Listing directories

The native `ls` command lists the current working directory or explicit
directory paths. It includes dot names and sorts by unsigned byte order.
Directories keep their trailing `/`; multiple operands have path headings.

```text
ls boot://
ls -1 home://
ls -l
ls boot:// | cat
ls boot:// | less
ls boot:// > home://listing.txt
ls -- -directory
```

## Presentation and options

When stdout is a console, names appear in columns fitted to its reported
width. Read across each row, then continue with the next row. The actual
stdout binding controls this choice; a separate named terminal grant cannot
make redirected output colored.

| Kind | Identification | Terminal foreground |
| --- | --- | --- |
| directory | Directory enumeration | Blue |
| program | File name ends in `.pxe` | Green |
| script | Other file starts with the two bytes `#!` | Cyan |
| file | Other regular file, or unreadable script prefix | Default |

The suffix identifies programs without validating their executable contents.
Script classification checks the prefix, not whether its interpreter exists
or its full shebang is valid.

`-1` forces one name per line and keeps terminal colors. `-l` prints a kind
label, right-aligned byte size and name per line. Directory sizes are `-`;
unavailable file sizes are `?`. Long format overrides columns even when `-1`
is also supplied. There are no modification times, Unix modes or owner fields.
Options can be combined (`-l1`) or appear between operands; `--` ends option
parsing. Unknown options report usage and fail before listing anything.

In a pipe or file, names are literal and colors are absent. Plain listings
have one name per line; `-l` adds its requested details. Terminal names and
headings replace control and non-ASCII bytes with `?`, one cell per byte,
so names cannot change the style or column layout. Missing terminal geometry
uses one name per line. A name wider than the terminal remains complete and
wraps normally. Unicode column widths and filename quoting are absent.

## Authority, lifetime and failures

Plain piped/redirected output uses only directory enumeration and opens no
child files. Terminal coloring and long format also request lookup/file-read
rights from the caller's existing grants. No write or mutation authority is
requested. Each child is opened with READ alone and closed before the next;
script classification reads at most two bytes. File details use the same held
directory that produced the names, and all owned handles close before output.

If reads were withheld or fail, enumerated names remain visible. An unreadable
script prefix uses regular-file color; `.pxe` still identifies a program by
name. Unavailable sizes under `-l` produce `?`, diagnostics and failure status.
A file size can remain available even when its content cannot be read.

Sorting collects one directory's entries and name bytes on the heap. Memory
exhaustion or a detected directory change abandons that listing and reports
failure; there is no automatic restart or silent truncation. Enumeration and
subsequent size/prefix reads remain live observations, not a filesystem snapshot.
Other directory operands continue after listing or metadata failures; a stdout
or stderr error stops processing. Output and handle-cleanup errors fail the
command. File operands, recursion and other options are absent.

In a [foreground pipeline](shell.md#foreground-pipelines), ls reports its own
stage status; the final stage determines the pipeline's result. For example,
`ls -l host://restricted | cat` can finish with pipeline status 0 even when ls
reports unavailable sizes and status 1. Reader closure fails ls if a subsequent
write observes it; already completed output does not turn into a later error.

## Validation

The 2026-10-07 full source image build used the existing GCC builder and
`make -j16 image`. The changed ls sources built without warnings; unchanged
third-party ports emitted their existing warnings. The local container used
the host's installed Kconfig Python modules. No compiler rebuild was needed.

Manual QEMU/KVM checks used a nested VM, q35, four CPUs, 512 MiB, OVMF,
virtio-net/rng and a temporary virtio-fs export, without disks. At baseline
Pyxis `29a1777` / userland `05326ec`, `ls boot://` printed 59 rows at 80
columns. The same 59 names after this change occupied 15 rows at 80 columns
and 7 rows at 160 columns. This is output-density evidence, not a timing or
physical-host performance claim.

RAM/HOST listings covered byte-order sorting, dot names, all four colored
kinds, zero/one-byte files, a 180-byte name, control/UTF-8 names, narrow columns,
`-1`, `-l`, combined options, `--`, relative/multiple directories, missing
directories and usage errors. A HOST directory with denied child lookup
retained its names: ordinary ls returned 0 with file-color fallback; `-l`
printed unknown sizes, diagnostics and status 1. Native directory-grant
attenuation, allocation failure and detected concurrent change were inspected
in code, without injected failures.

Piped output was plain, and redirect/pipe versions of the boot listing had
identical SHA-256 hashes. `ls boot:// | less` paged and exited normally.
GDB observed a script probe at offset 0 with capacity 2, followed by the
kernel's READ operation on an initrd FILE with rights `0x1` (READ alone).
Disk-backed directories and owner-run ThinkPad usage were not exercised.

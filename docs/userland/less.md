# less

BusyBox less is packaged at `bin://less.pxe`, with the shared GPL-2.0-only
license at `boot://share/licenses/busybox/LICENSE`. The shell resolves `less`
to it. Its [recipe notes](../../ports/busybox/README.md#less) identify the source,
feature selection and patch.

```text
less boot://share/hwdata/pci.ids
ls boot:// | less
cat system://notes.txt | less
less < tmp://notes.txt
```

Content uses libc file or stdin reads. Keys and drawing use the named
`input`/`output` console grants through libterm, independently of a pipe or file
stdin. The [shell's final-stage rule](shell.md#foreground-pipelines) supplies
READ alone for named input when stdin is a pipe/file and stdout is a console;
it adds no interrupt-arming, keyboard or pointer right. `cat | less` has two
console readers and is unsupported. Choose a producer that does not read the
console, a named file, or redirected input.

Space/Page Down and `b`/Page Up move pages. Arrows or `j`/`k` move lines;
`g`/`G` or Home/End select the beginning/end. Counts select line/page positions.
`/` and `?` search forward/backward, and `n`/`N` select later/earlier matches.
Search uses BRE patterns with matching text highlighted; `-I` enables ASCII
case folding. For example, `/\.pxe$` finds program-name suffixes in
`ls boot:// | less`. Malformed patterns report the libc regex error. Changing files
clears search state. `:n`, `:p` and `E` select another file.

The pager holds [Ctrl+C passthrough](foreground-interruption.md) throughout its
session; Ctrl+C is input and `q` exits. `-N` adds line numbers, `-m`/`-M` show
extended status, `-E` exits on reaching the end and `-F` exits when the file fits
on the first screen. `-~` suppresses the end-of-file markers.

## Limits

Screen dimensions are measured at startup, and display is ASCII. Read display
lines remain in memory for backward paging, up to the configured 9,999,999-line
limit; excess input and allocation failure report errors. Content reads block
when filling a new page or searching beyond cached input. Cached navigation
needs no further read, but a stalled producer can delay keys during a refill.
There is no live refresh or nonblocking pipe readiness. Raw escape display,
shell commands, marks, bracket matching and log saving are disabled. Regex uses
[libc's ASCII-only classes/folding and pinned back-reference limits](libc-portability.md#regular-expressions-and-utf-8-conversion).

A missing named console grant fails explicitly. A downstream intermediate pager, or a pager with file/pipe stdin and non-console
stdout, receives no named input under the final-stage rule. The first stage's
original console-input rule is preserved; drawing uses named output even if
stdout was redirected. Read and terminal failures report failure; terminal input EOF
ends the pager. See [technical debt](../technical-debt.md#less-pager-limits).

## Validation

On 2026-10-07, the regex-enabled source image built and booted under the same
four-CPU QEMU configuration as the [vi comparison](vi.md#regex-validation-2026-10-07).
Framebuffer (1280×800) and remote (80×24) checks found and highlighted
`/^#include` in a 240-line file and `/\.pxe$` in `ls boot:// | less`.
Both reported `Missing ']'` for a malformed pattern and exited normally with
`q`. Remote `-I` searches also found the uppercase `FOOO` line; `:n` to a second
file cleared the prior regex and match list, and `n` left that file unchanged.
Nonmatching display lines rendered normally, without reading failed-match
offsets. These checks used scheme paths, files and pipe stdin; named-grant
policy was unchanged. No fixtures, screenshots or automation are committed.

On 2026-10-06, `make -j16 image` and interactive QEMU used nested KVM, four CPUs,
512 MiB, patched QEMU 10.2.2, virtio-net and a virtio-fs export. Framebuffer
(1280×800) and remote (100×29) checks covered file paging, Page Up/Down, literal
search and `q`; the packaged pipeline `ls boot:// | less` paged both ways and
survived Ctrl+C. A 240-line `cat | less` found line 160 and paged backward;
`less < FILE` also worked. Intermediate use and non-console stdout failed
explicitly. GDB observed pipe stdin, console stdout, named input with rights
`0x2` (READ), and no keyboard/pointer binding in the final pager.

A second boot used an npfs `system://` volume on a disposable `/dev/shm` GPT
image, with the unchanged installed boot configuration. Less opened
`system://pages.txt` and found line 160 on the framebuffer.

The port is 70,768 bytes in that build. Terminal error handling, cached-input
stall avoidance and clearing search state on file changes were also checked
by code inspection; failures were not injected.

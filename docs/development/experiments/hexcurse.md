# Hexcurse gate-4 qualification

Terminal UI gate 4, begun 2026-10-23 on Pyxis `7b11fb43` (ncursesw merged).
Native hardware qualification is gate 5; no native or PXE action here.
Raw build inputs/logs remain under `/shared/present/terminal-ui/hexcurse-probe/`.

## Source and baseline

[Hexcurse v1.60.0](https://github.com/LonnyGomes/hexcurse/tree/3ce605db4276310297f1cf871a38365650099f5d),
commit `3ce605db4276310297f1cf871a38365650099f5d` (annotated tag `012b7ac3`,
unsigned). The existing raw-github cache serves
`LonnyGomes/hexcurse/archive/refs/tags/v1.60.0.tar.gz`; SHA-256
`f6919e4a824ee354f003f0c42e4c4cef98a93aa7e3aa449caedd13f9a2db5530`.
Keep the GPL-2.0-or-later source notices and both upstream licence texts.

Builder `pyxis-llvm23.1.3-3d69e83`; ordinary `make -j16 sdk` and the existing
ncursesw recipe build pass. No compiler change. The old configure does not
recognize the target or its default P1F output filename. Direct SDK-only source
compilation with truthful target configuration identifies `bzero` and SIGWINCH;
VERSION is generated build metadata, not a runtime gap. Thus there is no runnable
unmodified application baseline. Add the common libc function, use a small
native build profile and replace signal resize with KEY_RESIZE. No always-on
kernel path changes.

## Implementation scope

Bundle MEMORY MANAGE and precise clock READ/SLEEP are declared; stdio, cwd and
exported file roots follow the existing bundle contract. No new launcher,
network factory or device authority. Package outside the boot archive.

Keep ordinary in-place save semantics. Validate retained file identities before
writing; aliases preserve the same file. Save As never falls back to the source.
Check read/seek/write/flush/sync/close before showing success, retain edits on
error, and abandon unsupported pending provider transactions. Failed writes can
partially change the selected target: no atomic replacement or concurrent-writer
exclusion is claimed. Open adopts its validated candidate only after success.

## Qualification

Q35/KVM nested QEMU, host CPU, 4 GiB RAM, modern VirtIO GPU/net, full live image;
repeated on one and four CPUs. Initial qualification used parent `7bf9de5d`,
userland `b8988c7`, ports `2e09068`. Rebased userland also includes the unrelated
package-upgrade RAM-publication change; final image/package smoke checks use the
PR heads. No native run or performance improvement is claimed.

Installed the real package from a private unsigned HTTP repository, with no
Hexcurse payload in the archive. Interactive checks:

- A 65,536-byte fixture (`bytes(range(256)) * 256`) retained its length and
  matched independently calculated SHA-256 after edits, Save, Undo/Save and
  Save As. Relative and scheme spellings of the same file preserved identity.
- Read-only boot-file Save, missing-parent Save As and HTTP transaction Save As
  produced visible errors. Retry to a valid new file retained edits; the HTTP
  server received no POST. Failed/cancelled Open retained the original buffer.
- Search, goto, help, dirty quit, framebuffer, plain Remote and remote-mux panes
  worked. Shrink below minimum geometry paused; enlargement retained pending
  edits, which saved with the expected hash. Split/focus/exit restored the shell.
- Piped stdin refused with a terminal-setup diagnostic and status 1. An ordinary
  SDK caller confirmed exclusive create, existing-name refusal without truncation,
  invalid-mode refusal and descriptor retention after rejected `fdopen(...,"wx")`.
- GDB inspected real saves without injected calls: source/target FILE identity,
  pending edits and the distinct-copy branch on four CPUs; matching device/inode
  and the non-copy branch on one CPU.

The RAM home store refuses hard-link creation, so no two-name hard-link runtime
claim is made here. Disk-error injection, concurrent writers, native behavior
and memory/latency measurements remain unqualified. Existing save checks report
real failures; the port supplies no transactional-save guarantee. Raw captures
and debugger records stay in `/shared/present/terminal-ui/hexcurse-qemu/`.

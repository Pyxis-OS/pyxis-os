# clangd in Neovim

Status: **milestone assigned by the owner, 2026-10-17.** The three decisions
below are accepted. Each task still starts with a short proposal of its open
details; nothing here authorizes code beyond the task being worked on.

## Goal

Edit C on Pyxis with language-server help: open a project such as
`home://test/src/main.c` in [Neovim](../userland/neovim.md) and get clangd's
diagnostics, hover, go-to-definition and completion, using the
[hosted Clang](../development/hosted-tools.md) toolchain installed under
`home://toolchain/clang`. Qualified on vela from the installed stick.

Today Neovim's port starts only its own embedded server: `jobstart`,
`vim.system`, `:!` and `:make` are refused, so no language server or external
build can run. The hosted payload contains Clang and LLD only.

## Accepted decisions (owner, 2026-10-17)

1. **String commands go through the Pyxis shell.** The shell gains
   `shell -c "LINE"`, and Neovim's `'shell'` uses it, so `:!`, `:make` and
   `system("...")` follow ordinary shell grammar and authority rules. Argument
   lists (`jobstart({...})`, `vim.system({...})`) launch directly.
2. **clangd runs with threads.** Build it with LLVM threading enabled; its
   single-threaded `-sync` mode is the fallback if threads cause problems. Task 2
   first records what clangd actually needs.
3. **One URI convention on both sides.** `home://test/src/main.c` ⇄
   `file:///home/test/src/main.c`: the root name becomes the first path segment,
   in Neovim's LSP client and in clangd alike, reversible for every root. Pyxis
   paths are never sent as URIs themselves, since `home://test/...` would parse
   `test` as a host.

## Tasks

- [x] **1. Neovim external jobs.** `jobstart`, `vim.system`, `:!`, `system()`
  and `:make` start native programs with stdin/stdout/stderr pipes, exit status
  and termination, through the existing native job and libuv spawn paths, plus
  `shell -c`. After this, compiler errors land in Neovim's quickfix list
  through `:make`, before clangd exists. Implemented: libuv spawn uses the
  shared lookup and prepares bundles; `shell -c`; `'shell'` defaults with
  `shellpipe=2>%s`; immediate termination, with stopped jobs reporting 143;
  temporary files in `tmp://` through libc `mkdtemp`. See
  [Neovim external jobs](../userland/neovim.md#external-jobs-and-shell-commands).
- [ ] **2. clangd build.** Extend the hosted payload producer so clangd
  (`clang-tools-extra`) ships as a bundle beside `clang.pxb` and `lld.pxb`, with
  its resource and sysroot roots, provenance and measured sizes.
- [ ] **3. URI mapping.** Implement decision 3 in Neovim's LSP URI conversion and
  in clangd's file URI scheme, with round-trip checks for every root a space
  holds. Details accepted 2026-10-17: the mapping is mechanical, including roots
  only clangd holds (`sysroot://include/stdio.h` is `file:///sysroot/include/stdio.h`),
  and task 4 adds the aliases that let Neovim open those; a buffer name
  `NAME://rest` is a native path only when Neovim holds root `NAME`; the fork
  change (pyxis-llvm `pyxis/clangd-uri`) merges only after task 4 decides
  whether clangd needs more fork changes, so one container rebuild covers both.
- [ ] **4. Neovim LSP setup.** Start clangd for C files from the toolchain path,
  with `compile_flags.txt` support for simple projects; document the setup.
- [ ] **5. Qualification on vela.** On the installed stick: diagnostics, hover,
  go-to-definition and completion on the owner's project, and responsiveness,
  with the measurements recorded.

## Related

- [Neovim follow-ups](later-os-directions.md#neovim-follow-ups) (LSP needs jobs
  first; compiler diagnostics before clangd is the owner's preference).
- [Native leaf jobs](../userland/native-jobs.md) and the
  [libuv port](../../ports/libuv/README.md) spawn rules.
- [Shell script mode](../userland/shell.md#script-mode).

# clangd in Neovim

Neovim on Pyxis edits C with clangd: diagnostics, hover, go-to-definition,
completion, signature help and semantic highlighting, from the
[hosted toolchain](../development/hosted-tools.md) provided by the `clang-hosted`
package through the shared live package-command view. The native qualification
below used the earlier manual publication path on vela, 2026-10-18. Package delivery
and a headless clangd session were qualified separately in
[QEMU](../development/experiments/packages-slice1/README.md#hosted-toolchain-package-2026-10-20);
the owner also ran the installed package on vela (2026-10-20), where clangd worked in
Neovim and `clang` compiled and ran a C program.

## Setup

1. Configure a network repository with [`pkg`](../interfaces/packages.md#package-client)
   and run `pkg install clang-hosted`. The package declares command exports for
   its tool bundles, including `clang`, `lld` and `clangd`. Existing spaces see
   installed commands through their live package-command entry; no new space or
   per-program catalog grant is required. Live installs last until reboot, with
   no archive fallback. The default launch reports when the package is absent.
2. Add `vim.lsp.enable('clangd')` to `home://.config/nvim/init.lua`, or copy the
   [example `init.lua`](neovim.md#language-servers), `boot://share/examples/nvim/init.lua`,
   from a `DISTRIBUTION=full` development image, which also turns on completion.
   The default core archive omits the example. With a configured writable
   `home://`, create a missing `init.lua` with the enable call, or run `:lua vim.lsp.enable('clangd')` in the session.
3. For a project whose headers are not beside the files that include them, add
   `compile_flags.txt` at its root, for example `-std=c23`, `-Wall`, `-Iinclude`,
   one per line.

For completion as you type through
`vim.lsp.completion.enable(true, client.id, bufnr, { autotrigger = true })`, set:

```lua
-- Show the menu and documentation without inserting a candidate while typing.
vim.o.completeopt = 'menu,menuone,noinsert,popup'
```

The command, flags, root detection, `compile_flags.txt` rules and default keys
are in [Neovim language servers](neovim.md#language-servers).

## How it fits together

- **Jobs.** Neovim starts native programs through `jobstart`, `vim.system`, `:!`
  and `:make`; string commands run through `shell -c`
  ([external jobs](neovim.md#external-jobs-and-shell-commands)). A server runs as
  an attached child and stops with the editor. Bare `clangd` uses the same
  [command list](../interfaces/command-search.md) as shell and Lua launches.
- **The clangd bundle.** The hosted payload ships `clangd.pxb`, built with LLVM
  threads, with its own read-only `clang_resources://` root and the editor's
  `sdk://`, of the package set captured when the editor started
  ([clangd](../development/hosted-tools.md#clangd)).
- **URIs.** `ROOT://rest` is `file:///ROOT/rest` on both sides, so a native path
  is never sent as a URI ([file URIs](neovim.md#file-uris)). Headers clangd
  reports under its own roots open as read-only views
  ([server roots](neovim.md#server-roots)). The editor retains resources from
  the actual admitted server selection; a later package switch cannot replace
  an open header buffer's view.
- **No preamble.** Pyxis builds none, so every edit, completion and signature
  help request parses the whole file ([completion and highlighting](neovim.md#completion-and-highlighting)).
- **Display.** Semantic tokens are drawn above Tree-sitter, and diagnostics are
  underlined by the `TERM=pyxis` profile.

## Native qualification

Historical manual publication under `home://toolchain/clang/payload/`:
vela (Core Ultra 7 356H), installed stick, Neovim with the shipped flags
(`--resource-dir=clang_resources:// -j=4 --background-index=false
--pch-storage=memory`), payload `hosted-801c7d2f` (main `a29372f8`, fork
`3d69e83`). Each value is the median of five samples (min–max) from the
[timing script](#timing-script). The reference project is `pointer-min.c` with
the cursor on `display_submit`, three runs; the owner's project is private.

| Measurement | Reference project | Owner's project |
| --- | --- | --- |
| Start to first diagnostics | 506, 570, 550 ms | 523 ms |
| Hover | 1 ms | 1 ms (1–6) |
| Go-to-definition | 0 ms | 0 ms (0–1) |
| Completion at a symbol | 87, 87, 88 ms | 78 ms (72–122) |
| Edit to fresh diagnostics | 147, 148, 146 ms | 142 ms (135–255) |

On the reference project, member completion after `p.` offered the struct's two
fields in 40 ms (38–62), and signature help in `add_numbers(` gave
`add_numbers(int left, int right) -> int` in 40 ms (36–53). In the owner's
project, field completion, signature help, diagnostic underlines, `Ctrl-]` and
the semantic colour toggle (`vim.lsp.semantic_tokens.enable`) passed.

An earlier payload (`hosted-255dfa42`, fork `abc00d0`) completed from words in
the file only, because completion fell back without a preamble: 9–21 ms but
unusable. With `-sync` instead of `-j=4`, its reference numbers were 476–561,
1–2, 0, 5–9 and 139–146 ms, so threads made no measurable difference on this
project. Once, the semantic token toggle reported that the function could not be
found; after a reboot it worked and the error did not recur.

### Timing script

`clangd-time.lua` restarts clangd for the current buffer, then times start to
first diagnostics and five samples each of hover, definition and completion at
the cursor and of a one-line edit to the next diagnostics. It prints one line of
times, counts and clangd's flags, without file names or contents, and leaves the
buffer unchanged. It shipped with the owner's run sheet and is not part of the
image or this repository.

## Limits

- Every edit, completion and signature help request reparses the file and its
  headers, so latency grows with the includes ([clangd without a preamble](https://git.internal/PyxisOS/pyxis-os/issues/1382)).
- clangd runs four workers because libc reports no processor count, has no user
  configuration or index cache, and has no background index
  ([clangd on Pyxis limits](https://git.internal/PyxisOS/pyxis-os/issues/1383)).
- Server root views are an editor guard, not a read-only grant
  ([Neovim language-server limits](https://git.internal/PyxisOS/pyxis-os/issues/1331)).
- `Ctrl-]` closes a `pyxis-remote` session; use `:lua vim.lsp.buf.definition()`
  over Remote.
- Nothing writes `compile_flags.txt` or `compile_commands.json`; this remains an
  [everyday gap](../wip/everyday-gaps.md). Package retention and cleanup follow
  the [package client](../interfaces/packages.md#package-client).

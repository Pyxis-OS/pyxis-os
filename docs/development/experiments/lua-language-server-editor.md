# LuaLS package and editor integration

Task 2 of [lua-language-server](../../wip/lua-language-server.md), 2026-10-25.
No native or full feature/capacity qualification is claimed here.

## Inputs and builds

Qualification code: parent `e8ea5e2341ef22cc93adcfdf742f8b79713afc86`, ports
`967c57966ac0e71b90ca84bc3b48a3a45f7fa761`; source pins match task 1.
SDK `db52da3e`, userland `b468f20c`, LLVM `3d69e83` as in the
[runtime record](lua-language-server-runtime.md). Ordinary approved-cache builds
of LuaLS, Neovim, libuv, Lua 5.1/LPeg/luv, utf8proc and tree-sitter pass. Genuine
current pkg/boot-init were built with native Lua 5.5/Mbed TLS/shared SHA-256.
No libc/kernel source or compiler/container change.

Standard `pxpkg.py pack --build build` packed the nvim and LuaLS recipes from
the clean code checkout, then `check` and unsigned local `publish` passed.
Both packages are `1+e8ea5e2341ef`, ABI `e8ea5e2341ef`: no ABI/version bypass.
Their uncompressed archives are 32,222,208 and 4,214,784 bytes respectively.
Neovim P1F is 7,808,990 bytes; LuaLS P1F is 818,762 bytes. Receipts carry hashes.
Later restacking includes unrelated Rust/LUS changes; selected Neovim/LuaLS
and dependency source scopes remain unchanged, as do reached libc/lib/include
sources between userland b468f20c and 67aa6220.

## QEMU observed behaviour

q35/KVM, CPU max, 4 vCPUs, 2 GiB, OVMF, modern VirtIO network, UTC RTC.
The dedicated image has kernel implementation `16cee762`, genuine current
pkg/boot-init and code-H `os-release`. It reuses 3035 unchanged utility files
from the public b364 image; this is not an exact-H whole-image rebuild.
ABI headers and kernel implementation scopes at 16cee and H match. Image SHA-256:
`d10358d42931391c5fdf2e2a391651463b3d3e63104d664d4ceec13b2b955f1f`.
Full mixed-component provenance is in the raw image receipt.

- Before install, bare `lua-language-server` reports Not found/launch_failed.
  Normal `pkg install lua-ls-local/nvim lua-ls-local/lua-language-server`, with
  its ordinary confirmation, activates the exact two package versions.
- The full live image intentionally searches its old archive editor first.
  `pkg://nvim/nvim.pxb` selects the installed editor; both app/runtime roots
  expose its real `lua_ls` config. LuaLS remains the **bare package command**;
  no absolute server path, JIT or fake-global substitution is used.
- The client attaches to a public single-file project using its directory.
  Effective server settings are `Lua 5.1` and `nvim_runtime://lua`; the actual
  held `_meta/api.gen.lua` is readable. Read-only masks are source-inspected,
  not established by a write probe.
- After observed loading progress reaches 141/141 files, completion at
  `local api_member = vim.api.` returns 101 callable API items. The source
  contains no API member name. `nvim_get_current_buf()` has function kind 3;
  resolve returns `function vim.api.nvim_get_current_buf() -> integer` and
  real buffer documentation. Null results during initial indexing were not
  mistaken for completed analysis.
- A nested file finds its nearest `.luarc.jsonc` root. Effective `Lua 5.5` and
  empty-library overrides are confirmed through the existing config query;
  the public `<const>` fixture returns its real document symbol.
- Normal client stop, repeated start/stop, and normal editor `qa!` with an
  active client leave the cache-instance directory empty. Editor and Remote
  statuses are 0; child numeric exit status during editor teardown was not
  separately observed. No heap-leak proof is claimed.

These are ordinary public fixtures. No private project, ROM, native/PXE action,
exploit-style probe, package lock bypass, permanent test or new benchmark tool.

## Existing editor startup comparison (nested)

Before code changes, built-in `--startuptime` used a fixed 118-byte Lua file,
headless `-u NONE -i NONE -n --noplugin`, scheduled normal quit and no LSP.
The cached image's Neovim/libuv/Lua51/utf8proc/tree-sitter recipe trees exactly
match the task baseline. Six archive-backed starts gave 24.820, 7.049, 6.915,
5.265, 7.678 and 10.368 ms; the first was the first editor invocation.

A second matched RAM-backed fixture uses the same cached P1F and input at the
same path/backing. Complete file inventories prove the after delta is only the
added `lsp/lua_ls.lua`; the module remains unloaded in this no-LSP workload.
One priming launch precedes five measured repeats on each side:

| NVIM STARTED | Before | After |
| --- | --- | --- |
| Median | 8.592 ms | 5.603 ms |
| Range | 6.272–9.991 ms | 5.482–6.037 ms |

No causal speedup is attributed: cache/host history differs and the inventory
read additionally warmed data before the after samples. This is a data-only
startup comparison, not a performance result for the freshly linked package,
LSP initialization or native hardware.

## Remaining qualification

Task 3 must measure actual message/framing/queue maxima and peak backing/thread
stacks on real workloads, select clear headroom, and then qualify native usage.
The provisional 16 MiB messages, 4 MiB LSP bodies, 256 items/channel and other
limits are not accepted defaults. Full decoded semantic tokens/rendering,
completion edits/parameters, typed diagnostic changes, navigation, larger/two-
project corpus and failure/capacity cases remain task 3.

Raw source/artifact receipts, commands, ordinary protocol logs and cleanup:
`/shared/present/lua-ls/task2/`, particularly `integration/runtime-qualification.md`,
`current-image/final-image-receipt.json` and `qemu/matched-comparison.md`.
All owned QEMU/HTTP/Remote helpers were stopped.

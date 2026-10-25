# LuaLS runtime closure

Task 1 of [lua-language-server](../../userland/lua-language-server.md), 2026-10-25.
This qualifies the manual runtime bundle, not the public package or Neovim
semantic-token/completion integration. No private project was opened.

## Inputs and build

- LuaLS 3.19.2 `b144c872f081373a893b55979142fed651e4425a`, bee
  `f55b6988e8d9564816a555118b0e76564ef9a283` with embedded Lua 5.5.0,
  LPegLabel `912b0b9e8641074408ffc2259e069b188e0c717b`; remaining licence
  references, archive hashes and all patches are in the ports recipe.
- SDK Pyxis `db52da3ebd123d9aa124f65b896df17d86ba6573`, userland
  `b468f20c39dc6b098467a761140a57918f142e7f`, FS
  `f3f22b6e07e7b38078ae86b414e430ffb0b4f9c2`; builder
  `pyxis-llvm23.1.3-3d69e83`. Ordinary `make -j8 sdk` completed after
  rebasing over shared SHA-256 main `41f1e4fc`; the compiler is unchanged.
- Ports `99b6fe8853fb1a57eff222b870b948bd53b453dd`, containing ports main
  `6520b079a62b28f56590e3d20e47b4dfcea24cb6`. The rebase adds no LuaLS
  runtime changes; staging also carries the new SDK shared-hash notices.
- Ordinary build: `lua ports/build.lua lua-language-server --sdk SDK --work NEW_DIR`,
  using only the approved raw-github cache. Static link and all declared staging
  outputs pass; no missing standard operation required a libc/kernel change.
  The compiler/container pin is unchanged.

The staged manual bundle has 350 files, **3,855,159 logical bytes** including
sources/notices. The current P1F is **818,634 bytes**, SHA-256
`8ec762fcdc0e24533c8df6657ca03d1b9ebae175a8aaa8e87f6a6150da9a9c14`.
Build paths appear in upstream diagnostics, so rebuilding in a different work
directory can change artifact bytes. SDK/provenance, complete selected licences
and covered adapter sources accompany the bundle.

## QEMU observed behaviour

Reused public qualification image, Pyxis `b364a5894b35`, SHA-256
`a6bdacfd24e6747d96e3a3871ec182d3d3dc8adbf8c6b041afd3e8d578c9ac13`;
q35, KVM, 4 CPUs, 2 GiB, OVMF, modern VirtIO network. Its public ABI sources
match the current task parent ABI (including main `41f1e4fc`). The guest kernel is the recorded reusable
image, not a claim of a new exact-parent-head boot. The unchanged stock
Neovim 0.12.5 bundle supplies real native PIPE stdio via headless `jobstart`; a
temporary Lua client sends public LSP messages. No Neovim configuration changes.

- `--version` reports 3.19.2 and `--help` reports the bounded profile.
- initialize returns real capabilities, including semantic-token legend and
  document-symbol support. All three formatting provider fields are absent;
  source inspection confirms no watched-file registration.
- After initialized and a pause, didOpen of a tiny authored public Lua file
  followed by documentSymbol returns its real variable, function and parameter.
  This exercises workspace initialization, parser and worker serialization.
- Console startup explicitly refuses the non-PIPE bindings with exit 1 and
  removes state. Ordinary temporary parent manifests separately withholding
  threads or randomness refuse child admission with EACCES and create no process.
- shutdown acknowledges, then a later exit notification succeeds while the
  client leaves stdin open. Child and client exit 0, Remote drains cleanly.
  Instance enumeration goes from one owned leaf to empty after exit.

## Limits

No native run, public package, API-aware completion, decoded semantic tokens or
full typed-diagnostic qualification is claimed. No fault injection, allocation
accounting proof, peak backing/thread-stack measurement or large-project
capacity qualification. Program-private budgets remain provisional. Those
feature/memory/native checks remain tasks 2–3.

No existing shared I/O or launch path changed and there was no prior Pyxis
LuaLS runtime for a matched A/B. Editor launch/edit baselines precede task2's
changes; this record contains no nested timing presented as native performance.
Raw builds, immutable source/artifact receipts and manual protocol captures:
`/shared/present/lua-ls/`, especially `sdk-refresh-build.log`, `runtime-current-build.log` and
`qemu/run-02/`. The earlier pre-SHA SDK checks are retained separately in
`qemu/run-01/` and are not substituted for this refreshed artifact.

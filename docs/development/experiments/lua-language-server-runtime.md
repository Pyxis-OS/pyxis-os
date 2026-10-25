# LuaLS runtime closure

Task 1 of [lua-language-server](../../wip/lua-language-server.md), 2026-10-25.
This qualifies the manual runtime bundle, not the public package or Neovim
semantic-token/completion integration. No private project was opened.

## Inputs and build

- LuaLS3.19.2 `b144c872f081373a893b55979142fed651e4425a`, bee
  `f55b6988e8d9564816a555118b0e76564ef9a283` with embedded Lua5.5.0,
  LPegLabel `912b0b9e8641074408ffc2259e069b188e0c717b`; remaining licence
  references, archive hashes and all patches are in the ports recipe.
- SDK Pyxis `22fd00759f9dda1e4bc6117d2a712e80edf0fa8f`, userland
  `8fe58dde758adf84f0ee8c8398c6d01c4b18c036`, FS
  `f3f22b6e07e7b38078ae86b414e430ffb0b4f9c2`; builder
  `pyxis-llvm23.1.3-3d69e83`. Reached userland `libc/include/lib` sources
  match parent main's pin `8a2ff95ee4a130118615f89e21916549d42d5cf5`.
- Ports `a69ac97fa0d2cecbd33e77c43c138d55ef67e27f`; runtime code/manifest/scripts
  match the QEMU-qualified `fb3504b57af1e61865308a79d2d8683a0c82b0a5`. Later
  changes are licence/provenance and qualification documentation only.
- Ordinary build: `lua ports/build.lua lua-language-server --sdk SDK --work NEW_DIR`,
  using only the approved raw-github cache. Static link and all declared staging
  outputs pass; no missing standard operation required a libc/kernel change.
  The compiler/container pin is unchanged.

The staged manual bundle has346 files, **3,819,991 logical bytes** including
sources/notices. The current P1F is **818,426 bytes**, SHA-256
`392e1f98338a9df2e162800a89800883da4304dfd80fe1f5e6151db346cc03d2`.
Build paths appear in upstream diagnostics, so rebuilding in a different work
directory can change artifact bytes. SDK/provenance, complete selected licences
and covered adapter sources accompany the bundle.

## QEMU observed behaviour

Reused public qualification image, Pyxis `b364a5894b35`, SHA-256
`a6bdacfd24e6747d96e3a3871ec182d3d3dc8adbf8c6b041afd3e8d578c9ac13`;
q35, KVM, 4 CPUs, 2 GiB, OVMF, modern VirtIO network. Its public ABI sources
match the task parent base `2446d3b8`. The guest kernel is the recorded reusable
image, not a claim of a new exact-parent-head boot. The unchanged stock
Neovim0.12.5 bundle supplies real native PIPE stdio via headless `jobstart`; a
temporary Lua client sends public LSP messages. No Neovim configuration changes.

- `--version` reports3.19.2 and `--help` reports the bounded profile.
- initialize returns real capabilities, including semantic-token legend and
  document-symbol support. All three formatting provider fields are absent;
  source inspection confirms no watched-file registration.
- After initialized and a pause, didOpen of a tiny authored public Lua file
  followed by documentSymbol returns its real variable, function and parameter.
  This exercises workspace initialization, parser and worker serialization.
- Console startup explicitly refuses the non-PIPE bindings with exit1 and
  removes state. Ordinary temporary parent manifests separately withholding
  threads or randomness refuse child admission with EACCES and create no process.
- shutdown acknowledges, then a later exit notification succeeds while the
  client leaves stdin open. Child and client exit0, Remote drains cleanly.
  Instance enumeration goes from one owned leaf to empty after exit.

## Limits

No native run, public package, API-aware completion, decoded semantic tokens or
full typed-diagnostic qualification is claimed. No fault injection, allocation
accounting proof, peak backing/thread-stack measurement or large-project
capacity qualification. Program-private budgets remain provisional. Those
feature/memory/native checks remain tasks2–3.

No existing shared I/O or launch path changed and there was no prior Pyxis
LuaLS runtime for a matched A/B. Editor launch/edit baselines precede task2's
changes; this record contains no nested timing presented as native performance.
Raw builds, immutable source/artifact receipts and manual protocol captures:
`/shared/present/lua-ls/`, especially `runtime-build-03.log` and
`qemu/run-01/runtime-parser-03.log`.

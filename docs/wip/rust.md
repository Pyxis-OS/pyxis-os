# Later Rust work

The [cross-toolchain reference](../development/rust-toolchain.md) documents
implemented core+alloc, the first native std profile and the optional hexyl
port. Their qualification does not establish support for arbitrary crates or
the work below.

The later port direction is tokei → ripgrep → bat → broot → xh → gitoxide →
Helix, then wgpu. These are future assignments. Crates selecting only Unix or
Windows need native branches and audited feature sets; do not enable a false
platform family or add successful unsupported stubs.

Hosted rustc/Cargo, unwinding and tier-3 upstreaming remain separate completion
points. The owner-maintained `pyxis` branch of PyxisOS/pyxis-rust is the source
for the eventual upstream target proposal, with chronium as maintainer.

Horizon has independent winit/wgpu, tokio, audio, gamepad/HID and wasmtime
requirements. Neither the tiny-skia checksum nor the first std profile supplies
those subsystems.

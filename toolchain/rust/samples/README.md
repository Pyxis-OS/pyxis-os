# Rust qualification samples

The native `rust-hello` and `rust-raster` binaries use `core` + `alloc` and
libc's C `main` startup. The small runtime belongs to these qualification
samples; it is not a general Rust platform library. Its FFI follows the SDK
headers, without a libc crate, a replacement entry point or another FS owner.

`rust-hello` checks Vec/String growth and drop, zeroed allocations and
grow/shrink reallocation at 16 KiB alignment, and a C `fma` call with f64
arguments and result, plus an initialized local-exec TLS variable. `rust-hello --panic` requests the negative qualification:
the runtime emits a fixed diagnostic and exits with status 101. Allocation
failure emits a fixed diagnostic and exits with status 102;
`rust-hello --allocation-failure` exercises it with a real oversized request
that libc refuses before backing allocation. These handlers
abort Rust execution without unwinding. They use caller-supplied stderr.

The raster programs share the same no_std scene crate and unchanged
tiny-skia 0.12.0 with only `no-std-float` enabled. The Linux reference uses
its ordinary std allocator; the native binary uses the libc-backed allocator.
FNV-1a covers all premultiplied RGBA bytes in the 96 × 64 pixmap. The pinned
nightly's Linux x86-64 release reference is:

```text
rust-raster: fnv1a64=d0dfb9f8f80d6c59 width=96 height=64
```

From this directory, use the pinned prefix prepared by the host installer:

```sh
export PATH=/absolute/pyxis-rust-nightly-2026-10-24/bin:$PATH
export CARGO_HOME=/absolute/path/to/private/cargo-cache
cargo fetch --locked
cargo run --frozen --release -p rust-raster-host
```

`.cargo/config.toml` replaces crates.io with the closed internal sparse proxy.
Fetch seeds the exact `Cargo.lock` graph; ordinary builds use `--frozen`.
`dependencies.json` records every registry archive's checked SHA-256,
upstream and approved cache URLs, source commit and notices. `licenses/`
contains the distributed notices from all nine locked crates, without changes
to crate sources. The workspace's own sources use the repository's MPL-2.0
license. No registry patch overlay is needed for this graph.

Native package manifest templates request only `memory/manage`. Standard
streams come from the caller, and no stack override, thread or network grant
is requested. The parent integration packages the programs with SDK,
compiler, lockfile and dependency/license provenance. A host checksum does
not establish native allocation, linking or launch behavior; those require
the separate Pyxis qualification.

# Hosted program build flags

Programs, ports and reusable SDK libraries compile as hosted GNU C23; C++ is
hosted too. The SDK Make fragment and CMake toolchain agree. This permits the
compiler's normal standard-library builtin lowering without selecting global
fast-math, changing errno/rounding policy or changing the x86-64 baseline.
Compiler-generated libcalls still use Pyxis's runtime. See the
[SDK reference](sdk.md) and [measured flag comparison](experiments/hosted-build-flags.md).

## Runtime boundary

Whole libc, its musl/TLSF closure, crt0 and pre-TLS native startup helpers remain
freestanding. Existing no-builtin and strict rounding controls remain: hosted
optimization must not turn a libc implementation into a recursive call to itself.
Host generators continue using native host tools. Header order is unchanged;
the SDK's integer headers precede compiler builtin headers.

The no-red-zone convention remains in the SDK, compiler driver and Rust target.
Deleting its redundant SDK spelling would not enable a red zone in the driver.
Enabling it requires a separate ABI decision and entry/debugger qualification;
hosted C does not make that promise. This flag delivery changes no runtime ABI
and requires no compiler-container rebuild.

## Ports and qualification

Mesa's Meson release profile explicitly selects O2 and removes conflicting
inherited optimization selectors from cross-file C/C++ arguments. O3 is a
comparison profile, not an assumed improvement or a generic SDK default.
Neovim no longer adds a duplicate freestanding-C option; its generators remain
host tools.

The delivered SDK and reached Mesa/SDL, editor, network/library and native-command
closures build. QEMU exercises shell/cat, hashing, headless Neovim editing and
OpenArena; its broad overlapping ImGui samples establish no stable speedup.
The [experiment](experiments/hosted-build-flags.md) preserves exact revisions,
prototype-versus-delivered observations and limits. A separately coordinated
[native matched comparison](https://git.internal/PyxisOS/pyxis-os/issues/1688)
remains open; source/SDK checks are not a native rendering result.

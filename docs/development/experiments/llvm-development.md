# LLVM development prefix qualification

2026-10-23, task 1 of the [Quake 3/Mesa track](../../wip/quake3-llvmpipe.md).
Runtime qualification build `8984a853` on main `9caeaea9`; userland `6e0c6840`, ports
`5e4fba64`, lwIP `d52ebd20`. SDK/current full image use those inputs. Compiler
container `pyxis-llvm23.1.3-3d69e83`; target libraries from development-only fork
`a63ce5822d3b5319a568b499dba8f3172633eaf9`. Installed compiler/hosted Clang
pins did not change. No kernel, libc or port adaptation was needed.

## Build and source closure

The original hosted pin `819c2b7` failed in ORC's unconditional socket header
and shared-library scanner's platform/stat/symlink assumptions. The narrow fork
refuses those features explicitly. A later compile exposed a missing standard
`<stddef.h>` in `llvm-c/ExecutionEngine.h`; the fork includes it. Both changed
ORC sources compile on Linux; the C header compiles alone on Linux and Pyxis.
The complete target closure builds with threads on, exceptions/RTTI off, X86
only and optional host dependencies off.

CMake's obsolete `engine` alias still adds a removed `LLVMjit` archive; the
manual target names concrete libraries instead. The config-tool's real engine
graph also needs Interpreter, and its X86 group needs X86TargetMCA. The
producer builds them rather than reporting missing archives as available. Its
filtered, target-derived component table excludes unavailable optional modules.
The upstream host tool and ordinary LLVM source remain separate from target
archives.

Export: 55 target archives, generated/public headers, host `llvm-config`, LLVM
and SDK notices plus original embedded Support licence material. Final prefix
identity `d88197f536d93706dd6cd9a63f57b466a72b6f51114a7a25e45c81ce07741998`.
The final notice export and SDK-directory overlap validation do not change
target code. Archives, headers and host tool after runtime qualification were checked
byte-for-byte against the qualified archives, headers and host tool.

## Discovery and target execution

Host Meson 1.7.0, ordinary config-tool method through a Pyxis cross file, finds
all ten required groups at version 23.1.3. Optional `coroutines` and `lto` are
reported missing. A relocated prefix returns its new include/library paths;
Meson discovery succeeds there too. A separate consumer compiles and links
using only the exported headers/archives and `llvm-config` flags, not source
or build-tree includes.

Q35/nested KVM, four max CPUs, 4 GiB, OVMF, VirtIO GPU/network/RNG, full image.
The native program runs through the ordinary Remote shell and declared bundle:

```text
LLVM_ORC_REFUSAL COMPLETE transport+shared-library=NOT_SUPPORTED
LLVM_OBJECT COMPLETE target=x86_64 bytes=776 execution=NONE
```

Both the build-tree target and independently linked exported-prefix consumer
passed. Native FILE readback yields identical 776-byte objects. Host readelf
identifies ELF64 little-endian x86-64 ET_REL, exporting `pyxis_answer`; objdump
shows `mov $42,%eax; ret`. No generated instructions were executed. The
exported-prefix consumer is 34,630,514 serialized bytes, 34,996,224 image backing
bytes, with an 8 MiB declared stack; these are not a runtime heap peak.

A debug ELF companion has byte-identical loadable segments. Hardware
breakpoints identify the native main and fwrite of the ELF bytes. Read-only
physical page-table inspection shows the object buffer present, writable,
user-accessible and NX. This is an ordinary emission/permission observation,
not an allocation-failure or heap-leak proof.

No matched runtime baseline exists for this additive prefix: it changes no
existing program, compiler, SDK or image path. No build duration or nested cost
is treated as a performance result. Physical-machine use, native hosted-Clang
compilation of the consumer, JIT execution and Mesa rendering remain unrun.
Records, commands and receipts: `/shared/present/quake3/llvm-development/`.

## Cached Mesa verification

Both 26.2.5 and 25.0.7 cached tarballs match the plan's SHA-256. Detached `.sig`
files verify with `57551DE15B968F6341C248F68D8E31AFC32428A6`; the key is in the
tagged release's maintainer-key file and its expected fingerprint was fixed in
the accepted plan. GPG's undefined personal trust is not a web-of-trust claim.
Sources came only from `repo.internal`; no Mesa code was vendored or changed.
Receipts: `/shared/present/quake3/source-verification/`.

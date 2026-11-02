# Libc capacity qualification

Initial before/after qualification, 2026-10-30: parent `b37debacd`, initial userland `ae6d946`,
changed userland `dbc08a1`, ports `a527526`. Compiler container
`pyxis-llvm23.1.3-8bdf136`, SDK/C++ runtime rebuilt, hosted C at the SDK's O2 and
no-red-zone convention. No compiler-container rebuild. Kernel source unchanged.

Local KVM/Q35 QEMU, host CPU model, 8 GiB, one and four CPUs, 1024x768 display,
stock live/network/session configuration with Remote beacon `wp6`. The archive
uses the existing sealed pxe-default contents for unchanged ancillary programs;
boot-init, session, shell/init-shell, Remote terminal, httpfs, textfs, echo, log,
allocbench and threadprobe were rebuilt against the selected SDK. Lua, Mbed TLS
and picohttpparser prefixes were rebuilt after a reused old Lua archive failed
linking. No stale prefix was used in successful qualification.

Existing commands: `threadprobe` lifecycle, tls, runtime, runtime-cxx,
runtime-mutex, runtime-once, runtime-limit; allocbench heap
`--size 64 --live 128 --rounds 4096`, mixed heap
`--size 4096 --live 128 --rounds 256 --mixed`, and pages
`--size 65536 --live 8 --rounds 100`. Thread consumers declared memory, threads
and clock grants; no call-site authority wrapper.

| Ordinary consumer check | Result |
| --- | --- |
| Existing native/C11/pthread/C++ lifecycle and synchronization commands | Pass, one/four CPUs |
| 96 held workers, joined results, replacement generation and stale identity | Pass |
| Detach and subsequent creation/join collection paths | Pass; callback completion alone is not a retirement proof |
| 192 keys, unset/NULL values, stale replacement, four destructor passes and callback growth | Pass |
| 4 KiB minimal libc worker and 16 MiB worker stacks | Pass; minimum does not promise arbitrary code fits |
| 64 KiB alignment with nondivisible size, calloc overflow, failed realloc contents | Pass |
| 2,147,487,744-byte real allocation, first/last byte touches, free | Pass; not an every-byte bandwidth measurement |
| Both SDL archives built; 65,537-byte and 16 MiB ordinary SDL workers joined; SIZE_MAX refused | Pass (SDL runtime at one CPU) |

Before-code controls were captured before integrating changes. The first
interleaved run overlapped another LLVM build and is retained as load-affected,
without a performance conclusion. After that build ended, no build/container or
prior QEMU remained active locally (VM load 1.24 over 16 vCPUs). Repeated the same
existing allocbench commands, A/B interleaved three times within one four-CPU
QEMU boot; these are within-boot samples, not native results or fresh-boot variance.

| Workload | Before median ms [min, max] | After median ms [min, max] |
| --- | --- | --- |
| Heap 64-byte payload | 29.072 [29.035, 36.147] | 29.630 [28.666, 40.191] |
| Mixed heap | 2.595 [2.547, 2.618] | 2.658 [2.593, 3.022] |
| Native pages | 25.121 [22.798, 26.484] | 23.767 [22.647, 24.221] |

Median changes are +1.9%, +2.4%, and −5.4%, respectively, with overlapping ranges.
No material regression is established in these nested samples; they do not
predict native throughput. Small receipts and provenance are in
`/shared/present/limits-audit/wp6-evidence/`; source, SDKs and images stay local.

The initial run above preceded coordinated native count/reason changes. Stopped raw-context key-value cleanup was source-reviewed, without
an injected allocation failure or a new stopped-context runtime claim. The
existing SDL detach no-op is outside this delivery. All owned QEMU guests stopped.

WP6.7 follow-up: failed obsolete key-metadata retirement no longer stops the
process. Source review checks that released/uncertain descriptors are forgotten
before any collector could retry them; failed pre-publication allocation keeps
its original status. Rebased onto parent main `d1c409a8b`, userland main `869a40e`, and ports main
`52381a0`; integrated userland `aba5e3a` and ports `19a4a02`. Rebuilt kernel,
SDK/C++ runtime, selected applications and both SDL archives. No WP6 kernel edit.
The 13 ordinary key growth/destructor, lifecycle, stack, allocation and SDL
checks passed at both one and four CPUs after this change and the WP9 rebase.
Existing allocator timing results above are retained; no timings were taken
during the concurrent hosted LLVM build. Cleanup-error behavior is
source-reviewed; no injected native failure or every-byte reuse proof is claimed.


## Coordinated kernel admission, 2026-11-02

Base is actual WP3 `2f9463ae4` (userland `a0a4a9d`, ports `02aec1f`), with the
WP6 userland/SDL commits rebased onto those heads. Before editing kernel code,
rebuilt the kernel, SDK/C++ runtime and selected boot/session/shell/Remote/HTTP,
benchmark and thread consumers with the same `8bdf136` compiler. Unchanged
ancillary archive programs were not treated as rebuilt release artifacts.

One/four-CPU Q35 KVM, 8 GiB: 288 workers held concurrently and joined; generation
reuse/detach controls; process exit while 288 workers remained held followed by
successful fresh native-thread launch; 192 keys/destructor growth; 4 KiB/16 MiB
stacks; C++ thread control all pass. A native SDK consumer checks page/guard
rounding LIMIT, user-address MEMORY_ADDRESS LIMIT, zero/reserved BAD_REQUEST,
cleared failure outputs, small allocation/release and optional key detail clearing.
No failures are injected and no counter/identity exhaustion is claimed.

Fresh per-variant boot controls (three samples each) show heap medians
18.723/19.523 ms, mixed 1.802/2.386 ms and pages 18.209/19.020 ms. The mixed
candidate spans 1.760–2.698 ms, overlapping the baseline; no causal performance
conclusion follows from that variation. A follow-up interleaves the before/after
SDK-linked allocbench binaries in one candidate-kernel boot, using the same
commands above and no concurrent owned build:

| Workload | Before median ms [min, max] | After median ms [min, max] |
| --- | --- | --- |
| Heap | 18.985 [18.962, 19.372] | 18.904 [18.892, 21.667] |
| Mixed | 1.786 [1.736, 3.243] | 1.855 [1.821, 3.522] |
| Pages | 18.222 [17.894, 18.521] | 18.332 [18.033, 19.051] |

Changes are −0.4%, +3.9% and +0.6%, with overlapping ranges. This isolates the
relinked SDK/libc path on the changed kernel; it is not a native or fresh-boot
kernel A/B result. Other agents' idle/low-load guests were not stopped. No
material regression is established by these nested controls.

The LLVM native `Memory.cpp` translation unit also compiles against the new SDK;
full LLVM producer/consumer qualification awaits the required native-library
container rebuild. The cross compiler itself remains usable for these local
checks. Small receipts/provenance are in the existing WP6 handoff directory.

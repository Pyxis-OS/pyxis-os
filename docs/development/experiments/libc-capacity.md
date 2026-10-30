# Libc capacity qualification

Inspected and built 2026-10-30: parent `b37debacd`, initial userland `ae6d946`,
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

Kernel >256-thread admission and common numeric limit reasons await WP3
coordination. Stopped raw-context key-value cleanup was source-reviewed, without
an injected allocation failure or a new stopped-context runtime claim. The
existing SDL detach no-op is outside this delivery. All owned QEMU guests stopped.

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

Before-code controls and post-change/interleaved allocbench samples are retained
locally. The interleaved run was load-affected while another LLVM build was active
in the VM; its wide and overlapping ranges do not establish a regression or a
speedup. Quiet matched measurement remains pending. No native result is claimed.

Kernel >256-thread admission and common numeric limit reasons await WP3
coordination. Stopped raw-context key-value cleanup was source-reviewed, without
an injected allocation failure or a new stopped-context runtime claim. The
existing SDL detach no-op is outside this delivery. All owned QEMU guests stopped.

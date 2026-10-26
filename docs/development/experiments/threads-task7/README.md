# Threads gate 7 qualification

2026-10-12: static local-exec TLS, caller-only initial FS bootstrap, per-thread
errno and native runtime keys/destructors. No C11/pthread or synchronized libc
profile is claimed; those remain gate 8.

## Inputs and build

- A: main `ae0bc1df36b06d8e4861072dad8aa7f7b1b9259f`, userland `bdef26b`,
  LLVM `49e2c1a`; exact successful CI #1992 component bundles, verified ordinary
  image assembly. Earlier pre-code baseline was main `48219eca`.
- B: kernel/integration `48be694f`, userland `83c0415` (PR #231), ports
  `fd1eb30` (PR #101), fs `9b4479e`, LLVM fork #6
  `2d2344355c6825f3293e026aafe185c5632abfdb`. Ordinary kernel, SDK, C++ runtime,
  ports, userland and image builds passed with locally rebuilt Clang/LLD;
  unchanged-source compiler-rt/tools reused the existing compiler installation.
  The owner-built matching container is published. Exact-head CI passed on
  `6042dc18` (#748) and `fe4fede5` (#744); the latest rebase is being checked.
- B ELF SHA256 `c3d2b1b3737eea96b6054eb6fb76a69c9a1a82c25b302a9919d1a6200a27d375`;
  initrd `38f9717f8ba077f98beb0aa0795e18e84a0c9b088f2a8ab407777684ea3ceba3`.
  Each A/B kernel used its matching initrd: the startup ABI and errno changed.

Review rebase: integration now contains main `ecdeb0f4`; userland #231 is
`2bc56bf4`, containing main `d6842f8`. Range-diff confirms all three TLS
runtime/probe commits are unchanged. Ports `fd1eb30` still contains current
ports main `5ba172c`. The measurements above and below retain their original
inputs; no static initial-thread storage change was made. The published image
digest is `sha256:01ed06d712567c7e242cf43a01fb558557623863c11d5645e6db198ba512d5d7`.

Local compiler/linker probes passed mixed, empty, zero-only and over-aligned
TLS, P1F/ELF ordinary-load equivalence and malformed/unsupported model refusal.
The intermediate PT_TLS zero tail may overlap ordinary ELF data addresses;
only initialized bytes are copied. Bootstrap object inspection found no FS,
errno, malloc or compiler-generated copy/zero dependency before binding.

## Interactive qualification

QEMU 10.2.2 Q35/nested KVM, host CPU, one/four cores, 2 GiB, UTC, fresh raw OVMF,
modern VirtIO SCSI/net/RNG, standard VGA. Existing remote client and GDB; no
injected calls or added test infrastructure. Run the existing manual modes:

```text
session bin://threadprobe.pxe tls
session bin://threadprobe.pxe tls-inspect
session bin://threadprobe.pxe bind
session bin://threadprobe.pxe tls-stop
session bin://threadprobe.pxe tls-fault
```

Both CPU counts passed initialized/zero TLS aligned to 8192, distinct errno,
C++ destructor order 2 then 1, four key callbacks, stale generations, BUSY live
release and repeated completion. GDB established three members sharing CR3,
distinct FS/RSP, inaccessible guards, zero borrowers and pin release before
completion publication. Ordinary-exit breakpoints hit both destructor hooks;
CR3-qualified hooks had zero hits on forced stop/fault. Raw lifecycle and
parking refusals also passed. Final rebased image repeated TLS on 1/4 CPUs and
four-CPU GDB cleanup inspection; earlier forced/failure paths had identical
relevant source.

Unmerged manual programs qualified 4-byte initialized and zero-only TLS at
FS-4, errno during atexit, and SET_FS-only startup authority refusing CREATE,
EXIT and parking. In a 512 MiB guest, real 1 GiB TLS allocation returned
NO_MEMORY; bootstrap exited 1 with FS=0 before main, then echo succeeded.
Guest TCC compiled and ran a stdio/errno C program. Host TCC rejected actual TLS
objects without output. No native hardware result is claimed.

## Nested-QEMU matched cost

Same four-core configuration, identical kernel configs, info logging/profile
OFF, no debugger or task build during capture. Final A3/B3 follow an earlier
interleaved A1/B1/A2/B2 pair, preserved locally. Four ipcbench modes use sizes
64/4096, CALL/SEND, 256 messages and 100 rounds. Pages uses size 65536/live 8/
100 rounds; launches use a 16-child warmup then three Lua loops of 1024 echo
children. All 800 final IPC passes, counts/bytes, 800 page allocations/releases
per boot, launch counts and 18 FINAL 0/complete records were independently checked.

| Workload | A3 | B3 |
| --- | --- | --- |
| CALL64 completion | 98.962 (77.483–125.567) | 105.497 (76.419–117.695) |
| CALL4096 completion | 99.066 (75.828–118.589) | 106.056 (76.476–115.441) |
| SEND64 completion | 15.765 (11.844–24.287) | 17.358 (11.734–23.344) |
| SEND64 admission | 1.245 (1.186–1.873) | 1.246 (1.185–1.684) |
| SEND4096 completion | 15.565 (11.844–22.534) | 15.865 (11.763–25.988) |
| SEND4096 admission | 1.321 (1.260–1.940) | 1.325 (1.245–2.559) |
| Pages, ms | 276.450 | 434.554 |
| Pages, ns/op | 172781.5 | 271596.5 |
| 1024 launches, wall s | 4.26 / 3.26 / 3.74 | 5.15 / 4.94 / 4.56 |
| Launch median, wall s | 3.74 | 4.94 |

Clock calibration 35,668–40,723 ns/read was not subtracted. Launch wall time
includes reverse discovery/session setup: the final median increase is 32.1%.
Earlier B launches also rose; skipping empty caller-local key values removed
unneeded exit locks but did not eliminate this observed shift. Source adds a
MEMORY copy, guarded allocation, SET_FS bind and startup-grant close; allocation
and bind each take a BSP VM transaction. Exact cost attribution is unmeasured.

B3 pages rose 57.2% in this pair, while earlier B samples ranged 266–453 ms and
straddled A. IPC completion medians rose here with overlapping ranges; admission
medians stayed close. Nested-host load varied, including another agent's VM;
these observations do not establish native costs. Raw captures/commands/hashes
remain local and in the PR.

## Owner-native cost

2026-10-12, ThinkPad on AC/Ethernet, A/B/A/B fresh boots: A `dc4b1487`
with compiler `49e2c1a`, B `6042dc18` with `2d23443`, each using its own kernel
and initrd. The owner reported these results in #748; raw data remains at
`/shared/present/batch2/ab748/`.

| Workload | A1 / A2 | B1 / B2 |
| --- | --- | --- |
| 1024 launches, s (three runs each) | 1.262 / 1.228 / 1.448; 1.296 / 1.237 / 1.383 | 1.344 / 1.356 / 1.355; 1.526 / 1.638 / 5.85 |
| CALL64 fastest, ns | 3696 / 3876 | 3694 / 3669 |
| CALL4096 fastest, ns | 4494 / 4662 | 4479 / 4475 |
| SEND64 fastest, ns | 305 / 306 | 303 / 304 |
| SEND4096 fastest, ns | 685 / 707 | 583 / 583 |
| Pages, ns/op | 23630–26256 | 23599–26603 |

The owner's reading is +6–9% launch cost, about +0.11 ms per launch; the
5.85 s B2 launch was reported as an outlier and remains visible above. CALL,
SEND64 and pages are unchanged; faster SEND4096 may be layout noise, not a
claimed speedup. No individual bootstrap operation was profiled. This native
run measures costs, not a new hardware TLS/destructor inspection.

The owner accepted the launch cost on 2026-10-12. Initial-thread static TLS,
avoiding its guarded reservation while keeping siblings guarded, is a
much-later follow-up for launch-heavy shell scripts or native builds. No
optimization is assigned by this result; see [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1301).

For the owner-native run, stage each exact head's matching kernel and initrd,
then use the existing hands-free remote harness with these commands (launches
three times, after the 16-child warmup):

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 100
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 100
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 100
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 100
allocbench pages --size 65536 --live 8 --rounds 100
session bin://lua.pxe -e "for i=1,16 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=16')"
session bin://lua.pxe -e "for i=1,1024 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=1024')"
```

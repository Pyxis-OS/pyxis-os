# Clang driver execution qualification

Pre-code baseline, 2026-10-13. The following baseline precedes the driver changes.
Fresh main `4a09711e5c6890588a4dfe3da12a1e4b32b01c14` contains merged #772.
Userland `b38b0e09`, ports `c7e4bc35`, fs `414be0ac`, lwIP `a1aadb91`;
published compiler/fork `pyxis-llvm23.1.3-4c23a20` / `4c23a208`.
Ordinary image/SDK build and all four bundle verifiers pass. Kernel SHA-256
`a2c64c63e76c15ab4be71b3bc73a901fcd8c1a860e9395267ad86450b0c92551`;
ordinary ISO `b37bc60073c743603867b8b13f86e6b598c429203460993a89c8df17812cbf2e`.
Private consumer ISO `c55560d3eb6fbc8bbb3ffa8014b63f0725b2d2c681ab647f7b43cfb1d4b2db9f`.

The installed immutable `hosted-5466bcbd…` revision retains its self-contained
SDK/main `6bc3a009`; it was not rebuilt or changed. Current source ABI
`7c3e54374a431c83f8048c55bc5d4841d5e9877060f8f0ed768048c712bcadd0`, all
SDK headers and core source pins match. Nine runtime archive/crt0 bytes differ
between build paths; eight become identical without debug, and libc++abi has
three objects with differing assertion-source path literals. No byte-identical
archive claim: the installed tools use their own qualified packaged runtime.

QEMU 10.2.2 Q35, nested KVM/host CPU, 2 GiB, UTC, fresh OVMF, 1 then 4 CPUs;
VirtIO SCSI boot, net/RNG and read-only npfs VirtIO block storage. Same data image,
GUID `ebd66076-a25f-4d92-8bfc-bcbd170ca9c2`, hash `3043ea06…` unchanged.
Consumer has read-only toolchain, writable tmp, no tool launcher, separate
network space, no HOST/home. No probe, profiler or debugger during timing;
host load unisolated. Three serial samples include remote connect, one command,
shell exit and FINAL drain; first version follows boot, later samples warm,
without forced cache eviction. No new native run: [#772 native samples](../hosted-clang-task5/README.md#native-owner-qualification-2026-10-13)
are the physical-host reference, not an isolated A/B with this nested setup.

| Session seconds | 1 CPU ordered samples; median | 4 CPUs ordered samples; median |
| --- | --- | --- |
| Clang version | 11.80 / 10.62 / 10.64; 10.64 | 11.54 / 10.69 / 10.57; 10.69 |
| Hello compile-only | 10.87 / 10.83 / 10.89; 10.87 | 10.74 / 10.91 / 10.84; 10.84 |
| Standalone LLD | 8.22 / 8.21 / 8.22; 8.22 | 8.17 / 7.97 / 7.52; 7.97 |

All 18 timed commands exit 0 with complete FINAL drain. Hello runs as `hello 5`,
exit 0. Missing-semicolon and read-only output cases exit 1/no failed output on
both CPU counts. Object `1138fa33…` and PXE `915e1331…` match the exact hashes in
the task 5 record. All owned build/QEMU/client jobs stopped; source clean.

## Default driver refusal

Once, 1 CPU, remove the earlier executable before:

```text
toolchain://hosted-5466bcbd5ec9ad9a8f20ea56f24a246d325ce8a93503b0fec94f876f1ed3c87f/payload/clang.pxb tmp://hello.c -o tmp://hello.pxe
```

Actual command exit 1: `unable to execute command: Pyxis does not support
process timeout or statistics requests`. The next listing has no hello.pxe;
standalone LLD subsequently recreates it and runs successfully.

Inspected exact fork [Program.cpp:51](https://git.internal/PyxisOS/pyxis-llvm/src/commit/4c23a208a023007024a1b4ff32e5d9cd8733cf1c/llvm/lib/Support/Program.cpp#L51)
rejects `SecondsToWait || ProcStat` before `Execute`. [Job.cpp:385](https://git.internal/PyxisOS/pyxis-llvm/src/commit/4c23a208a023007024a1b4ff32e5d9cd8733cf1c/clang/lib/Driver/Job.cpp#L385)
passes a non-null statistics pointer on the ordinary path. Thus no child launch
follows this guard by source inspection; no runtime child tracing is claimed.
This is not a measured launcher-authority failure. Later launch-selection,
bundle lookup and containment gaps remain independently inspected.

Full provenance, commands, header/runtime comparisons, raw typed JSONL and
ordered `.time` samples: `/home/chronium/src/pyxis-clang-driver-baseline/build/driver-baseline/`
(`baseline.txt`, `commands.txt`, `timing-summary.json`, `consumer/`).


## Native subgroup foundation (2026-10-13)

Kernel implementation `dce3d0af`; retention/policy checks `6ca93160` (public
comments only), userland `e6ae2d59`. Current-main integration `6bc8d48b` merges
`3455b3b2`, userland rebased to `95859dda` over `e3c6ff9`; group/launcher/policy
sources are unchanged. Final integration `5f412368` merges main `c7a914e7`
and rebases userland to `2aacf97a` over `fcb0c0e4`, preserving the exact patch.
Fresh ordinary image/SDK builds pass with published
`4c23a20`, no fork/container change. Source guards add no klog lines.

Matched unprofiled Q35 nested KVM, 2 GiB, host CPU, fresh OVMF, 1/4 CPUs,
VirtIO SCSI/net/RNG, VGA, no HOST/disk/probe/debugger. Before is the frozen
`4a09711e` image: current-main `a2619a62` changed only Quake packaging/docs,
not kernel/ABI/userland/Lua benchmark inputs. After uses fresh `dce3d0af` image,
SDK and userland. Remote config and kernel configuration are identical.
Each IPC command uses 256 messages/100 verified rounds; one before/after boot
per CPU count, no host isolation or causal native claim. Cells are median
[min–max] milliseconds for the batch, except launches in seconds.

| Workload | 1 CPU before | 1 CPU after | 4 CPUs before | 4 CPUs after |
| --- | --- | --- | --- | --- |
| CALL64 completion | 84.226 [83.725–86.028] | 84.977 [83.958–109.082] | 99.702 [74.077–117.849] | 102.367 [74.722–170.040] |
| CALL4096 completion | 85.016 [84.234–87.543] | 84.980 [83.559–330.893] | 96.630 [74.486–121.184] | 100.895 [74.826–122.986] |
| SEND64 completion | 13.135 [12.927–13.695] | 13.163 [12.910–22.656] | 14.600 [10.268–20.120] | 15.450 [11.242–27.490] |
| SEND64 admission | 1.211 [1.194–1.676] | 1.221 [1.196–2.043] | 1.200 [1.040–1.645] | 1.217 [1.153–2.565] |
| SEND4096 completion | 13.199 [12.991–13.756] | 14.994 [13.352–57.642] | 15.528 [12.515–23.345] | 15.555 [11.466–22.452] |
| SEND4096 admission | 1.273 [1.251–1.510] | 1.477 [1.285–6.410] | 1.283 [1.212–1.988] | 1.297 [1.216–2.517] |
| Pages | 184.781 [183.915–186.186] | 184.784 [184.598–186.312] | 242.857 [239.675–248.535] | 263.209 [260.656–430.658] |
| 1,024 launches (s) | 3.620 [3.620–3.630] | 3.720 [3.640–4.220] | 4.690 [4.590–4.780] | 4.980 [4.870–5.110] |

Pages: three runs `allocbench pages --size 65536 --live 8 --rounds 100`.
Launches: 16-child warmup, then three Lua `pyxis.run` loops of 1,024 children,
wall proxy including client connect/FINAL. All 800 IPC passes verify, no CALL
failures/SEND rejections; all page/launch runs exit normally. Higher medians
and after outliers are retained: notably 1-CPU SEND4096 completion +13.6% /
admission +16.0%, 4-CPU pages +8.4% and launches +6.2%. These samples do not
establish a native regression; no per-payload-copy lock was added by this unit.
A native A/B would need each kernel's matching initrd: the retired CREATE_GROUP
right makes a shared old/new startup image incompatible. No compatibility
adapter or native staging was added.

Interactive debugger qualification, separate from timing:

- KVM 1/4 CPUs: real Remote root creation and existing Lua SDK `fresh_group`
  profile selected before validation. Atomic CONTROL|WAIT / bound-LAUNCH pair,
  depth two/parent identity, echo child EXITED0, wait/read/close0. Completed
  child leaves zero active edges/pending work despite retained handles.
- KVM 4 CPUs: creator `os.exit(7)` with a published live child closes sole
  CONTROL, requests child PROCESS_TERMINATED, then drains child/pending counts.
  Host-client disconnect with live parent/child loops stops both; COMPLETE
  stays false until safe retirement/deferred cleanup, then all group counts zero.
- TCG 1 CPU, safe BSP/kernel-root/IF=0: three-level stop with launch or cleanup
  token delays every ancestor; ending the token propagates completion while
  storage stays retained. Depth 17 and object 257 return LIMIT, including
  completed-held and unreaped storage. Final BSP destruction restores count
  zero and creation succeeds again.
- TCG real-object policy helpers: own/descendant allowed, ancestor/unbound
  refused, including zero-right grants; live tables/grants unchanged. This
  checks the shared kernel predicate, not negative COPY/IPC syscalls on the wire.
- Current integration `6bc8d48b`: fresh 1/4-CPU image/SDK repeats contained echo
  and zero-count cleanup. TCG ancestor TERMINATE at the real constructor's final
  install checkpoint wins: ENDPOINT_CLOSED, pair unset, reservation unconsumed
  until unwind, no new member. Admission keeps ancestors incomplete through
  that cleanup; final group count zero. This is an internal control-request
  interleaving, not simultaneous hardware execution.
- Final integration `5f412368`: fresh ordinary image/SDK and 1/4-CPU KVM
  contained echo repeat: depth two, one controller, successful start/wait/close,
  zero active child/pending counters, and final root destruction returns the
  space count to zero. Typed shell FINAL is EXITED0 with complete drain; guest
  poweroff exits QEMU normally, including the existing NVMe shutdown path.

No native run, broader descendant grants, forced-fault probe or exhaustive
concurrent COPY/IPC race coverage. Daybreak's adversarial kernel review remains
required before merge. Earlier debugger-command errors are preserved locally:
KVM register injection hit VM-entry failure before the function; an invalid
optional TCG fixture caused a debugger-only panic and was discarded; corrected
real-object checks ran in a fresh guest. Neither is shipping-path evidence.
All owned QEMU/GDB/client jobs stopped.

Records/raw captures: baseline worktree `build/subgroup-baseline/`; implementation
worktree `build/subgroup-after/`, `build/subgroup-qualification/` and
`build/subgroup-final/`. Final current-main smoke is in
`build/subgroup-current-final/`: kernel SHA-256 `51e8469f…`, ISO `fb2443a4…`.
Lua debugger ELF is relinked from current objects/SDK, rather than reusing its
earlier debugger copy. Logs/ordered samples stay local.

Linear rebuild after the `.pxs` rename: main `823a4101`, userland `39499b92`
over `b199314d`. The subgroup code patch keeps stable ID
`cd4025975b4e996d2f2ce79bb4f107b37bd671ea`; userland's patch keeps
`9f4ba39f3f69cf27baf49ebb674af8816f65a01a`. Comparing the old `8052c41a`
and rebuilt code trees finds exactly main's RAM FILESYSTEM_INFO additions in
`directory.h`, `directory.c` and `user/launch.c`, with no subgroup-code change.
The branch contains no merge commits beyond its main base. Earlier runtime
evidence remains historical; the current rebuild/CI is reported on #789.
Daybreak's independent source/exercised review found no confirmed serious issue;
its bounded race/refill/escape coverage and limits are in that PR's review comment.

## Driver source checkpoint (2026-10-13)

Contained core `68cf967e` (userland #251), integration SDK `0a00512c`, clean
inputs, published compiler `4c23a20`. Fresh ordinary SDK/runtime builds pass;
the existing C++ runtime emits its known missing-prototype and unsupported
`hardware_concurrency` warnings. Producer syntax and document/diff checks pass.
The producer declares Clang's optional launcher and embeds the same leaf LLD
bundle under read-only `linker://`; no launch authority is added to LLD.

Fork `8c3b0fe1` (LLVM #8): compile the five changed translation units (Support
Program, Clang Job/Driver/Pyxis toolchain/driver main) for Pyxis and Linux using
existing generated commands/configuration. Native uses the current SDK/core;
Linux selects `x86_64-unknown-linux-gnu` without stale precompiled headers.
All ten compile checks pass without warnings/errors. Objects/commands/logs stay
local in the fork worktree's `build/reached/`. This is source-compilation evidence,
not target links, tool runtime or installed compiler adoption.

Exact-fork container publication is an owner action from
`toolchain/clang-driver-container` at `e05d8673`. Current integration retains the
published old compiler pin until the new image exists. Full target closure,
default driver compile/link/run and refusals, cleanup, overlapping-memory
measurement and native owner check remain pending. No new native speedup claim.


## Adopted-image runtime checkpoint (2026-10-13)

Parent `b810b64e` (later docs-only `12111262`), userland `8c052506`, exact fork
and published image `8c3b0fe1`. Fresh ordinary image/SDK and all four CI jobs
pass; fresh workers-off Release X86 Clang/LLD links pass. Clean producer inputs
stage `hosted-bb12b9a196bf486d48e03ac57ffc28e90b541bccc05fab720131eaca4fc0a450`.
Archive 292,915,200 bytes, SHA-256 `d456eb973d2108bc57b5da78a80157dd10d9ec07808a28c31c6dbf51ed7dd9df`.

| Image bytes / mapped span bytes | Clang | LLD |
| --- | ---: | ---: |
| P1F | 90,385,750 | 55,776,350 |
| Page-rounded backing/span | 90,976,256 | 56,389,632 |

Both retain 8 MiB stacks and existing ceilings. Imported files compare exactly;
fsck/GPT pass. Ordinary read-only consumer ISO `0acb366f…`; isolated 1 GiB npfs
pool/disk `e8f10160…`. Configuration is the frozen Q35/nested-KVM/2 GiB profile,
separate network space and Remote with declared launch authority, no HOST/home.

**Qualification blockers, not a completed driver loop:** 1 CPU: version,
compile-only, standalone LLD and `hello 5` pass. Default driver refuses before
job admission: `Pyxis tool bundle: Invalid argument`. Inspection finds the shared
bundle parser rejects LLD's private `clang_resources`/`sysroot` names against
Clang's parent lookup roots, before final child selection filters them. Userland
`78372410` separates private manifest names from lookup context; actual selected
child-publication collision checks and external grant-alias policy stay unchanged.
Syntax, compile-only read-only output, requested statistics and oversized child
capture reject honestly. Linker/read-only driver failures are masked by the
bundle failure and are not qualified yet. No new timing or peak result.

4 CPUs, `launch=false`: compile-only passes; link-only refuses the missing declared
launcher, exit 1. A pre-existing `tmp://kept.pxe` containing `KEEP` disappears.
Separate hardware-breakpoint GDB observes `Compilation::CleanupFile` with that
exact filename, called by `CleanupFileMap` / `Driver::ExecuteCompilation`.
Fork `e0801c26` removes native requested-name cleanup, retaining owned temporary
cleanup and native writers' staged publication. This fixes the accepted old-output
preservation boundary; host cleanup behavior stays unchanged. Replacement image
`pyxis-llvm23.1.3-e0801c2` is an owner action from container branch `e982a12e`.
Do not qualify or stage the initial `bb12b9a…` revision as the completed driver.

The prepared native handoff is `/shared/present/batch2/hosted-clang-driver/native/`;
its final revision/hashes and expected output remain pending the corrected fresh
build. Final 1/4-CPU driver/refusal/owner-loss/cleanup checks, ordinary timings,
separate memory-probe checks and the native owner run remain. Matching debugger
ELFs were linked from the initial exact objects: all LOAD bytes/extents/flags,
entry and zero tails match the P1Fs. Raw captures stay under
`build/driver-qualification/` and shared handoff directories.

Unmerged measurement branch `probe/clang-driver-vm-peak`, `694160f1`, base
`b810b64e`: patch `/shared/present/batch2/hosted-clang-task5/probe-driver/driver-vm-peak.patch`,
SHA-256 `bda42582195352cd3d000f5e0922b5cf0d7f6b882dda345e8126fa378822152a`.
It records owned private data-frame backing per VM, tagged capture separately,
and their actual simultaneous global sum including shell/services. It excludes
page tables, kernel metadata/stacks, filesystem/cache, untagged allocations and
shared/device/borrowed mappings. Lifetime/per-boot peaks are not subtree-only or
whole physical RAM; independent peaks must not be added. Probe kernel
`648821b4…` built, labelled PROBE ISO `63fe832d…`, not yet run; never merge or
stage as ordinary PXE. All QEMU/GDB/client jobs for this checkpoint are stopped.

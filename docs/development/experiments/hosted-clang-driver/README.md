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

## Qualified driver integration (2026-10-13)

Fresh source/SDK `da89bcf5`, userland `19df7341` (rebased on merged libc
#256), ports `7d698eeb` (merged jq), fs `414be0ac`, exact fork
`e0801c2660613d130abc66edc52a33875ee2e1f1` and published
`pyxis-llvm23.1.3-e0801c2`. Ordinary image/SDK, fresh workers-off Release X86
Clang/LLD links and staging pass. No serial-only source adaptation. Existing
platform warnings remain; no build error. Matching debugger ELFs compare every
LOAD byte, extent, flag, entry and zero tail against the actual P1Fs.

Revision `hosted-e0d776324e241b611495059eb7691d5901c35a0ba35b998bee2c93fd9dc0d7fc`;
archive 293,416,960 bytes, SHA-256
`a94062826a98b2f099f0839f078938396ad3857243cae6487ff98c8d0c901787`.
All 6,487 imported files compare exactly; fsck/GPT pass. Clang image/span
90,385,074/90,976,256 bytes, LLD 55,776,366/56,389,632; both 8 MiB stacks,
existing 128/256 MiB image/span ceilings. Read-only toolchain pool, writable
tmp, separate network space, Remote launch=true; no HOST/home or boot payload.

### Runtime and ownership

QEMU 10.2.2, Q35/nested KVM/host CPU, 2 GiB, UTC, fresh OVMF, VirtIO
SCSI/net/RNG/read-only npfs block; 1 then 4 CPUs. Each 31-command machine
session returns all expected typed results and FINAL exited0/complete drain.
The plain relative command is `toolchain://REV/payload/clang.pxb hello.c -o hello.pxe`.
Driver and separate link/run print `hello 5`, exit0; their PXEs match
`acb72969…`, object `68ba3eeb…` (1,160 bytes). The reference object was generated
and downloaded from this guest, rather than reused from an earlier revision.

Syntax, genuine default undefined-symbol link, invalid linker option, read-only
compile/link output, statistics and oversized child capture all exit1 with the
correct diagnostic. Both invalid-option and default undefined-symbol links
preserve the five-byte KEEP file, hash `c7cde802…`; final listings contain only
fixtures and successful outputs, no failed requested files or owned temporaries.
A fresh 4-CPU launch=false boot still compiles, refuses linking with exit1,
preserves KEEP and leaves no partial file.

Separate interactive GDB: successful close on 1/4 CPUs has actual EXITED/0,
process+group complete, exact handle generations and zero members/launches/
pending cleanup/children before close. On 4 CPUs, a new real LLD entry is
identified by argv/process/CR3, not overlapping symbol addresses. Foreground
Ctrl+C stops Clang; kernel table cleanup releases its sole CONTROL, stopping
LLD. Actual child TERMINATED precedes subgroup completion (pending cleanup 4);
NPFS retirement then completes the subgroup with all four counts zero. No
injected calls, target writes, program delays or stale-pointer inspection.

Initial `8c3b0fe` qualification exposed two genuine blockers: the bundle parser
compared private child roots with parent lookup roots, and inherited driver
cleanup deleted pre-existing requested output. Userland #251 separates private
manifest validation from final selected-publication collision checks; fork
`e0801c2` skips native requested-name cleanup while retaining owned TempFiles.
Initial `bb12…` and intermediate `5f09…` payloads are historical, not current.

### Ordinary wall proxies

No debugger/probe/own builds during timing; host remains unisolated. Three serial
samples after functional preparation, warm caches without forced eviction.
Times include remote connection, image loading, command and cleanup/FINAL;
host CPU time is the client, not compiler CPU time. Every timed command exits0.
Old baseline/native task5 numbers are references, not matched current-SDK A/B.

| Session seconds | 1 CPU ordered; median | 4 CPUs ordered; median |
| --- | --- | --- |
| Empty | .02 / .02 / .02; .02 | .02 / .02 / .01; .02 |
| Clang version | 10.36 / 10.34 / 10.35; 10.35 | 10.41 / 10.47 / 10.36; 10.41 |
| Compile-only | 10.55 / 10.58 / 10.57; 10.57 | 10.65 / 10.53 / 10.52; 10.53 |
| Separate LLD | 8.15 / 8.10 / 8.09; 8.10 | 7.82 / 7.99 / 8.04; 7.99 |
| Driver compile/link | 18.06 / 18.06 / 17.99; 18.06 | 18.36 / 18.24 / 18.06; 18.24 |

### Separate memory probe and limits

Authorized **unmerged** branch `probe/clang-driver-vm-peak`,
`694160f160139b0f5fa9d4df05edad000ac2fc54`, base `b810b64e` (kernel/ABI/arch
code equals current source before the probe). Patch
`/shared/present/batch2/hosted-clang-task5/probe-driver/driver-vm-peak.patch`,
SHA-256 `bda42582195352cd3d000f5e0922b5cf0d7f6b882dda345e8126fa378822152a`.
Trace kernel `8749ae05…`, labelled PROBE ISO `6b32c75f…`; ktrace only, never
shipping code or ordinary PXE. Fresh 4-CPU/2-GiB boot for each case, same inputs;
standalone link uses the new reference object without compiling first.

| Tracked peak bytes | Clang VM | LLD VM | Actual per-boot VM+capture total |
| --- | ---: | ---: | ---: |
| Version | 99,618,816 | — | 206,139,392 |
| Compile-only | 101,888,000 | — | 206,139,392 |
| Separate LLD | — | 70,451,200 | 136,945,664 |
| Driver compile/link | 101,888,000 | 70,451,200 | 238,833,664 |

Capture backing is 90,386,432/55,779,328 bytes and returns0 after release.
Every retired VM has after_cleanup0; global current returns to the live-services
baseline 14,970,880 after shell exit. The global peak includes other tracked
VMs/services and actual overlapping capture, not a sum of independent peaks or
a compiler-subtree-only total. Excluded: page tables, kernel metadata/stacks,
filesystem/cache, untagged allocations, borrowed/shared/device mappings and
unbacked guards/reservations. This is not total RAM or a stack high-water bound.
Existing planning allowances remain provisional; native driver peaks are pending.

Real 1-CPU/256-MiB PROBE boot: nine Clang capture attempts refuse before program
entry with launch_failed/Out of memory. Partial backing returns0, every retired
VM cleans to0, shell remains usable, KEEP unchanged, no failed outputs and
FINAL0/complete. This qualifies admission rollback, not started-tool malloc
exhaustion or native low-RAM behavior.

All owned build/QEMU/GDB/client jobs stopped. Raw records, inputs, exact hashes,
ordinary samples and debugger proofs remain under the integration worktree's
`build/driver-qualification/`; final native packet is
`/shared/present/batch2/hosted-clang-driver/native-final/README.md`.
**Native driver execution/timing/peak and physical publication are NOT RUN**;
the ready sheet specifies owner/luna ordinary and separately labelled PROBE runs.

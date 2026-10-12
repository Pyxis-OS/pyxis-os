# Threads gate 8 qualification

2026-10-12: managed libc creation/collection, parking-backed synchronization,
shared allocator/descriptor/FILE/cache safety and audited external C++ threads.
No new kernel ABI or compiler source change. Consumer adapters remain gate 9.

## Inputs and baseline

Pre-code A: `ce8c53d17397027bdf4876787b72279027e8031e`, tree-identical to
qualified #748 `0c316b61`, now merged in main `7db4cb31`. Userland `2bc56bf4`,
ports `fd1eb30`, fs `28386cc7`, lwIP `a1aadb91`, published builder
`pyxis-llvm23.1.3-2d23443`. Successful exact-head CI REST2087/UI2010 bundles
passed independent verification and ordinary image assembly before capture.

Sequential fresh one/four-CPU QEMU10.2.2 Q35, nested KVM/host CPU, 2 GiB, UTC,
fresh raw OVMF, modern VirtIO SCSI/net/RNG, standard VGA. Existing remote client,
no debugger/build during timing; LOG_LEVEL=info and trace/profile off. Other
agents' VMs were present, so host load was not isolated. Commands: four ipcbench
CALL/SEND 64/4096 modes, 256 messages/100 rounds; allocbench pages size65536/live8/
rounds100; 16 echo launches warmup then three Lua loops of1024. Ordinary echo/exit
exercised the launch/IPI path without a separate IPI timing claim.

All800IPC passes, warmups, message/byte counts,800allocation/releases per boot,
launch counts and20FINAL0/complete records verified; no exclusions. Median
(minimum–maximum) ms per256-message pass:

| Workload | A1CPU | A4CPU |
| --- | --- | --- |
| CALL64 completion | 84.299 (83.614–90.425) | 119.099 (76.674–734.443) |
| CALL4096 completion | 84.136 (83.599–86.255) | 101.907 (67.237–574.976) |
| SEND64 completion | 13.037 (12.776–13.512) | 15.399 (11.465–22.135) |
| SEND64 admission | 1.197 (1.183–1.372) | 1.219 (1.155–1.811) |
| SEND4096 completion | 13.156 (12.892–14.970) | 15.688 (11.489–25.172) |
| SEND4096 admission | 1.273 (1.241–1.723) | 1.298 (1.233–1.957) |
| Pages ms | 185.511 | 262.209 |
| 1024launches wall s | 4.38 / 4.36 / 4.39 | 4.63 / 5.84 / 6.69 |

Clock calibration35,818–40,910ns/read was not subtracted. Launch wall includes
reverse discovery/session setup; pages has one sample/boot and four-CPU ranges
are broad. The final matched comparison below includes later merged pipeline
and FAT32 inputs; this original capture establishes the before-code baseline.

## Final matched nested-QEMU comparison

A: `98a4b1b05e762efe53e209da46cd8609189868c0` (#755, now merged), userland
`3daa24611e1e0a3471e182bb34e281e861be5515`. B: `c6449601`, userland `486d80e`.
Both use fs `30f625`, ports `fd1eb30`, lwIP `a1aadb91`, the same published
compiler and byte-identical kernel configuration. A's successful exact-source
CI REST2106/UI2028 bundles passed all ordinary verifiers/image assembly; B is
the local ordinary build independently covered by successful exact-source CI
REST2108/UI2030. Each image uses its matching initrd.

Same configuration/commands as above, frozen client and firmware, fresh boots
in order **1A, 1B, 4A1, 4B1, 4A2, 4B2**. All2400timed IPC passes,24warmups,
six800-allocation/release page runs, six16-launch warmups,18x1024launch loops
and60FINAL0/complete records were independently verified; no exclusions.
Cells are median (minimum–maximum) ms per256-message pass; four-CPU cells pool
200samples/mode across two boots. These are batch timings, not latency percentiles.

| Workload | 1CPU A | 1CPU B | 4CPU A | 4CPU B |
| --- | --- | --- | --- | --- |
| CALL64 completion | 84.091 (83.441–90.502) | 87.148 (84.856–89.832) | 102.794 (73.608–129.004) | 100.456 (69.692–159.581) |
| CALL4096 completion | 84.375 (83.631–86.594) | 86.987 (85.351–98.195) | 103.579 (73.747–143.900) | 104.266 (72.355–142.060) |
| SEND64 completion | 13.038 (12.840–14.757) | 13.430 (12.872–16.599) | 15.471 (11.668–24.450) | 18.130 (12.848–32.711) |
| SEND64 admission | 1.204 (1.184–1.709) | 1.232 (1.185–1.633) | 1.228 (1.152–2.172) | 1.287 (1.193–3.000) |
| SEND4096 completion | 13.119 (12.917–13.655) | 13.733 (13.078–16.965) | 15.690 (11.463–26.435) | 18.241 (12.365–29.234) |
| SEND4096 admission | 1.260 (1.240–1.483) | 1.332 (1.261–1.919) | 1.312 (1.206–2.031) | 1.345 (1.210–2.958) |
| Pages ms, all samples | 185.388 | 212.470 | 261.706 / 272.922 | 448.980 / 447.933 |
| 1024launches wall median s | 3.380 | 3.770 | 4.655 | 5.025 |

Launch samples: 1A3.37/3.38/4.38s;1B3.81/3.77/3.76s;
4A1 4.69/4.58/4.62s;4B1 4.61/4.72/4.66s;
4A2 4.53/4.79/5.73s;4B2 5.36/6.51/5.33s. Clock calibration
35,567–42,120ns/read was not subtracted. Launch wall includes discovery/session
setup, pages has one sample/boot. B pages are higher in both four-CPU pairs
(64–72%) and the single one-CPU pair (15%); four-CPU SEND medians are about17%
higher. These shifts are recorded, not accepted native costs. Four-CPU host
load varied with unrelated VMs, left untouched; no unrelated VM was observed
in the one-CPU snapshots. No causal attribution or speedup is established.
Owner-native interleaved A/B is required before accepting a steady cost.

Source inspection separates startup from timed work: libc now queries/copies
supplied THREADS and CLOCK grants and establishes managed identity at startup,
and adds shared-state exclusion to allocation and FILE/cache operations. Launch
therefore includes real additional setup; its individual contribution is not
measured. `allocbench pages` calls native MEMORY directly inside its timed loop,
with calloc/stdio outside it; new TLSF locks/BUSY helpers are not that loop's
allocation path. Kernel reply staging, payload copies and syscall root accounting
remain unchanged. The pending-entry scheduler correction executes after rejected
admission handoff, not on an ordinary successful entry. None of these inspected
facts attributes the measured pages/SEND shift.

Commands for native capture use each image's matching initrd and the same client:

```sh
for mode in call send; do
  for size in 64 4096; do
    printf 'session bin://ipcbench.pxe %s --size %s --messages 256 --rounds 100\n' "$mode" "$size" |
      pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323
  done
done
printf 'allocbench pages --size 65536 --live 8 --rounds 100\nexit\n' |
  pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323
printf 'session bin://lua.pxe -e "for i=1,1024 do assert(pyxis.run{\x27echo\x27,\x27-n\x27}==0) end; print(\x27launches=1024\x27)"\n' |
  pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323
```

Use16launches for warmup, then three1024-launch samples measured with the host's
`time`. Exact commands, hashes, raw JSONL and independently checked results are
retained in `pyxis-threads-gate8-control-755/build/gate8-comparison/`.

## Interactive implementation qualification

B: parent `c6449601`, userland `486d80e` (contains pipeline/FAT32 main `3daa246`),
fs `30f625`, unchanged ports/compiler. Required FAT32 ABI dependency #755 is now
merged; it is not gate 8 implementation. Ordinary runtime, SDK (threaded
libc++/abi/unwind), ports, userland and image builds pass. Exact-head CI
REST2108/UI2030 passed changes/pins/filesystem/build. No new compiler container.

One-CPU2GiB final image passed all six managed runtime modes, TLS and forced
TLS stop/fault/parking-stop; each returned FINAL0/complete. C++ covered separate
TLS/errno and concurrently held exceptions, static/once exception retry,
cross-thread heap-backed objects, language-before-key drain, thread-exit
notification and string/shared streams. Guest TCC built and ran a TLS-free
stdio/errno C program. Earlier four-CPU2GiB fixed-core runs passed these paths
and repeated the63-sibling limit/collection. The final four-CPU512MiB image
repeated all six runtime modes and exhausted341,598,208bytes in523native
regions: native allocation/CREATE returned NO_MEMORY and managed create returned
thrd_nomem, then both created/joined after cleanup.

GDB established real word parking, shared process/CR3 and distinct FS/stacks/
TLS/errno; post-join task/storage counts1, cleared backing identities/observers
and zero waiters/borrowers. Final four-CPU pipe checkpoints established actual
`task_wait_sleep_interruptible <- pipe_call` admissions with zero mapping
borrowers before FILE/raw close/reuse; both returned verified EOF/EPIPE/occupant
results. C++ readiness flags/delays establish retry opportunities, not observed
guard enrollment; native parking was inspected separately. No injected calls,
user-data mutation, new test infrastructure or klog lines.

Manual commands use `session bin://threadprobe.pxe` with modes `runtime`,
`runtime-inspect`, `runtime-time`, `runtime-once`, `runtime-limit`, `runtime-cxx`,
`runtime-io` and `exhaust`. Existing TLS/stop/fault/parking modes remain available.
Use the matching ELF relink (`-Wl,--oformat=elf`) and CR3-qualified hardware
breakpoints; no injected calls or live user-data mutation. IO checkpoints require
actual `pipe_call` admission, not a ready flag or scheduling delay.

Raw input hashes, commands, captures and debugger records remain under the
isolated gate8 baseline and implementation worktrees' `build/gate8-*` directories.
These functional runs are nested-QEMU evidence; owner-native timing is recorded below.

## Owner-native A/B and hot-loop review

2026-10-12, ThinkPad on AC/Ethernet/PXE, A/B/A/B: A main `36bcd7e9`, B
`b350db24`; each uses its matching initrd (userland `3daa2461`/`486d80ef`),
published `pyxis-llvm23.1.3-2d23443`. Raw JSONL is under
`/shared/present/batch2/ab756/756{A1,B1,A2,B2}/`. All1600timed IPC passes and
16FINAL0/complete records were independently checked, with100verified passes
per mode/boot and no exclusions. Completion/admission cells below are the raw
summaries' median ms per256-message pass, not single-message latency percentiles.

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 1.451 | 1.601 | 1.408 | 1.646 |
| CALL4096 completion | 2.001 | 2.239 | 2.040 | 2.249 |
| SEND64 completion | 0.578 | 0.738 | 0.505 | 0.742 |
| SEND64 admission | 0.122 | 0.151 | 0.114 | 0.138 |
| SEND4096 completion | 1.302 | 1.206 | 0.976 | 1.201 |
| SEND4096 admission | 0.267 | 0.265 | 0.257 | 0.257 |
| Pages ns/operation, all3samples | 26263.5 / 26134.4 / 26166.2 | 25712.4 / 26147.4 / 23759.2 | 26351.6 / 23769.0 / 26691.8 | 26169.4 / 24809.7 / 26077.2 |
| 1024launches wall s, all3samples | 1.534 / 1.469 / 1.520 | 1.447 / 1.432 / 1.527 | 1.599 / 1.618 / 1.644 | 1.570 / 1.755 / 1.647 |

The CALL64 floor is3851–3859ns/round trip and SEND64 admission floor306–307ns/send;
CALL4096 reaches4611–4618ns apart from B1's5230ns minimum. These are minimum
batch means. Pages/launch and SEND4096 admission are unchanged within variation;
small-message batch medians are higher in both B cycles. The shift is not
dismissed as noise or accepted as a steady cost.

**Inspected hot paths:** linked ipcbench `.text` has all9411non-relocated bytes
identical within its10383-byte instruction block at `0x400000`. Native
endpoint/clock blocks match outside relocations; memcpy/memset's1063bytes match.
CALL invokes native endpoints and copies; the receiver receives/copies/replies.
SEND calls native sends, with a DRAIN control CALL per8messages (32per pass).
Receipt release is native `syscall_close`, not libc descriptor close. No new
lock, thread-self, descriptor or allocation call executes in these loops.
All sample reporting happens **after all timed passes have finished**, so FILE locking is
neither inside timing nor between passes. Successful kernel syscall-entry
instructions/spills match; its scheduler correction is only after rejected entry.

**Changed state, not a percentage attribution:** B runtime retains one extra
CLOCK READ handle in each receiver (no THREADS/SLEEP authority is supplied).
Accounting for the common retained TLS and allocator MEMORY grants, source
reconstruction predicts DRAIN's receipt at slot3 and data receipts at A slot7
versus B slot8: eight versus nine entry tests in the guarded scan from slot0.
This predicts256extra tests per pass; it is not a live table/count measurement. Standalone
CALL receiver receipts still select slot3 in both; the sender's four-reply-slot
reservation can scan farther depending on its startup grants. Indexed handle
lookup is unchanged. This corrects the narrower earlier assertion that unchanged
instructions imply no added timed work: retained grants can add scan work.

B also adds27792executable bytes and7152writable-memory bytes to ipcbench,
moving helpers (endpoint block `0x410420` to `0x4170b0`). Fresh receiver setup,
allocation and cleanup occur outside each interval but can alter placement/cache
state for the next pass. Scan, layout and background-userland contributions to
the native percentages remain unmeasured; none is a proven cause. An unsupported
borrowed-clock shortcut would weaken retained-grant lifetime, and a speculative
reservation optimization is not applied to conceal the regression.

The bounded next native diagnostic uses the **old ABI-compatible artifacts**:
kernel A + initrd B, then kernel B + initrd A; run only CALL64/SEND64 with the
existing256messages/100rounds commands. A shift following initrd B narrows the
cause to userland/state/background activity; following kernel B narrows it to
kernel effects; an interaction remains possible. Do not cross these old images
with the subsequent completion ABI. No source probe or profiling is required.

Userland #236 is now rebased onto `62da3399` (merged completion helpers), at
`21cf935c`; #756 also includes current main's merged #758 completion ABI. The
table above remains qualification of the original native A/B, not a measurement
of those new inputs. A new ordinary image build passed; fresh one/four-CPU2GiB
boots passed the six managed modes (including63siblings/refusal/reuse), two
runtime inspections and four-CPU IO inspection, with15FINAL0/complete records.
Post-join GDB found task/storage counts1, FREE records with backing/observer0,
VMborrowers0, empty condition/address waits and four-CPU global VMwaiters0.
Both IO phases established actual parked `pipe_call` admissions and clean
borrower state. Records are under `build/gate8-qualification/rebase-final/`;
all owned VM/GDB/client jobs stopped. Merge order remains userland #236 before
Pyxis #756.

Four-CPU qualification exposed two failures before delivery. GDB caught a worker
returning from its first failed malloc with native MEMORY `CALL_BUSY` still in
its request record and errno ENOMEM. The reviewed allocator now rechecks published
pools and retries only bounded, definite pre-effect BUSY refusals outside guards;
managed backing/CREATE/cleanup uses the same bounded policy. Real NO_MEMORY and
unknown outcomes are not retried.

The 63-sibling stress also hit a kernel exception with a stack-address RIP and
corrupted interrupt return fields. An admission breakpoint independently caught
a migrated, not-yet-admitted syscall retaining CPU2's scheduler pointer/stack
while executing on CPU0. A repeated rejection could switch onto the wrong CPU's
permanent stack. The kernel correction refreshes the CPU-local scheduler after
that handoff, preserving pre-admission migration/stop behavior and adding no
ordinary successful-entry operation. Fixed-image runtime/capacity repeats passed without another panic.

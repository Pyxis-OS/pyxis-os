# Guarded inspection qualification

2026-10-09, debugger task 2. Baseline `fe50d6c2` was captured before code changes;
implementation `f0afab51`, documentation/header comments `22d6c983`. Ordinary
`make -j16 image PREBUILT="sdk userspace ports"` passed in the installed
`pyxis-llvm23.1.3-49e2c1a` builder, both default-off and `DEBUG_CHECKPOINT=1`.
Verified SDK/userland/ports bundles were reused; no compiler rebuild. Pins:
userland `74f3e229`, ports `6d63971d`, lwIP `a1aadb91`, fs `b427df29`;
SDK content identity `2d5c490771939fad52ccf5b160b0132ccdb7d1a57dec095453bd82154b4a9d27`.

## Interactive inspection

QEMU 10.2.2, Q35/nested KVM, CPU max, 512 MiB, one and four CPUs, standard VGA,
display none, VirtIO net/RNG, user networking, UTC RTC, fresh Fedora
`OVMF_VARS.fd` paired with `OVMF_CODE.fd`; no disks/export/profiling. Existing
QEMU GDB only, hardware breakpoints and the owned mailbox/probe/window controls,
no injected calls or target RAM/MMIO writes. Separate ordinary-fault checks used
one CPU and 2 GiB. No new guest self-test or qualification infrastructure.

| Inspection | Observed result |
| --- | --- |
| BSP/AP registers | COMPLETE, matching generation; returned captured frames/CR3/GS, selected AP RIP matched its snapshot |
| Kernel RAM, read-only text, cross-page text | OK; returned bytes matched direct GDB inspection |
| Page tables and debugger storage | Recursive root and snapshot RAM readable; no write exclusions imposed on reads |
| Invalid requests | Out-of-range CPU, stale generation and oversized length: BAD_REQUEST |
| Invalid addresses | Unmapped, noncanonical, overflow, APIC MMIO, framebuffer, invalid captured CR3 encoding: BAD_ADDRESS |
| Range into stack guard | BAD_ADDRESS, zero successful bytes, partial output cleared, both window leaves absent |
| Guarded #PF | Withdrew only the owned translation/data leaf before its first load; FAULT, clean windows, later valid read succeeds |
| Guarded #GP | Changed only owned probe source/RDI to noncanonical; observed vector 13/error 0, FAULT, later valid read succeeds |
| Unrecognized #PF/#GP | Inactive probe on fresh guests; original exception_handler and normal fatal kernel exception report; request not completed |
| Matching release, four CPUs | RELEASED/resume generation 1, windows absent, normal ready point reached |
| Expiry, one CPU | No release request; expiry unchanged by inspection, RELEASED at HPET 30.19334268 s for expiry 30.19323821 s; remote echo then exits 0 |

The initial conservative fixed-MTRR refusal rejected QEMU's root at physical
`0xa000`; implemented fixed-range decoding then permitted its actual WB RAM.
QEMU's PAT/MTRR setup was inspected; no native AMD/cache-extension qualification.
The guarded-fault epilogue uses no IRET: code inspection establishes preservation
of outer NMI blocking. A QEMU `monitor nmi` attempt caused no nested entry, but
no subsequent delivery was observed, so it does not independently qualify
pending-NMI delivery. No native, syscall-window or arbitrary clock-maintenance
reentry result is claimed. DMA/self-storage reads are not coherent snapshots.
The task 3/4 clock reentry qualification remains in the milestone plan.

Raw logs are retained in the isolated task worktree
`/home/chronium/src/pyxis-debug-inspection/build/inspection/` (`interactive-v2.gdb`,
`final.gdb`, `one-expiry.gdb`) and `build/fallback-qualification/` (PF/GP GDB and
serial logs). One post-expiry remote attempt preceded session readiness and
reset; after normal startup the session succeeded. The fallback PF transcript
also retains an initial recognized recovery before correcting GDB's rejected
`false` literal to numeric zero. Neither setup attempt is reported as a pass.

Qualified ELF SHA256:
`5fa72080104213d888065694139036fcf0dbbc72ea94f756ccd63dd061d6f1bb`.
The `22d6c983` rebuild has identical `.text`, `.data` and BSS layout; `.rodata`
differs only in the embedded Git revision. Its ELF SHA256 is
`e6ddfdd526fe90975513ff827b215017f25a73df6225d7beae67d4473ce923c8`.

## Before-code baseline and comparison inputs

Three initial baseline ready-point boots: 217.29656, 217.35031, 216.47264 ms;
plain echo: 0.02, 0.02, 0.03 s. Five initial fresh 100-launch boots:
0.52, 0.50, 0.52, 0.53, 0.50 s. All valid launches/echoes exited 0. These initial
blocks establish the before-code baseline, not the final interleaved comparison.

For each measured fresh boot, QEMU uses four cores/one socket/no SMT, 512 MiB,
the settings above and loopback 24569 to guest TCP 2323; fresh firmware variables.
Exactly one hardware breakpoint at `kernel/init.c:145` reads the existing ready
log's HPET counter at `0xfffffe80402020f0`, with its 10 ns period, then detaches.
Timing excludes firmware/earlier adapter work and can be perturbed by GDB.

One plain remote echo session, then one 100-launch session, use the exact frozen
client and input on every image, without an attached debugger during launch:

```sh
printf 'echo launch-baseline\nexit\n' >echo.input
/usr/bin/time -f '%e' pyxis-remote --machine --no-shell-echo \
  --columns 80 --rows 24 127.0.0.1 24569 <echo.input
```

100-launch input, followed by `exit`, is:

```text
lua -e 'for i=1,100 do assert(pyxis.run{"boot://echo.pxe", "ipi-baseline"} == 0) end'
exit
```

Host durations include connection, shell/application launch/wait/output/cleanup
and explicit exit/FINAL, at 0.01 s resolution. The staged Remote configuration
adds only `launch=true`; A and B use the byte-identical archived cpio (SHA256
`bdcd8ffb970d2130bb892384277ad15beadd1809e1c84e8b36de8a401e484f06`),
exact pins/config and frozen client (SHA256
`d8c65d8d6779d55e625359a3aa6285c7ae5158e0988a826cdcef6246d8257931`).
Neither image has `debug.checkpoint`. Only the kernel and its normal revision
identity differ. The initial profile typo was retained as an invalid-input
attempt, excluded before any echo launched; corrected profile is common to A/B.

Initial measurements had another owner's SDL VM running; it was left untouched.
These are shared-host nested-VM results, not native performance or isolated IPI
latency measurements. Current final-comparison conditions and every sample are
recorded below; no speedup or mathematically zero overhead claim is intended.

## Final interleaved comparison

Fresh boots in A1/B1 through A5/B5 order, five per image; A=`fe50d6c2`,
B=`22d6c983` (unchanged code from `f0afab51`). Every sample is retained,
including the initially slower B1 and later slower A5. No extra trials,
warmup or exclusions. All ten echo sessions printed one expected line; all
ten Lua sessions printed exactly 100 expected lines; commands and shell exited
0 with complete FINAL drain.

| Pair, A then B | Ready A, ms | Ready B, ms | Echo A, s | Echo B, s | Lua100 A, s | Lua100 B, s |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 295.74533 | 215.71054 | 0.02 | 0.03 | 0.49 | 0.60 |
| 2 | 228.07983 | 219.97080 | 0.03 | 0.03 | 0.54 | 0.56 |
| 3 | 218.26435 | 212.67616 | 0.02 | 0.02 | 0.49 | 0.48 |
| 4 | 212.07424 | 216.05120 | 0.03 | 0.02 | 0.53 | 0.49 |
| 5 | 213.44165 | 280.74529 | 0.04 | 0.02 | 0.69 | 0.51 |

| Metric | A median (range) | B median (range) |
| --- | --- | --- |
| Ready, ms | 218.26435 (212.07424–295.74533) | 216.05120 (212.67616–280.74529) |
| Echo, s | 0.03 (0.02–0.04) | 0.02 (0.02–0.03) |
| Lua100, s | 0.53 (0.49–0.69) | 0.51 (0.48–0.60) |

B-minus-A Lua differences: +0.11, +0.02, −0.01, −0.04, −0.18 s; mixed direction.
Each median falls inside the other image's observed range for all three metrics.
No option-off slowdown was reproduced in these matched trials; this closes the
task's timing gate without establishing zero overhead or a speedup.

Primary/reviewer qualification and builds were stopped before the comparison;
only one trial VM ran at a time. Initial snapshot listed no other QEMU, and the
per-trial snapshots listed only the trial VM. The earlier owner SDL VM had gone
away without this task stopping it. Recorded sample/process timestamps span
19:33:05–19:38:23 UTC. No host load averages or unrelated build-process census
were captured. QEMU snapshots do not establish whole-host idle time or
instantaneous CPU utilization; this remains a shared-host measurement.

B1 GDB inspection showed DISABLED, `arch_debug_enabled=false`, null `debug_boot`,
zero windows, null leaf pointers and cache readiness false; GDB detached before
timed sessions. Source inspection confirms ordinary IDT/fault handling, timer,
scheduler and IPI paths add no debugger work with the option absent.

A profile ISO SHA256:
`360b49498ae239a49b20945728cf109854a5e17644b8e6bdd65f9664dadc2cd0`;
B profile ISO SHA256:
`26f776fc24d347b47f6a76ab716a45616995ca496135cee6368b6e387718eb3a`.
Both extracted cpio archives match the common hash above. A ELF SHA256:
`42626cf08bbed8222bc8993bf28e81bad7dc6d90228648c68aaec33050e83739`;
B is the `22d6c983` ELF above. Raw inputs, JSONL, GDB, serial, timestamp/process
records and `parsed-results.json` remain in
`/home/chronium/src/pyxis-checkpoint-task2-baseline/build/{baseline,comparison}/`.
All owned QEMU/GDB/client/build processes were stopped afterward.

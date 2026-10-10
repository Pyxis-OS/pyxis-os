# File identity task 2 qualification

Recorded 2026-10-10 for [Neovim task 2](../../../wip/neovim-libuv.md#task-2-contract).
These are nested-KVM measurements. Raw captures and manually invoked scratch
probes remain in ignored `build/identity-{baseline,after,control,qualification}`.
No benchmark infrastructure, CI job or klog call was added.

## Inputs and commands

Before code: exact main `c29e0217c26902a34b346c3e86a51b19485b606f`, with verified
bundles from [CI #1647](https://git.internal/PyxisOS/pyxis-os/actions/runs/1647).
After: `1a0fbbd3`, then `7eeb914d` for the final recursive-once correction;
kernel/ABI source is identical between those revisions. Userland
`bb1c49ad0b6520338216e40cc67953caa5ec7a35` ([#193](https://git.internal/PyxisOS/pyxis-userland/pulls/193));
ports initially `dc75895`, finally `9397093c99b71ae3fa2d827c9b90f28f310111f6`
([#81](https://git.internal/PyxisOS/pyxis-ports/pulls/81)). Before pins:
userland `5aede1c`, ports `19fb10b`; fs `b427df2` and lwIP `a1aadb9` unchanged.
Ordinary `make -j16 image` builds use `pyxis-llvm23.1.3-49e2c1a`;
no compiler-container rebuild or filesystem format change.

QEMU Q35, nested KVM, `-cpu host`, 512 MiB shared memfd RAM, one/four CPUs,
standard VGA, raw OVMF CODE/VARS, VirtIO RNG, modern VirtIO net/user NAT,
VirtIO block and virtio-fs. Virtiofsd 1.14.0 uses `--sandbox=none`,
`--inode-file-handles=never`, `--no-announce-submounts`, `--rlimit-nofile=0`.
Private copies of a 2 GiB installed disk provide system/home npfs volumes;
current ISO kernel/apps boot against that disk, not its older ESP/apps.
Qualification-only config grants writable HOST/system/home and repeated roots
`host2`/`home2`. Before/after timing uses the same config, initial disk snapshot,
CPU/device layout and fresh firmware variables. Debugger/profiler detached.

A scratch native loop, recompiled against each SDK from identical source
(SHA-256 `47d923d4cf728053528756860372d5b613715a3da804897a8243b615431b9010`),
performs 1,000 `stat` calls or 1,000 open/fstat/close cycles per batch, verifies
regular type and stable 32 KiB size, warms up once and records five batches.
These include lookup/open/close; they are not isolated metadata latency.
Existing iobench verifies all bytes, short reads and EOF. Commands, repeated
for `tmp://`, `host://` and `home://`, and three TCC runs per CPU count:

```text
host://meta_cost.pxe stat home://small.bin 1000
host://meta_cost.pxe open-stat home://small.bin 1000
iobench read home://small.bin --bytes 32768 --rounds 5
tcc -bench -E -P host://shell/main.c -o tmp://shell.i
```

## Costs

Milliseconds per 1,000 calls, median [min, max]. Initial matched runs:

| CPUs / backing | stat before | stat after | open/fstat/close before | after |
| --- | --- | --- | --- | --- |
| 1 / RAM | .856 [.854, 2.291] | .896 [.886, 1.529] | 1.026 [.938, 2.268] | .979 [.976, .983] |
| 1 / HOST | 786.310 [782.846, 794.775] | 808.549 [798.170, 1642.258] | 800.816 [785.227, 810.932] | 909.382 [815.382, 926.618] |
| 1 / npfs | 961.794 [956.316, 983.708] | 1088.999 [1069.934, 1264.893] | 962.694 [959.452, 972.079] | 965.176 [959.189, 1055.906] |
| 4 / RAM | .900 [.858, 1.273] | .952 [.926, 1.497] | .965 [.957, 1.792] | 1.507 [1.031, 1.827] |
| 4 / HOST | 753.146 [738.754, 803.898] | 1211.064 [1166.138, 1276.477] | 742.296 [737.537, 757.109] | 1209.933 [1166.224, 1407.096] |
| 4 / npfs | 971.576 [968.378, 991.688] | 958.718 [952.989, 963.791] | 971.526 [951.183, 983.079] | 955.936 [955.460, 976.208] |

The large HOST increase did not persist: the same four-CPU after image repeated
at 733.816 [726.655, 736.960] / 740.005 [732.392, 747.271]. One-CPU follow-ups
interleaved a fresh baseline boot and the final after boot:

| Backing | control stat before | final stat after | control open/fstat/close before | final after |
| --- | --- | --- | --- | --- |
| RAM | .861 [.856, .879] | .891 [.890, 1.528] | .940 [.932, 1.560] | .978 [.976, .983] |
| HOST | 790.588 [784.238, 804.602] | 796.117 [784.689, 798.471] | 798.746 [784.119, 805.635] | 787.513 [783.420, 795.952] |
| npfs | 1042.865 [965.583, 2461.782] | 959.851 [954.276, 963.477] | 1035.944 [923.112, 2475.521] | 961.123 [956.259, 977.961] |

RAM's final median adds about 35 ns/stat against the original baseline.
HOST/npfs follow-ups return near original costs; the baseline itself exhibits
large outliers. These runs do not isolate scheduling/host-load effects or
establish physical-host performance. Code inspection adds bounded metadata
copy/conversion and live HOST mapping allocation/scans; it does not add disk
reads to npfs INFO. HOST mapping lookup is linear in live mapped incarnations.

Verified iobench complete-consumption milliseconds, median [min, max]:

| CPUs / backing | before | after |
| --- | --- | --- |
| 1 / RAM | .117 [.116, .144] | .118 [.117, .125] |
| 1 / HOST | 16.952 [6.684, 17.606] | 17.418 [8.039, 19.477] |
| 1 / npfs | 17.722 [8.477, 18.534] | 18.013 [8.433, 19.338] |
| 4 / RAM | .118 [.117, .343] | .121 [.117, .122] |
| 4 / HOST | 17.049 [9.886, 17.274] | 15.409 [8.943, 17.475] |
| 4 / npfs | 19.431 [7.031, 20.364] | 18.125 [7.853, 19.323] |

TCC seconds before → after: one CPU .010/.009/.009 → .010/.009/.009,
also repeated unchanged on final ports; four CPUs .009/.012/.008 → .011/.011/.011.
The authored source stays fixed; SDK headers evolve (2649 lines/110059 bytes
before, 2741/113440 after). All timed commands completed successfully.

## Manual qualification and limits

QEMU probes passed on RAM, HOST and installed npfs: delegated aliases,
rename preserving held identity, recreated/replacement names differing from
held victims, retained victim I/O, write-only FILE metadata, zero-right
DIRECTORY metadata, separate volume domains, sampled writable times, unavailable
archive time and helper failure clearing outputs. HOST hardlinks matched;
a debugger pause between two held-handle queries plus host `utime` showed
fresh -2s/500000000ns then 1s/750000000ns with unchanged identity.

Shell refused home-root aliases, HOST hardlinks and a later pipeline output
alias before truncation; original bytes remained. Text snapshot → home output
succeeded despite unavailable object identity, and partial stat retained only
its domain validity. A qualification-only instance of the baseline text provider
(unsupported INFO) exercised size-only stat and unknown-domain redirect refusal
without truncation. Its init addition was absent from all timed boots.
TCC compiled HOST hardlink/root aliases and home-root
aliases. Same-path replacement while paused distinguished the new header.
Missing identity and an error after once produced diagnostics. Recursive aliases
retained one stream per object: at the third include GDB saw two native wrappers
(retained stream plus candidate), returning to root mappings after cleanup.

Allocation/exhaustion and malformed-reply unwinding were inspected, not injected.
No bind-alias, after-full-close, cross-boot, mutation-lease or physical-host
identity/performance guarantee is claimed. Once retention consumes descriptor
and stream backing until translation-unit cleanup; existing limits diagnose failure.

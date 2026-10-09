# Clipboard first-delivery qualification

Baseline captured 2026-10-09 before implementation at Pyxis `c1e5f3be`,
userland `72f303d6`, ports `8b918c46`; compiler image
`pyxis-llvm23.1.3-49e2c1a`. The ordinary `make -j16 image` build passed.
The accepted contract is in [the clipboard milestone](../wip/clipboard.md).

QEMU 10.2.2: Q35, nested KVM, `-cpu max`, four CPUs (one socket, four cores,
one thread), 512 MiB, UTC RTC, fresh raw OVMF, modern VirtIO GPU/SCSI CD/RNG,
relative PS/2, no NIC/disk/USB/audio, `-display none`, 1280x800. For matching
runs only, staged `config/live.lua` adds a `mux` space using `init-readonly`,
with `launch`, `multiplexer`, `screenshot` and read-write RAM `home`. The
configuration is local qualification data; the shipped profile is unchanged.

Manual HMP input with read-only GDB (`set may-call-functions off`) measured four
`space_present` entry-to-return intervals per workload. Existing HPET ticks at
`0xfffffe80402020f0` are 10 ns. Commands: hardware breakpoint, read tick into
GDB convenience variable, `finish`, subtract ticks, continue. These include
debugger/scheduler variation and do not measure native input/paste latency.

| Workload | Baseline samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Caelum idle | 1.99936, 1.59899, 1.62924, 1.48254 | 1.61411 | 1.48254–1.99936 |
| Caelum completed selection | 2.01465, 1.41075, 2.09701, 2.07252 | 2.04359 | 1.41075–2.09701 |
| Local shell idle | 1.69770, 1.97172, 1.64445, 2.03110 | 1.83471 | 1.64445–2.03110 |
| Mux one pane idle | 2.03695, 1.24358, 1.29716, 1.51210 | 1.40463 | 1.24358–2.03695 |
| Mux completed selection | 2.21552, 1.29504, 1.35677, 1.56633 | 1.46155 | 1.29504–2.21552 |

Caelum selection used first-row cells 1–13; local shell was idle; mux selection
highlighted its prompt at content row 0, columns 0–6. Input ran while the VM
executed. Raw logs, captures and baseline ISO/kernel/configuration hashes stay
local under `build/clipboard-baseline`; no test programs or automation were
added. All baseline QEMU/GDB processes were stopped before code changes.

The earlier baseline on `6df2bdd8` predates the fixed click rule and is not used
as this delivery's matched comparison. Main includes the merged correction;
the owner also confirmed it natively in boot log, raw terminals and mux.
Implementation and matched qualification results will be recorded below.

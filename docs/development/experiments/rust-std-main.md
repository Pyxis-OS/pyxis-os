# Rust std main: baseline

2026-10-25, gate 2 of the [std milestone](../../wip/rust-std.md).
The following controls precede adapter/runtime edits. The lexical component
contract was accepted afterward (chronium, 2026-10-25); implementation is in
progress and no after result is claimed.

Source main `eff8dbb2bf38d8e5ce03b18add5fdcd1a3c4b012`, userland `88872fe3`,
ports `e6c51d9d`, fs `f3f22b6e`, lwIP `d52ebd20`. Ordinary kernel/SDK and native
userland builds pass with unchanged builder LLVM 23.1.3-3d69e83. Required port
libraries are reused from the same unchanged recipes, not rebuilt as all ports.
The private fixture uses fresh native programs and the existing qualified
core+alloc samples; it has RAM home and ordinary Remote launch authority.
No game data, disk, native profile, kernel instrumentation or ABI change.

Nested QEMU/KVM q35, host CPU, one/four vCPUs, 2 GiB, OVMF, modern VirtIO
GPU/net/RNG, VirtIO-SCSI read-only ISO, UTC session and host-loopback Remote/GDB.
No concurrent build runs during the measured envelopes; GDB is detached.
The existing Remote machine client keeps input open through shell exit/drain.
After 16 warmup children, three fresh sessions per CPU count execute:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
exit
```

| 1,024-child whole Remote session | Baseline samples (s) |
| --- | --- |
| One CPU | 0.815, 0.815, 0.815 |
| Four CPUs | 0.865, 0.915, 0.865 |

Every recorded program exits zero and the final shell result reports zero and
`drain=complete`. These are whole-session host-wall observations, including Lua,
launch/exit, Remote and drain, not isolated loader or native-hardware timings.
There is no after result or performance conclusion yet. Repeat against the same
fixture and devices after implementation, retaining variation and CPU count.

Read-only GDB snapshots of PMM allocated frames, after the four-CPU launch warmup:
9,713 with no active Remote shell, 10,104 with one shell held after an `echo`,
9,715 after that session exits. Page size is 4 KiB. These whole-guest counts
include kernel/page-table/cache backing; the active-session delta is 391 frames
(1.527 MiB), not a deduplicated process-owned resident census. Initial pre-warmup
idle is 6,617 frames; retained growth during warmup is recorded without a cause
or leak claim. Matched after-runs must use the same warmup and observation points.

Raw image/configuration hashes, exact commands, serial, client JSONL/completion
records and debugger captures stay in `/shared/present/rust-std-gate2/baseline/`.
No native/PXE machine is accessed. No Rust std, threads or hexyl result is claimed.

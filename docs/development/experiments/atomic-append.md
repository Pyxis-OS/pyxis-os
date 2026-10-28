# Atomic append qualification

## QEMU

The native, libc/shell and language slices passed on QEMU/KVM q35, host CPU,
3 GiB RAM, one and four vCPUs: RAM, RAM-/disk-backed npfs, disposable writable
FAT32, capture, zero probes, rights/CUT, aliases/hard links, concurrent tagged
records, returned positions, explicit positioned writes and HOST refusal before
effects. Shell checks covered descendants, both `2>&1` orders, independent and
repeated-new-target redirects, root aliases, capture bytes/value handoff and
HOST plain-output relay. FAT write authority was explicitly declared.

SDK/C++ runtime, kernel, affected Lua/libuv/sbase, Go/Git/Caddy, Rust stage-2 and
LLVM hosted/development closures built. Caddy was link-only because the fixture
lacked its declared TCP grant. Hosted Clang C compilation, separately launched
LLD linking and clangd initialize/shutdown passed. Earlier loaded clangd runs
reported `Resource busy`; the variation is unattributed.

## Nested measurements

Before: parent `e13b8f35`, userland `4060486d`, ports `4f2e8aeb`, fs `f3f22b6e`;
builder `pyxis-llvm23.1.3-3d69e83`. QEMU profile above, four vCPUs, 960×752
VirtIO display, unprofiled. RAM-npfs used a live sparse pool; disk npfs used a
128 MiB VirtIO pool with an 8 MiB journal. No timed path included SYNC.
After: parent `297e48a4`, userland `79af144`, ports `24e7d6a`.

| Workload | RAM-npfs before / after (ms) | Disk npfs before / after (ms) |
| --- | ---: | ---: |
| iobench 1 MiB, 4,080-byte WRITE | 6.905 / 8.922 | 6.983 / 2.521 |
| iobench 1 MiB, 64-byte WRITE | 305.390 / 319.368 | 338.981 / 321.084 |
| Lua `w`, 8,192 × 64-byte line + flush | 166.154 / 173.276 | 158.862 / 73.357 |
| Lua `a`, same loop | 304.126 / 176.525 | 318.062 / 180.099 |

Commands: `boot://iobench.pxe write ROOT/file --buffer 4080 --rounds 1`
(or `--buffer 64`), and a Lua 5.5.1 loop writing 63 `L` bytes plus newline,
explicitly flushing each line. Lua's native monotonic clock excluded open/close
and content verification. iobench had one built-in warmup; Lua had none.
Each cell is one timed sample, not a median or range estimate; outputs were checked.

Because main changed and WRITE controls varied, retained/rebuilt Lua binaries
were also paired on the same new kernel and fresh scratch pools. Append fell
300.520 → 171.352 ms on RAM-npfs and 306.926 → 168.757 ms on disk npfs (43–45%).
The apparent ordinary WRITE slowdown did not reproduce with reversed execution
order: RAM 170.757/172.085 ms, disk 173.986/156.965 ms. Native APPEND removes the
per-chunk SIZE call; short-interval scheduling/cache variation prevents stronger
claims. These are nested cached-mutation measurements, not native throughput
or media durability results. Small-call native APPEND was approximately the
ordinary WRITE cost; the short 4,080-byte RAM comparison reversed direction
across cohorts and was inconclusive.

## Native vela RAM smoke, 2026-10-27

The orchestrator ran the sealed packet hands-free on standard PXE main
`297e48a4`, beacon `vela`, and recorded:

```text
APPEND_NATIVE COMPLETE C=PASS RUST=PASS GO=PASS LLVM=PASS CAPTURE=PASS CLEANUP=PASS
```

A unique `tmp://` RAM leaf was removed after C/Rust/Go/LLVM append, cursor,
alias and positioned-write checks; C/Rust/Go concurrent records and Rust inherited
output/held capture passed. Payload provenance: userland `1006c96`, Rust
`e635120a`, LLVM `8bdf1360`, Go `7327791c`. Final integration rebases preserve the
libc latch patch and Rust append source/tests; the older artifacts were not
relabelled as rebased heads. Adoption requires matching SDK/prefix rebuilds,
not a new cross-compiler container.

The image contains #1508 native APPEND but predates #1515's shell. This is native
RAM correctness evidence only: newer-shell native coverage, persistent media,
durability, injected lost replies/short writes and teardown heap reclamation
remain unqualified. See the [reference limits](../../interfaces/atomic-append.md#durability-and-qualification-limits).
Small raw evidence/provenance is retained under
`/shared/present/atomic-append/{baseline,task1,task2,task3/final,native-quick}`;
the native result is `native-quick/results/20261027-165300/`.

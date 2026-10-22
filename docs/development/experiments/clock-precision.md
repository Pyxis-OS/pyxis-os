# Explicit clock precision qualification (2026-10-22)

Task 3 of [clock reads](../../wip/clock-reads.md). The qualification DATA-package
choice is accepted (chronium, 2026-10-22). Older toolchain timings are context.

## Fresh nested baseline

Current-main `a662e49d`, matching SDK/userland `c0b9883f`, ports `dd2882d5`,
fs `f3f22b6e`, existing compiler container `pyxis-llvm23.1.3-3d69e83`.
Ordinary source image build and interactive installation onto an 8 GiB raw disk.
Three independent clones of the pristine installed disk/firmware variables:
q35/KVM, four CPUs, 4 GiB, VirtIO block/network/RNG, OVMF, VM-local storage,
cache=writeback, `-cpu host,+invtsc`. The outer VM now exposes invariant TSC;
all guest boots select KVM clock. This replaces the old nested configuration.

`pkg --timeout 1200 install clock-clang-data@frozen-3e1a92d1` reads the private
HTTP repository. The frozen Clang tree is DATA, with no commands and never
executed: 8,778 files, 373,116,928 archive bytes, SHA-256
`170a8e2f4893f0e6c7c978bf0d5a2505f25a901514d08c824a185c0936feb0a7`.
Keep this archive byte-identical after the change; program ABI checks stay intact.

| Before sample | Verify display (s) | Extract zero → first positive (s) | Extract zero → total display (s) |
| --- | ---: | ---: | ---: |
| 1 | 55.83 | 17.05 | 82.37 |
| 2 | 39.42 | 14.68 | 85.24 |
| 3 | 84.13 | 39.70 | 123.15 |

All three commands exit 0 with complete Remote drain. Verification median
55.83 s (39.42–84.13), first positive 17.05 s (14.68–39.70), displayed extraction
85.24 s (82.37–123.15). These are shared nested-VM observations with substantial
variation, not exclusive-host measurements. Display intervals include Remote
delivery and about 200 ms redraw quantization; first positive is 2/8778 after
the large Clang executable, and displayed total precedes final sync/publication.
No debugger/perf runs alongside accepted samples. A setup boot with the wrong
disk-template path was stopped before pkg; it is not a sample.

Raw configuration, clone commands, baseline image hash, captures and host
`vmstat` remain under `/shared/present/hpet-hot-reads/task3/`. The before fixture
is retained for matched after qualification. Implementation/after checks and the
separately scheduled native DATA-package pair are pending.

# Clock-read baseline (2026-10-21)

Pre-change evidence for the [clock interface](../../interfaces/clocks.md).
No clock or extraction algorithm change. The earlier Opus profile in the
assignment is separate evidence; these are delta's captures.

## Workload and configuration

`pkg install clang-hosted@b6f881636c099656908caaa0dcdfb9b8d9e79cc36345cf4275d860599ba21098`
from the same LAN public repository: 373,117,440 archive bytes, 8,778 files,
SHA-256 `3e1a92d1a57425158ee21a245a0fe280882d941854adb3f4f53c3ce329c75f6d`.
Each sample extracts a fresh tree rather than reusing an immutable revision.
An explicit `pkg verify clang-hosted` also passes in every native sample.

- **Nested:** own installed 0.0.7a image, kernel/userland `81313e0e`, q35/KVM,
  `-cpu host` without invtsc, 4 vCPU, 4 GiB, VirtIO block/network/RNG and OVMF.
  HPET direct 64-bit selected. Three serial fresh clones of the same 8 GiB raw
  disk on VM-local storage, QEMU cache=writeback. The frozen image SHA-256 is
  `ca5bff1faaffcac57fe4b156b66bd13c69310d3814ce86105cc0ab6966167b2b`;
  preserve it for the after-run. Kernel/ABI/fs/ports inputs are unchanged between
  this release and `c35788ec`; newer native userland routes DHCP logs differently.
- **Native:** ThinkPad T14, 12 CPUs, selected TSC about 2096.06 MHz,
  `RDTSCP+LFENCE`; current main `c35788ecd3d555591350c53b4654bc34acdb5604`,
  userland `cf731eff`, fs `f3f22b6e`, ports `053d5714`. Sole disposable 32 GB
  SanDisk directly attached, AC verified before/after each of three fresh boots,
  built-in Ethernet, `nvme.write=0`. Luna ordinary kernel SHA-256
  `62c0516dd01f1b98c54a36b84e6e70d9c2fb35fec6306a84d702d861b9898c40`.
  Private initrd `f236b206cbebbc74fdfcdbd368cb87c5148632d410948794ebdef17bb9096965`
  reuses the guarded durability packet: exact scratch identity, RAM home, no
  FAT32 work, cleanup restricted to its marked package root. Measurement-only
  userland brackets fetch and extraction through final staging-directory sync;
  it prints results after success. The kernel is unmodified.

## Results (seconds)

| Native sample | Fetch (guest) | Verify display | Extract zero → first positive display | Extract + sync (guest) |
| --- | ---: | ---: | ---: | ---: |
| BEFORE1, cold first sample | 11.27 | 14.62 | 12.27 | 75.71 |
| BEFORE2 | 5.95 | 7.72 | 11.14 | 72.49 |
| BEFORE3 | 5.98 | 7.81 | 10.43 | 69.03 |

Native extraction+sync median 72.49 s, range 69.03–75.71 s. First-sample
fetch/verification are colder; report all three rather than discarding it.
All captures pass identity, phase observation, verification and guarded cleanup.
The controller restored the PXE menu byte-for-byte and removed its staged
payload. The subsequent ordinary image answered `hostname` with `pyxis`, exit
0 and complete drain over the `t14` beacon (orchestrator capture).

| Nested sample | Verify display | Extract zero → first positive display | Extract zero → total display |
| --- | ---: | ---: | ---: |
| 1 | 432.82 | 141.36 | 624.00 |
| 2 | 436.08 | 138.71 | 665.09 |
| 3 | 435.39 | 118.02 | 571.77 |

Nested verification median 435.39 s, range 432.82–436.08 s; displayed extraction
median 624.00 s, range 571.77–665.09 s. All three commands exit successfully
with complete Remote drain. A pre-command listener refusal is retained separately
as setup failure; the replacement clone had a 30 s startup settling interval.

A separate **fetch-only**, profiled 10.084 s window on sample 1 counted 951,794
HPET MMIO reads across the four vCPU threads: **94,388 reads/s**, approximately
**31,463 clock samples/s** for the high/low/high reader (retries can add reads).
Command: `perf stat -t VCPU_THREADS -e kvm:kvm_mmio --filter
'type == 1 && (gpa == 0xfed000f0 || gpa == 0xfed000f4)' -- sleep 10`.
The profile ended before verification; the reported verify/extract intervals
are unprofiled. These counts establish frequent reads, not their share of time.

## Interpretation and reproducibility

Display intervals are host-monotonic receipt times of existing Remote redraws,
quantized at about 200 ms and including delivery delay. The first positive
display is **2/8778**, after the 90,417,950-byte Clang executable; it includes
real file writes and does not isolate an idle pre-file stall. Displayed extract
100% precedes final directory sync, immutable publication and command completion.
Nested display intervals and native guest extraction+sync are different
envelopes. Shared-host/nested scheduling and storage caches introduce variation;
sample 1 also had ordinary preparation activity during fetch. No exclusive-host
or causal clock-cost claim is made.

Raw records and private helper provenance remain outside Git under
`/shared/present/hpet-hot-reads/`: `baseline-delta/` contains `qemu-before-1..3`,
`hpet-fetch-perf.json`, `capture.py` and `qemu-runs.py`;
`thinkpad-clock-baseline/` contains `PROVENANCE.md`, `QUALIFICATION.json`,
`SHA256SUMS`, `profile/private-userland.patch`, `native-run.log` and
`results/BEFORE1..3` plus
`results/normal-return-hostname`. No personal vela volume was used.
Repeat the same installed image/VM, archive, commands and display envelopes
after the change, and the native packet with fresh extraction and guest timing.
The older durability 78.5/76.2 s results are context, not a matched clock pair.

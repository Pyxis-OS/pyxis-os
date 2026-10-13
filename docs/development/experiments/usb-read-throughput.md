# USB read-throughput native baseline

Owner/orchestrator, ThinkPad T14 Gen 1 AMD, 2026-10-13; PXE with the same SanDisk
USB 3.2 Gen1 toolchain stick, port and private Clang task-5 consumer archive.
Ordinary base `7700714a4083dd92535894f7bbaea9a959e51dc1`, `LOG_LEVEL=info`,
kernel SHA-256 `c2c5eb79a23ac7109dc49c05f33797efd30eadae31ab57809e7d8e12f01951d9`.
Unmerged probe `a0cc3f499a6fe8a549d1f200d7125e70e1599ad6`, trace,
kernel SHA-256 `6bbf5c5aac5cd94c50881ef778c410e72f177b8b80ef3d7ba1d5ead837cc52f0`.
Pins: userland `e3c6ff9f`, ports `4ed452df`, fs `414be0ac`, lwIP `a1aadb91`.
Builds used `pyxis-llvm23.1.3-4c23a20`, `make -j8 kernel` (probe: `LOG_LEVEL=trace`).
Probe branches stay unmerged; no throughput change has been made.

## Commands and conditions

The hands-free wrapper used existing `pyxis-remote --machine --no-shell-echo`,
adapted by the orchestrator to `--listen t14 0.0.0.0 2323` beacon mode because
the target does not accept direct connections. Each of three rounds ran an empty
session, `<delegated Clang .pxb URI> --version`, and hello `-c` with
`--sysroot=sysroot://`; source/object fixtures were unique `tmp://` files.
Command results, object hashes and FINAL/drain were checked; every required
workload passed. Optional inventory failed: fastfetch/lsusb were absent from the
private archive. Machine/port provenance comes from the owner, not those commands.

| Ordinary host wall seconds (ordered samples) | 1 | 2 | 3 |
| --- | ---: | ---: | ---: |
| Empty | 0.00525 | 0.00624 | 0.00588 |
| Version | 4.56489 | 4.29755 | 4.29894 |
| Hello compile | 4.31166 | 4.30873 | 4.30435 |

The probe boot added `probe.usb_read=1 probe.usb_partition=1 log.udp=1` to the
existing exact operator-local disk selection. It read only the first 32 MiB of
that GPT partition: a full-region 64-KiB warmup (0.740 s), then reordered rounds
4/8/16/32/64 KiB; 16/32/64/4/8; 64/32/16/8/4. No concurrent compile during
those reads. All 15 passes completed 33,554,432 bytes, zero retries/status and
matching last-4-KiB tails. Tail comparison is not whole-region integrity proof.

| Request | Round 1 seconds | Round 2 | Round 3 | Decimal MB/s range |
| --- | ---: | ---: | ---: | ---: |
| 4 KiB | 2.04482 | 1.80338 | 1.55180 | 16.4–21.6 |
| 8 KiB | 0.95884 | 0.89943 | 0.89948 | 35.0–37.3 |
| 16 KiB | 0.60366 | 0.60962 | 0.58045 | 55.0–57.8 |
| 32 KiB | 0.41046 | 0.40940 | 0.39808 | 81.7–84.3 |
| 64 KiB | 0.29598 | 0.29519 | 0.29131 | 113.4–115.2 |

Six Clang captures (version and compile) each completed 90,347,866 bytes.
First: 22,057 data misses, one hit, 45 metadata misses and 43,535 metadata
hits; 22,102 admitted READs, all 4 KiB (90,529,792 requested bytes), no retries;
45 physical/logical miss runs, longest 512 pages, ideal 1,379 data commands
at the existing 16-page limit. Allocation/read/image load: 0.05282/4.34429/0.12796 s.
Remaining five: 1,023 data hits, 21,035 misses, no metadata misses, 21,035
4-KiB READs (86,159,360 bytes); 43 runs, ideal 1,316 data commands.
Allocation/read/load ranges: 0.05367–0.05624/4.07808–4.10375/0.12791–0.12852 s.
Ideal counts exclude metadata and grouping overhead. No grouping is implemented.

## Limits and recovery

Probe version/compile wall times were 5.50–6.13 s and empty sessions about 1.07 s:
trace/UDP overhead is material. Do not compare those with ordinary timings as a
code regression or compiler CPU measurement. Raw-size and capture intervals are
instrumented; the ordinary wall baseline is uninstrumented. Fedora comparison
was not run. No native gain has yet been measured. The ThinkPad was restored to
the ordinary Clang task-5 build after capture.

Nested-QEMU qualification used Q35/KVM, four CPUs, 2 GiB, modern VirtIO boot/net
and qemu-xhci/usb-storage with a read-only scratch twin. All 15 passes and Clang
version/hello compile passed. The first raw-size ranges were 7.41–10.61 s at
4 KiB and 0.47–0.64 s at 64 KiB; they are functional observations, not a native
ceiling. Final exact-probe version again completed, with 22,102 4-KiB READs.
An initial restart without modern VirtIO left network selection pending; that
remote setup failure is excluded. All owned QEMU/GDB/client jobs are stopped.

Private raw records, exact command captures, adapted wrapper and native run
sheet remain in the owner's staging packet; no real disk GUID or inventory is
committed. See the [proposal](../../wip/usb-read-throughput.md) for interpretation.

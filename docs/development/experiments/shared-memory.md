# Shared-memory baseline and qualification

## Before-code controls — 2026-10-29

Clean main `20c99b3fa89412b0a108a7c38b428c6593e63fa0`, before shared-memory
implementation. Kernel SHA-256
`5e41933334d6f2dd9deb6d93f7ed448ead257bb50152935e75e40573e593c72d`;
effective config `106bf0ae11de93dc8269178a724abdce370cb1a92164690059774b0c69a3cc8e`.
Published compiler `pyxis-llvm23.1.3-8bdf136`; kernel built with
`make -j16 kernel LOG_LEVEL=info` in that container. No instrumentation.

QEMU 10.2.2, nested KVM/q35, `host,+invtsc`, 4 GiB, UEFI, virtio-scsi CD,
virtio RNG/network and localhost Remote; no disk or native/PXE action. Runtime
clock is KVM with shared monotonic floor; HWP unavailable; eager XSAVE `0x7`,
832 bytes. Development/network use CPU0; Remote/children are allowed CPU0 for
the 1-vCPU boot and CPU1 for the 4-vCPU boot. This is not multi-CPU scaling or
proof of actual execution on a particular AP.

The selected existing app bytes are intentionally frozen for later controls,
not rebuilt or claimed to be today's full userland: userland `8cefca401776`,
ports `18b56bf05ec2`, SDK source `397a22bffdd8`, same compiler. Selected bytes
match their original component checksum lists. Relevant MEMORY/IPC/launch/
system-info ABI headers are byte-identical. The older LOG reader is incompatible
with source-stamped records and excluded; the other differing header is unused
Wi-Fi. Exact member/archive/ISO hashes and component records accompany results.
The 1/4-CPU archives differ in the declared Remote CPU allowance.

The existing collector, launch loop and parsers run these unchanged workloads:

- 16-launch warmup, then 1,024 verified `echo` launches through retirement.
- `allocbench pages --size 65536 --live 8 --rounds 100` (800 attempts/releases).
- `session ipcbench {call,send} --size {64,4096} --messages 256 --rounds 100`,
  one warmup then 100 measured passes per case, with exact message/byte counts.

All workloads and FINAL drains pass. Guest elapsed intervals are separate from
Remote wall time. One successful timed boot per configuration; IPC cells are
median [min,max] milliseconds of passes within that boot, not independent boots.

| Guest interval | 1 vCPU | 4 vCPUs |
| --- | --- | --- |
| 1,024 launches, ms | 791.802 | 786.522 |
| Private pages, ms | 18.290 | 17.474 |
| CALL64 completion, ms | 1.270 [1.250, 2.199] | 1.223 [1.214, 1.266] |
| CALL4096 completion, ms | 1.432 [1.385, 1.961] | 1.367 [1.339, 1.408] |
| SEND64 completion, ms | 0.655 [0.638, 0.718] | 0.651 [0.635, 0.667] |
| SEND4096 completion, ms | 0.709 [0.681, 0.771] | 0.7045 [0.675, 0.757] |
| SEND64 admission, ms | 0.077 [0.076, 0.095] | 0.077 [0.076, 0.092] |
| SEND4096 admission, ms | 0.107 [0.100, 0.163] | 0.108 [0.101, 0.131] |

This establishes no boot-to-boot variation, regression verdict or shared-memory
functionality. Three untimed setup failures (omitted session/Remote assets,
missing timezone data and incompatible old LOG reader) are preserved separately;
no timed workload began in them. Boot identity uses the exact staged kernel,
ISO, configuration and QEMU command plus serial captures, not a guest revision
query. Both owned guests stopped.

Small evidence: `/shared/present/shared-memory/baseline/`. Frozen local build,
inputs, existing collector adaptation and full raw records:
`/home/chronium/build/shared-memory/baseline/`. Reproduction uses
`python3 run_baseline.py 1` or `python3 run_baseline.py 4` from that directory
with fresh output paths. Keep identical control bytes for matched after-runs;
no repeated runs merely to increase sample count. Shared-memory behavior and
native qualification remain tasks of the [milestone](../../wip/shared-memory.md).

# Native address parking qualification

Pre-code A: main `d124a05387d22e6aa43b52cb210f46ea8a8861ec`, including #710,
#714/#718 and userland #216/#217. Exact-head CI #1915 supplied matching kernel,
SDK, userland and ports bundles; ordinary bundled image assembly and host-client
build passed. No gate-6 code existed when the 1/4-CPU A1 baselines were captured.

QEMU 10.2.2, Q35/nested KVM, CPU host, one/four cores, one thread/core, 2 GiB,
UTC RTC, matching raw OVMF/fresh VARS, standard VGA/display none, modern VirtIO
SCSI CD/net/RNG. Info logging, no profiler/debugger or task-owned build during timing.
UDP NAT forward 2324; existing machine client waits for FINAL 0/complete.

Per fresh boot: CALL/SEND × 64/4096 at 256 messages/100 rounds; then 16-launch
warmup and three 1024-launch Lua runs. Commands follow [task 5](../threads-task5/README.md#interleaved-nested-qemu-comparison).
All 800 IPC passes verified, all launch counts matched and all invocations reached
FINAL 0/complete. Clock-read cost was not subtracted.

| Workload (ms per 256 messages) | 1-CPU A1 median (range) | 4-CPU A1 median (range) |
| --- | --- | --- |
| CALL64 completion | 84.920 (83.678–108.405) | 105.623 (75.731–761.895) |
| CALL4096 completion | 84.641 (83.911–270.759) | 103.113 (76.533–537.711) |
| SEND64 completion | 13.098 (12.874–13.774) | 15.746 (11.758–36.950) |
| SEND64 admission | 1.204 (1.183–1.681) | 1.304 (1.174–4.356) |
| SEND4096 completion | 13.231 (12.965–14.537) | 13.316 (11.396–20.806) |
| SEND4096 admission | 1.289 (1.258–1.518) | 1.293 (1.219–2.135) |

1024-launch host-client wall seconds, including discovery/session setup:
1 CPU `3.201 / 3.187 / 3.167`; 4 CPUs `3.307 / 3.144 / 3.157`.
The 4-CPU CALL ranges are wide; these are shared-host nested measurements, not
native speed claims. Interleaved A/B and owner-native A/B remain pending.

Frozen baseline SHA-256: ELF `ea37cfaed391bc15b3e30bfb5fe6b30f9d1cc675f02e01c35c7c22b4d038933b`;
initrd `d0ca6f8c938d113f7b8ba32c20653416af0b7f2dff50288a2331cf34b7f4e053`;
ISO `e217c7398d334b982342d3e7a84d0442b44fc419723fa7f1c458f54c9da4feed`.
Keep A's initrd/config unchanged for timings; qualification may use the new SDK/helpers.

Implementation, ordinary source build and native parking/race debugger qualification
are not yet complete. No new benchmark/test infrastructure or boot logging.

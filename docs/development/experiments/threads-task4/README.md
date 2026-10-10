# VM activity and user-buffer lease qualification

## Pre-code baseline

2026-10-10, clean main A `476b2a332c05e8cbe577b3d36bf08e24cd3e0a27`, after
merged #684. All four bundles from exact-head successful
[#1802](https://git.internal/PyxisOS/pyxis-os/actions/runs/1802) verified; ordinary
`make -j16 image PREBUILT="kernel sdk userspace ports" REMOTE_BEACON=t14` passed
with LLVM 23.1.3 builder `49e2c1a`. Cached owner-mirror AX200 firmware reused;
initial offline assembly lacked that cache and failed before boot. No upstream
fetch or compiler rebuild. Userland `796087b11fab0a89257b55ec047479c23a3e40cb`,
ports `43eec1937c0db8341b0ad28bcda1352b29f55e28`, fs
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

Frozen SHA-256: ELF `e9fb1fced62e12657b96ffc157545dca7a07099612b0fda0795a33b1bc3ca8a6`,
ISO `97bd7379b790ecde175ecbc4df93cd4c13c01cdd17a3322fe14ad692551cac9f`,
initrd `fe140ac9c0ca86076bf3fc91a943abf3099260b63b88df7bf55e971f5040ec6b`.

QEMU 10.2.2, Q35/nested KVM, CPU host, 1 or 4 cores/one thread per core,
2 GiB, UTC RTC, raw OVMF/fresh VARS, modern VirtIO SCSI CD/RNG/net, standard VGA,
display none, info logs/profiling off. No debugger/build during measurements.
Reverse client `--machine --no-shell-echo --listen t14 --beacon-address 127.0.0.1
0.0.0.0 2323`, UDP NAT forward 2324. Loopback beacon is QEMU's NAT adaptation;
owner native commands omit it.

Each boot: call/send 64/4096 bytes, 256 messages/100 rounds; 16-child launch
warmup then three 1024-child Lua launch sessions; three `allocbench pages --size
65536 --live 8 --rounds 100` processes. All 800 timed IPC passes verified with
zero failed CALLs/rejected SENDs; all FINAL records 0/complete. Each page run had
800 allocations/releases, zero failures. All launches reported expected counts.
Launch wall time includes discovery/transport; clock calibration is not subtracted.

Median (range), milliseconds except launch seconds:

| Workload | 1 CPU A1 | 4 CPU A1 |
| --- | --- | --- |
| CALL64 complete | 84.023 (83.505–87.398) | 75.047 (67.678–106.626) |
| CALL4096 complete | 84.257 (83.613–85.429) | 72.759 (67.156–98.132) |
| SEND64 admit / complete | 1.181 (1.168–1.384) / 12.957 (12.734–13.467) | 1.193 (1.071–1.905) / 12.186 (10.656–21.729) |
| SEND4096 admit / complete | 1.244 (1.221–1.714) / 13.122 (12.845–18.684) | 1.257 (1.180–2.140) / 11.944 (11.307–17.483) |
| Pages | 64.909 (64.668–65.446) | 59.639 (58.519–60.369) |
| 1024 launches, seconds | 3.15 (3.13–4.29) | 3.19 (3.13–4.10) |

Clock calibration: 32,185–37,541 ns/read across the IPC invocations. Nested-VM
costs are not native performance. Raw captures in ignored
`build/task4-baseline/{1,4}-A1`; VMs/clients stopped before implementation.
Later comparison and native evidence will be recorded separately.

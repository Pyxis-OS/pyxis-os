# Shared-table delivery qualification

Threads task 3, 2026-10-10. Owner accepted prospective grant capture with exact
source recheck and precommit REPLY policy validation/move delivery. Exactly one
user task remains; no public ABI change, new klog lines or new test infrastructure.
Implementation and post-change qualification are pending.

## Pre-code baseline

Clean main A `b0d0bc077b96f908a2041f69279e83bce5efd452`, after merged #671.
All four bundles from successful exact-head [#1747](https://git.internal/PyxisOS/pyxis-os/actions/runs/1747)
were verified, then ordinary `make -j16 image PREBUILT="kernel sdk userspace ports"
REMOTE_BEACON=t14` passed using the existing LLVM 23.1.3 builder `49e2c1a`.
No compiler rebuild or upstream fetch. Host client: `make -C tools remote`.
Inherited pins: userland `1162d7299dffdf2c183a7a2b15cd6dfc08724bec`, ports
`f510e21286dfc1f8ae7f0e5f8c209d31698356ed`, fs
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

| Frozen input | SHA-256 |
| --- | --- |
| A ELF | `49b751ecea39e8c284aa74e8c273a9849f16305c07ebe8e9b9c925ee9fe91927` |
| A ISO | `9a7f496139e587b8fdfa342dc517b191a17adf7e3a87593f1407a6ac228da947` |
| Shared staged initrd | `6210eb078ba9955b49402185020143765efef784b9d471b6639058cb890dd9da` |

QEMU 10.2.2, Q35/nested KVM, CPU host, one socket with 1 or 4 cores and one
thread/core, 2 GiB, UTC RTC, matching raw OVMF/fresh VARS, modern VirtIO
SCSI CD/RNG/net, standard VGA/display none. Info logs, profiling off; no debugger
or own build during timings. Reverse client with `--machine --no-shell-echo
--listen t14 --beacon-address 127.0.0.1 0.0.0.0 2323`, UDP NAT forward 2324;
GDB port 12673. Loopback beacon is only the documented NAT adaptation.
All baseline clients/QEMU stopped before code; raw captures in ignored
`build/task3-baseline/{1,4}-A1`. Nested-VM measurements are not native costs.

Each boot ran call/send at 64/4096 bytes, 256 messages, 100 rounds. All eight
invocations returned verified warmup, 100 verified passes, zero failed CALLs or
rejected SENDs, summaries and FINAL 0/complete: 800 timed passes total. Native
follow-up can use the same [hands-free loop](../../../userland/remote-terminal.md#consumers-and-limits)
with `--rounds 100`. Clock calibration is retained, never subtracted.

Launch: `session bin://lua.pxe -e "for i=1,1024 do assert(pyxis.run{'echo','-n'}==0)
end; print('launches=1024')"`; 16-child warmup and three timed sessions per CPU,
host `/usr/bin/time -f %e` including reverse discovery, launch, output and cleanup.
The session grants allow launches without altering the staged Remote policy.
Page workload: `allocbench pages --size 65536 --live 1 --rounds 64`, three fresh
processes/CPU; 64 allocations/releases and zero failures each. This exercises
BSP allocation and scheduler notification; actual IPI path inspection is separate.

Initial A1 medians (range), milliseconds except launch seconds:

| Workload | 1 CPU | 4 CPUs |
| --- | --- | --- |
| CALL64 complete | 87.044 (86.122..91.393) | 98.968 (75.310..123.024) |
| CALL4096 complete | 87.249 (86.368..99.328) | 95.763 (75.399..132.791) |
| SEND64 admit / complete | 1.189 (1.165..1.985) / 13.591 (12.737..14.212) | 1.199 (1.117..1.866) / 14.772 (11.226..23.509) |
| SEND4096 admit / complete | 1.252 (1.236..2.524) / 13.713 (12.930..16.943) | 1.262 (1.203..2.598) / 12.900 (11.475..25.019) |
| 1024 launches, seconds | 4.30 (3.36..4.33) | pending extraction |
| Pages, ms | 5.251 (5.232..6.280) | 4.886 (4.868..5.018) |

After code, complete matched per-CPU A1/B1/A2/B2 boot order using the exact same
initrd and configuration, then record manual 1/4-CPU debugger qualification and
exact submitted-head CI. No speedup, zero overhead or sibling-race claim yet.

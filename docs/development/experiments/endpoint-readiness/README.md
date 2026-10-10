# Endpoint receiver readiness qualification

Recorded 2026-10-10. These are manual nested-QEMU measurements, not native
throughput or pure kernel CPU accounting. Raw reports and debugger captures stay
in ignored `build/endpoint-{baseline,matched,reverse,provider}` directories.

## Inputs and throughput

Unchanged main `3c3f9cfe` was measured before kernel changes. The implementation
is `cf8984c3`; the paired images use identical userland `51bcb56b`, ports
`7c33f3ff`, fs `b427df29`, lwIP `a1aadb91` and SDK contents. Ordinary LLVM 23.1.3
builder `49e2c1a` source/image builds passed. Kernel-only builds reused verified
SDK/userland/ports bundles; no compiler rebuild or benchmark infrastructure.

QEMU 10.2.2 Q35, nested KVM, `-cpu host`, four CPUs (one socket/four cores/one
thread), 2 GiB, raw OVMF CODE/VARS, ISO boot, standard VGA, UTC RTC, modern
VirtIO network/RNG and user networking. No USB or HDA devices were added.
After the initial baseline, one QEMU alternated baseline/implementation ISOs
with manual monitor `change pyxis_cd IMAGE raw` and `system_reset`, in order
CALL 64, SEND 64, CALL 4096, SEND 4096. No debugger or build ran during timing.

Each command uses an untimed verified warmup and five timed passes, 256 messages
per pass, fresh endpoints and no payload attachments:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 5
```

Repeat with `--size 4096`. Session handoff replaces the shell; reboot between
commands. CALL completion includes echoed replies. SEND admission measures only
admission intervals; completion includes DRAIN acknowledgments. Every reported
pass verified its payload, with zero failed CALLs/rejected SENDs and all messages
consumed. Milliseconds per 256 messages, median (range):

| Workload | Before code | Interleaved baseline | Interleaved implementation |
| --- | --- | --- | --- |
| CALL 64 completion | 76.712 (74.415–91.964) | 110.837 (77.670–133.756) | 101.233 (92.955–108.415) |
| SEND 64 admission | 1.236 (1.228–1.262) | 1.197 (1.191–1.267) | 1.238 (1.199–1.324) |
| SEND 64 completion | 14.647 (11.797–16.061) | 17.990 (16.767–18.364) | 16.624 (13.358–21.136) |
| CALL 4096 completion | 134.678 (108.249–164.009) | 107.772 (92.463–109.143) | 86.190 (81.484–94.033) |
| SEND 4096 admission | 1.286 (1.255–1.373) | 1.338 (1.285–1.744) | 1.458 (1.261–1.504) |
| SEND 4096 completion | 14.769 (13.138–18.164) | 12.531 (12.216–18.588) | 18.790 (13.961–24.627) |

SEND admission medians rose 3.4%/9.0%; 4 KiB completion rose 50%, while CALL
medians fell. Ranges overlap; these runs do not isolate a stable kernel cost or
establish a speedup. The added IPC path checks the observer count, skipping
readiness scans/notifications without interests. Watched transactions compare
bounded queue/notice state before and after mutation, then notify after unlock.
No allocation, clock reads or extra locks were added to those transactions.

A reverse-order 4 KiB SEND diagnostic pair was retained locally, outside the
table: implementation admission/completion medians 1.303/15.819 ms, unchanged
control 2.944/33.873 ms. That control's clock-read calibration rose to 134,596 ns
from about 37,000 ns in the earlier control. Unchanged-code variation limits any
causal overhead estimate; cheaper timekeeping is outside this task.

## Mixed provider waits

The ordinary full image with userland `6e12b5f` passed these manual commands,
using the same four-CPU QEMU configuration and matching-ELF GDB inspection:

```text
session bin://server.pxe --wait --send
session bin://server.pxe --wait --delivered-timeout
session bin://counter.pxe --wait --withdraw
session bin://counter.pxe --wait --retire-full
```

- An empty provider parked with receiver and console interests, the correct
  process owner, one receiver registration and a null message head. A physical
  key woke console readiness; SEND then arrived through receiver readiness.
- Delivered CALL, further console input during the ten-second wait, then
  asynchronous timeout CANCEL and successful server completion.
- Withdrawal CALL/CANCEL/RETIRE, reuse of the acknowledged export ID and a
  second retirement notice. RETIRE at full capacity preceded sixteen SENDs,
  and the full-queue provider completed successfully.
- No endpoint readiness request remained active after those cases. The receiver
  interest is unregistered before its object reference is released.

Owner/transport denial, shutdown's CLOSED-only mask, unsupported interests,
duplicate observations, stopped-request cleanup and mixed TCP routing were
source-reviewed. No forged capabilities, fault injection or new self-tests were
used. RECEIVE remains blocking and can lose queued CALL readiness to expiry.
The larger completion API and startup/benchmark adoption remain separate work.

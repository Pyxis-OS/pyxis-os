# NCM receive backpressure, 2026-10-18

## Native attribution

ThinkPad, RTL8156BG over SuperSpeed, horse at 2500 Mb/s full duplex, the
same private explicit NCM selection and rotated TX64/RX4/RX64 packet as
[the baseline](../../../wip/usb-ncm-throughput.md#measured-baseline).
Frozen private diagnostic `4732028a` is based on main `2ed89a24`; compiler
`pyxis-llvm23.1.3-abc00d0`, matching main SDK/userland/ports bundles, debug
logging. No game payloads or disk writes. The uninstrumented baseline uses
main `a3efad31`; diagnostic rates are attribution, not an overhead comparison.
Commands are existing `ttcp -t -p 5001 -n 8192 -l 8192 PEER_IP` and
`ttcp -r -p 5002 PEER_IP`, with host-served 4/64 MiB payloads, followed by a
separate SHA256 check. Three rotated rounds:

| Payload | Diagnostic MiB/s |
| --- | --- |
| TX64 | 116.590, 110.513, 109.634 |
| RX4 | 0.859, 0.622, 0.568 |
| RX64 | 0.719, 0.695, 0.731 |

The content hash matches. Device bcdDevice is `0x3104`; actual speed notices
report 2,500,000,000 bps both ways. At the first RX64 snapshot, 617 failed
NTB frame handoffs exactly match 617 provider-full rejections; cumulative
later snapshots match 1130 and 1808. The provider reaches all 16 slots.
No malformed NTB, xHCI error, completion overflow or sequence break is
reported; both IN TDs remain posted at snapshot observation. Downstream
checksum/allocation counters stay zero. The unchanged 16-pbuf TCP out-of-order
limit also sees tail-discard candidates after holes (6271 at the first RX64
snapshot): provider loss is proven, with possible downstream amplification.
Do not infer that every layer is loss-free from successful content checks.

The final boot-log grep fails after the debug ring wraps; this does not make
the completed transfers failures. Raw records remain under
`/shared/present/ncm-throughput/native-diagnostic-01` and the baseline capture
under `native-baseline-02`; payloads/counter dumps are not repository data.

## Isolated candidate

The production change retains a validated NTB and frame cursor in the existing
class buffer. Queue-full arms a wake under the same BSP/IF=0 span as capacity
inspection; slot release notifies the USB worker. No subsequent NTB overwrites
the held suffix. A captured admission epoch prevents replay after withdrawal.
Completion credits reserve storage for posted network TDs, withholding rearm
until a take returns space. Notifications and TX continue while RX is paused.

Keep 16 KiB NTBs, two IN TDs, eight completed NTBs, 16 provider slots and TCP
settings unchanged. No new allocator, ABI, payload buffers or production logs.
The transport retains only frame spans/cursor/epoch: 280 additional requested
bytes per prepared transport in the matched instrumented build; copied-frame
and DMA backing are unchanged. This is a source/debug-type size observation,
not a resident-memory measurement. Native performance remains
pending; the later 1024-frame candidate must be measured separately.

## QEMU correctness

Production source `a96f5634` (subsequent changes are documentation only),
builder `pyxis-llvm23.1.3-3d69e83`, matching main `a29372f8` component bundles.
Four-CPU Q35/KVM, 2 GiB and fresh matching OVMF variables:

- Ordinary VirtIO control passes Remote echo/exit, four gateway pings and a
  matching 4 MiB TCP SHA256, with complete Remote drain.
- An actual Linux NCM gadget in a disposable Linux 6.19.10 peer VM enumerates
  at SuperSpeed. DHCP, Remote without shell echo, three pings and a matching
  1 MiB TCP SHA256 pass. No host gadget/configfs operations are used.
- Ordinary UDP bursts reach the unchanged provider bound. GDB observes a
  validated NTB held at frame 1 of 15 with epoch 1. After draining, all sixteen
  slots are free, the cursor/count/retained epoch are zero, NIC epoch remains
  1, both IN TDs are posted and xHCI is running/nonfailed; malformed stays zero.
- Real USB removal at a subsequent full handoff clears the retained suffix
  and wake state. The old transport loses publication authority, all slots
  drain and the NIC advances to epoch 4. A fresh branch retains the NIC.

Nested peer boot I/O required guest-local io_uring disabled and boot files in
RAM; these runs are correctness observations, not throughput measurements.
The same-address virtual reattach reached ready metadata but did not restore
ARP; that route is not claimed qualified. A fresh Linux gadget USB generation
with its udev service restores Remote, three pings and the matching 1 MiB hash;
the session exits with complete drain. Native RTL8156
performance and physical reconnection remain separate from this fixture.

Kernel SHA256: `a93e2d47bcae5a807c2f78806d8f4b10e19e19755eb1fe1e72c198bb8a9a20cb`.
Raw controls, GDB observations and launch inputs remain in
`/shared/present/ncm-throughput/handoff-qualification`.

## Native candidate packet

Private `probe/ncm-rx-candidate` at `1a020a4a` applies the handoff/credit changes
to frozen diagnostic A `4732028a`, retaining its compiler, component bundles,
archive and kernel configuration. It is never merged. In B, provider-full is
**deferred attempts**, not dropped/distinct frames; handoff-failed excludes
held frames and counts discarded suffixes. Empty/rearm intervals can include
intentional backpressure. The staged clean packet is
`/shared/present/ncm-throughput/handoff-packet`; no native candidate result yet.

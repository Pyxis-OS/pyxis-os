# CDC-NCM dock Ethernet

## Before-code baseline

Kernel source main `8395f988`, documentation head `e24c7472`; userland
`220b8076`, ports `efe8d877`, fs `d84f8084`, lwIP `a1aadb91`. Ordinary image
build passes with the existing `pyxis-llvm23.1.3-df87791` builder. No compiler
change. Performance configuration is `LOG_LEVEL=info`, QEMU 10.2.2/Q35/nested
KVM, CPU max, four CPUs, 2 GiB, UTC RTC, fresh OVMF variables, VirtIO network
and RNG. xHCI has a root USB 2 hub/keyboard and a Linux dummy_hcd/configfs NCM
gadget through usb-host. GDB inspects the unbound class function and a running,
nonfailed controller; no target calls or memory writes.

Remote client: `pyxis-remote --machine --no-shell-echo 127.0.0.1 24085`,
forwarded to guest 2323. Each IPC command is
`session bin://ipcbench.pxe MODE --size SIZE --messages 256 --rounds 100`.
All four warmups and 400 timed passes verify and exit 0 with complete drain.

| Nested time, ms | Median | Range |
| --- | ---: | ---: |
| CALL64 completion | 100.313 | 73.777–116.405 |
| CALL4096 completion | 100.430 | 74.582–106.510 |
| SEND64 completion | 14.059 | 12.389–22.549 |
| SEND64 admission | 1.210 | 1.134–1.625 |
| SEND4096 completion | 14.837 | 11.698–32.211 |
| SEND4096 admission | 1.251 | 1.135–2.674 |

A hands-free stream of 1,024 `echo -n` commands followed by `exit` reports
1,024 successful process completions plus successful exit; host-client wall
time is 15.03 s including connection and protocol overhead. This replaces the
old standalone `lua.pxe` loop: Lua is now a bundle and Remote has no job
profile. It is a wall proxy, not per-launch kernel time.

A prior trace-level capture also verified IPC, but idle logging substantially
widens its ranges; it is excluded from the performance comparison. Only the
info configuration above is the matched baseline. Raw captures, immutable
images and full commands remain in ignored `build/cdc-baseline{,-info}`.

Baseline ELF SHA256 `2ac7417e9e3ba23926a996f49e1d5f1af8b0285ced502e8eef596de866fe8123`;
ISO `859c63c2dda6846e6ba5d151f4571f290cb6a9dc0f80a6027e615fb71bd9167b`.
These are nested observations, not native performance or NCM driver evidence.
Native qualification remains pending; the newer runtime prevents a matched
after-code comparison with this baseline.


## Qualification checkpoint, 2026-10-15

Kernel code `5bc10f2e`, based on main `fecd1439` including #863's root settle;
published userland `45828adf` ([#267](https://git.internal/PyxisOS/pyxis-userland/pulls/267),
rebased onto `f239d47`), ports `0e760128`, fs `d84f8084`, lwIP `a1aadb91`.
Ordinary full source image and info kernel builds pass with the unchanged
`pyxis-llvm23.1.3-df87791` builder. No compiler/container rebuild. The source
review fixes configuration-inspection completeness independently of device
class, loss withdrawal before fence waits, initial retry/sibling ownership,
retained candidate capacity and captured-child versus hub completeness.

Ordinary non-NCM controls on `2b32dd74` (four CPUs) and `2f1270ed`
(one CPU) boot with complete discovery and running/nonfailed xHCI. Four CPUs
also pass hub-keyboard unplug/replug, hub removal and five verified CALL64
rounds; HID is idle with no pending hub changes afterward. These precede the
last two private discovery/withdrawal corrections, not the current NCM results.

Earlier HS gadget checks on `de624661`/userland `e9edd5b4` passed ping, plain
Remote, normal UDP log and a 5,848,400-byte xfer with matching SHA256. They
precede the recovery coordinator. Initial interoperability corrections were
control-OUT completion byte counts and byte-safe IN datagram handling; complete
NTB bounds, overlap, termination and mandatory NDP alignment remain checked.

The shared Codex VM's gadget cleanup deadlocked rtnl and required a reboot.
**Never create or tear down configfs/dummy_hcd/g_ncm gadgets on that shared VM.**
Current checks instead use a disposable Linux VM: Fedora 6.19.10, four vCPUs,
3 GiB, read-only host userland plus RAM overlay and a writable lab export only.
All gadget/module operations run inside that guest. Inner QEMU 10.2.2 uses
Q35/nested KVM, CPU max, 2 GiB, fresh OVMF variables, VirtIO network/RNG and
xHCI with a separate USB2 hub/keyboard. These extra-nested observations are
functional evidence, not timing or throughput comparisons. The protocol is
NTB16/no CRC, real descriptor MAC `02:77:00:00:00:02`, 16 KiB RX/TX, MTU 1500.

- **Four CPUs, HS root, static IPv4:** three pings pass; Remote echo exits 0
  with complete drain. GDB sees discovered/ready/bound/up, NIC ID 9, no malformed
  NTBs and initially free copied queues. Each of four removal/re-add cycles
  uses a fresh transport and retains NIC identity; removal closes admission
  and clears queues, and subsequent ping/Remote work. One cycle interrupts an
  active IPC workload; that interrupted series is not a completed performance
  sample. A later CALL64, eight messages, one warmup and one timed pass verify
  and exit 0. Carrier down/up clears queued work without spending a replacement.
- **Capacity refusal:** a fifth re-add leaves the NIC unprepared/not ready,
  branch stopped, all copied queues free, and exactly five consumed transports
  (initial plus four replacements). GDB still sees running/nonfailed xHCI,
  the unrelated keyboard active, its hub not retired, and the original fenced
  transport's NIC pointer detached.
- **One CPU, SS root, DHCP:** Linux enumerates the peer at 5,000 Mbit/s;
  dnsmasq records DISCOVER/OFFER/REQUEST/ACK for `192.168.77.2`. Plain Remote
  exits 0 with complete drain; normal UDP capture receives 3,961 bytes from
  the NCM MAC, beginning at packet 0. GDB sees bound/ready/up, no malformed NTBs
  and running/nonfailed xHCI. A 65,536-byte xfer publishes successfully; target
  SHA256 matches the source. Removal closes admission and frees copied queues;
  re-add spends one fresh transport, retains NIC identity and permits a fresh
  Remote connection. The old connection reports loss honestly.
- **Initial retry before configuration capture:** GDB stops before the first
  configuration read; the SS peer is removed/re-added through QEMU's device
  lifecycle, without target memory writes. Its device class is zero. The old
  record retires incomplete; two bounded initial attempts produce a ready/bound
  NIC, complete discovery, DHCP and Remote exit 0 with complete drain. This
  exercises real device loss before MAC/function capture, not a fabricated
  completion or new probe program. The nonzero-device-class uncertainty fix is
  source-reviewed separately.

Static four-CPU ELF SHA256
`fcc3245e8e6952136a245ceb885bd0abfc098128ac9266b9f678da4a1b38e992`;
ISO `873a99dd9d87b0c50fd32b595da96f3b78847920f516e9b9f64dac41decd7bad`.
One-CPU DHCP ISO
`8b4b16ee70b41e13f88f23e84ba968e9ee5eba40cf385a52b3b3a38c49de65ce`.
Raw captures and disposable-VM launch inputs stay local under
`build/disposable-ncm-lab`; earlier captures remain in `build/ncm-first` and
`build/ncm-checkpoint`.

All owned QEMU/GDB/client/log jobs are stopped; the disposable VM was quit
without gadget teardown on the shared host. The native candidate uses a private
exact-MAC DHCP profile read at runtime from the owner's Deck-dock PXE entry;
no hardware MAC is recorded here. The target is the Steam Deck dock on the
ThinkPad, not the absent proprietary ThinkPad dock. Firmware PXE through that
NIC may also select the Deck-tagged menu and must be checked when staging.

Limits: root-peer checks do not qualify ASIX hardware or a physical SuperSpeed
hub. Malformed NTB/candidate-capacity edge cases remain source-reviewed;
replacement-budget exhaustion is runtime-qualified above.
The original before-code baseline uses an older runtime; there is no matched
new-runtime before/after performance claim. Native ThinkPad/dock qualification
remains required. Both PRs remain draft and task 9 stays unchecked.

## Main refresh before native run, 2026-10-15

Kernel code `44ec33c3` is rebased onto main `b9b6bed7` (log levels, hardware
audit, NVMe and task-5 prerequisites). Userland #267 is `308f8d0d`, rebased onto
main `9437cb69` with an unchanged discovery patch; fs follows main `101e1f68`.
Other dependency pins and the compiler image above are unchanged. NCM patches
replay unchanged; the two new setup/recovery state lines use debug, and the
setup line omits the MAC. Existing per-operation trace remains trace.

An ordinary full source image passes. Final kernel and profile-only variants
reuse the verified unchanged SDK/userland/ports bundles. Build ceiling is debug,
console threshold info, normal UDP enabled. No trace chatter, compiler rebuild,
new test infrastructure or shared-host gadget operations.

- **Final code, one CPU SS root:** disposable Linux peer configuration above,
  DHCP, three pings, beacon Remote exit 0/complete drain and normal UDP pass.
  GDB sees ready/bound/up, sealed discovery and no malformed NTBs. Real QEMU
  removal closes admission, drains copied queues and detaches the old transport.
  Re-add on the same root port creates a fresh transport, retains the logical
  NIC, consumes one replacement and restores ping/Remote. A recovered 65,536-byte
  xfer publishes with matching independently checked target/source SHA256.
  xHCI stays running/nonfailed; interactive exit completes cleanly.
- **Final code, four CPUs ordinary control:** Q35/KVM, 2 GiB, modern VirtIO
  network/RNG and a separate USB hub/keyboard. Five CALL64 rounds of 256 messages
  verify, exit 0 and drain completely. Keyboard removal/re-add and hub removal
  leave xHCI running/nonfailed; a subsequent Remote echo completes. GDB sees idle
  CPUs, idle HID processing, no pending hub bits or retired objects/heap bytes.

These are correctness checks, not new timing samples. The earlier HS,
replacement-exhaustion and initial-loss runs remain separately dated evidence.
Raw refreshed captures stay in ignored `build/ncm-main-refresh/` and
`build/disposable-ncm-lab/refresh-20261015/`; all owned jobs are stopped.

Private packet `/shared/present/cdc-ncm-native/` is refreshed from the runtime
Deck-tagged dnsmasq entry, with required exact-MAC DHCP and matching host tools.
The staged archive's selector is checked without publishing it. Kernel SHA256
`16353e4c9b93c1d40c80b4024b7734c512198e2895a17110285aa718de1cf9da`;
archive `4205f5e24145ae4f43c526f6a10c233c3c3e85e0cae64e480b000844f32545ec`.
Native ASIX/physical-hub/cable checks remain the owner's next action; neither
task completion nor native success is claimed.

## ThinkPad first refreshed native attempt: preparation refused

2026-10-15, owner run of source `44ec33c3`, documentation head `da1f3877`:
PXE used the built-in NIC; the Steam Deck dock was connected over USB-C with
Ethernet attached. Both dock hubs enumerated, including the AX88179A
`0b95:1790` at `7:0.3` port `5.4`, SuperSpeed. Configuration 2 describes the
selected NCM function (configuration 1 is vendor-specific, 3 is ECM). The
required exact-dock net0 was absent. The only relevant retained info/debug
record was `NCM class preparation refused`; it did not name the failing gate.
This is a native failure, not successful dock qualification.

The follow-up adds permanent setup diagnostics, without changing class
requests, negotiation policy, deadlines or recovery. One specific info refusal
names the route/capacity or class step and result; class failures include the
attempted configuration, whether SET_CONFIGURATION completed, last request
fields, completion phase/status/length and deadline expiry. Debug records
include the function/endpoint fields, remaining setup budget, string descriptor
metadata, negotiated scalar values and raw numeric NTB parameters. No MAC
characters or address bytes are logged. `configured=1` means the device accepted
SET_CONFIGURATION, not an independent GET_CONFIGURATION observation.

Source-inspected candidates, not native attribution:

- A parent hub without a usable notification stream rejects the route before
  `usb_ncm_setup` runs. A root-attached QEMU peer does not exercise that gate.
- The parameter parser currently requires payload divisors to be powers of two
  and at least four, although NCM Table 6-3 applies that restriction to NDP
  alignment. Actual native parameters have not yet been observed.
- SET_CONFIGURATION explicitly attempts the selected bConfigurationValue 2;
  starting in configuration 1 alone does not explain refusal. Later control
  requests, string validation or the shared enumeration deadline can still fail.

Diagnostics-only validation: ordinary debug-ceiling kernel build; Q35/KVM,
four CPUs, 2 GiB, fresh OVMF, VirtIO network, xHCI hub/keyboard; remote echo
returned status 0 and complete drain. GDB found all four CPUs and the xHCI
controller running/nonfailed. This smoke check does not repeat the NCM peer
qualification or establish an AX88179A cause. Raw records are under
`build/ncm-native-refusal/`; owned QEMU/GDB jobs were stopped.

Native rerun: use the rebuilt packet in `/shared/present/cdc-ncm-native/` and
read `log -l debug | grep -i -e usb-ncm -e 'boot hub notification'` locally.
Result numbers: OK=0, BUSY=1, INVALID=2, IO=3, TIMEOUT=4, UNSUPPORTED=5,
STALE=6, STALL=7, DISCONTINUITY=8. Preserve the first refusal plus preceding
NTB/string/request metadata. Native confirmation remains pending.

## Native refusal identified: advisory datagram query

2026-10-15 ~14:50, owner ThinkPad run of diagnostic kernel SHA256
`1ee931fc9f2746fe828e998318809319a24234c81ece8cd85f7009ee2966693f`:
configuration 2 completed; control interface 0, data interface 1/alt 1,
capabilities `0x2b`, MAC string index 5 returned 26 bytes and validated.
NTB parameters: formats `0x0001`, RX maximum 32768, TX maximum 65536,
RX/TX divisors 8, remainders 6, alignments 8, TX datagrams 56. The
GET_MAX_DATAGRAM_SIZE request (`0x87`, type `0xa1`, interface 0, length 2)
completed successfully with two bytes, but its reported value was **28**;
the Ethernet functional descriptor advertises 1514. The old code rejected
that value with USB_UNSUPPORTED. No deadline expiry or parent-hub route
failure caused this attempt's refusal. Why firmware reports 28 is unknown.

The fix keeps the validated descriptor-backed 1514-byte frame profile and
logs unusable query values at debug. Query failure or an unusable reply does
not refuse preparation. Capability bit D3 still gates both requests; a reply
already equal to 1514 avoids a redundant SET, otherwise setup attempts
SET_MAX_DATAGRAM_SIZE(1514). SET refusal is also advisory, with its actual
result logged; subsequent mandatory requests, endpoint setup, transport
health and whole-frame bounds remain enforced. There is no success fabricated
for a failed control request.

Reference inspected at Linux v6.18 commit
[`7d0a66e4bb9081d75c82ec4957c50034cb0ea449`](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/usb/cdc_ncm.c#L609-L654):
`cdc_ncm_set_dgram_size` starts from the descriptor-based size, gates GET/SET
on the capability bit, skips SET for an identical value, and treats failed
GET or SET as debug-only. Pyxis also attempts the supported SET after a failed
GET, as directed by the owner. No Linux code was imported. Saved reference
file SHA256 `9acca9746e8130d4c7a455d0dc42625dbed6fb20586bed1a5f029737f799786d`.

Ordinary debug-ceiling kernel build and four-CPU Q35/KVM/2 GiB/fresh-OVMF
VirtIO-network/xHCI-hub-keyboard smoke pass: Remote echo exits 0 with complete
drain; GDB observes four CPUs and xHCI running/nonfailed. Raw records are in
`build/ncm-datagram-advisory/`; owned jobs are stopped. The actual native
fallback/SET/data path still awaits the owner's rerun; this smoke is not NCM
peer qualification. Rebuilt packet retains the exact paired archive and private
runtime dock selector. No compiler rebuild or new owner decision is needed.

## Native prepared NIC, DHCP pending: binding and carrier evidence

Owner rerun of kernel `ab9b0f11…` (~15:00–15:05) passed preparation and
created net0, but reported DHCP acquisition incomplete. Captured setup ran
twice; SET_MAX_DATAGRAM_SIZE(1514) succeeded, followed by NTB16 RX/TX 16384,
then fresh branch generation 1. The old image had no bind, bulk-start or carrier
transition records. This establishes recovery, not successful data transfer or
a missing binding. The dock has no router reservation; the LAN router supplies
a new pool lease, while the PXE host is only a proxy.

Source inspection confirms that recovery retains `branch->nic`, verifies its
MAC and passes it into replacement setup; withdrawal preserves the logical
NIC's `bound` flag. Fresh bulk activation observes that retained binding. No
rebind to a new logical NIC or fallback to another adapter is required.
New info records identify net0's NIC binding, active transport, carrier changes
and the recovery/admission sequence without printing MACs.

A separate readiness gap is fixed in userland #267 `2264bc8d`: DHCP waits for
both READY and LINK_UP before opening acquisition; static configuration keeps
its previous readiness policy. A prepared/recovering binding remains pending.
The ten-second foreground selection/acquisition budget and repeated thirty-second
background/address-wait windows are unchanged. A timeout retains the endpoint,
transaction ID, request state and randomized backoff; temporary unavailable
sends retry. Initial DISCOVER retransmission is after three to five seconds;
a first OFFER delayed by a second or more remains acceptable. No reservation
assumption or special lease policy was found. These changes improve sequencing;
they do not attribute the native DHCP failure without the next capture.

An unmerged diagnostic overlay records first-minute cumulative RX/TX/drop and
notification counts at debug, plus at most 32 DHCP wire events (DISCOVER,
OFFER, REQUEST, ACK/NAK with offered address/server/lease metadata, never MAC).
TX submission/completion counters distinguish USB acceptance; received wire
messages do not prove the userspace DHCP decoder accepted their options.
The read-only LOG interface cannot accept userspace debug messages, so no
writable log ABI or kernel DHCP policy is added for diagnostics. The overlay
is recorded/staged separately and must not merge. Native carrier/data/DHCP
qualification is still pending.

# RTL8111 qualification

The built-in ThinkPad RTL8168h/8111h, XID `541`, is qualified for the existing
remote terminal through VFIO and on the owner-run native cold/PXE boot with the
dock attached. [Driver behavior](../devices/rtl8111.md) and
[hardware ownership](../devices/rtl8111-hardware.md) are separate references.
Full and partial hardware MAC bytes are omitted. Historical planning remains in
Git; these are measured and owner-reported results, with their limits.

## Initial I/O regression

Baseline `b8a5255` and implementation `630f86a`, both pinning userspace `08e3c4b`,
were built with GCC 16.2 and `make -j16 image`, using
`CROSS_COMPILE=$HOME/opt/pyxis-cross/bin/x86_64-unknown-pyxis-` and
`PYTHON=build/hpet-config-venv/bin/python3`. Both include the merged installer and
selector dependency changes. Subsequent validation-note edits do not change code.
Builds passed; existing vendor port warnings remain. No compiler rebuild,
dependency pin change, test harness or fault injection was added.

Runs used QEMU 10.2.2/KVM on the owner's ThinkPad host (`systemd-detect-virt`: none),
4 CPUs, 2 GiB RAM, the raw edk2 OVMF pair, default init, and physical passthrough
`VFIO_PCI=0000:05:00.0`. These are host KVM results, not nested-VM measurements or
native Pyxis qualification; native results are recorded below. GDB inspected
state without profiling workloads.

With `NETWORK_CONFIG=/private/path/network.lua` and both NICs present, RTL bound
by its private boot-loaded MAC and VirtIO stayed unstarted with DMA unpublished.
The existing host remote client connected to `192.168.0.50:2323` and ran three
`ping -c 5 192.168.0.1` batches: 15/15 replies, batch means 1.383, 0.622 and 0.749 ms
(range 0.335–2.504 ms). `ping -c 3 127.0.0.1` returned 3/3. GDB found 157 interrupts,
112 received frames, 50 submitted/completed TX frames, no outstanding TX and no
malformed RX. Both rings wrapped; the device mask was `0x002f`.

A warm boot initially failed FIFO drain after an active guest exited. The new
firmware handed off Command `0x0007`, ChipCmd `0x0c` and MCU `0x22`. Recovery now
requires one confirmed software reset followed by fresh drain/OOB/stop checks;
it does not assume timeout means stopped. The final RTL boot and following
VirtIO boot both exercised recovery successfully.

Matched VirtIO runs used `VIRTIO_NET=1 TCP_FORWARD=2323:2323` with the packaged
profile, keeping RTL inactive (ChipCmd/mask zero, no driver DMA or interrupts).
The existing remote client ran three `ping -c 5 10.0.2.2` batches and three
`ttcp -t -p 5001 -n 128 -l 8192 10.0.2.2` sends. Host `socat` accepted TCP port
5001 and `wc -c` confirmed 1,048,576 bytes per run. UDP used the existing host
port-18080 echo with `udp-send 10.0.2.15 10.0.2.2 18080 "RTL I/O baseline"`.

| Measurement | Baseline | After |
| --- | --- | --- |
| Ping batch means, ms | 0.443 / 0.833 / 0.869 | 0.996 / 0.941 / 0.466 |
| Ping aggregate mean / median, ms | 0.715 / 0.431 | 0.801 / 0.555 |
| Ping range, ms; replies | 0.208–2.900; 15/15 | 0.203–2.911; 15/15 |
| TCP seconds, including closure | 0.470191 / 0.469106 / 0.467918 | 0.487508 / 0.472137 / 0.474428 |
| TCP mean seconds | 0.469072 | 0.478024 |
| UDP echo | exact 16-byte reply | exact 16-byte reply |

TCP mean time rose 1.9%, with more variation after the change; the small sample
cannot establish cause. Ping likewise varies within both runs. This is functional
and bounded timing evidence, not proof of unchanged throughput or sustained
reliability. Raw local captures are `build/rtl-io-baseline-*`,
`build/rtl-io-final-physical.jsonl` and `build/rtl-io-final-virtio-*`.

A separate four-CPU no-NIC boot reached userspace with both controller lists
empty, binding NONE, worker ready and external IPv4 unassigned.

## Sustained traffic and link checks

Baseline main `eaeb417` includes merged RTL I/O PR #367.
Driver changes are `36c45f2`; qualification used its source files from the
working tree before the commit (the kernel reports base `eaeb417`). The saved
qualified ELF has SHA-256
`7d7ef29474273998338f47fdd5a9fea481c0a1796dfe26bbf43a6738bec5878b`.
Dependency pins are unchanged, including userspace
`08e3c4b4a1385a6da978d64eced2c4aece528237`. The owner accepted suspending pending
TX deadlines while carrier is down, with a fresh five-second budget on return.
The worker preserves OWN and the binding throughout; carrier-up stalls still
stop until reboot. Internal tally capture adds one retained DMA page per
controller, without periodic polling or a userspace ABI.

The owner confirmed that the dock was connected throughout these checks.
Matched runs on the bare ThinkPad host used QEMU 10.2.2/KVM, 4 CPUs, 2 GiB,
`VFIO_PCI=0000:05:00.0`, `VIRTIO_NET=0`, Fedora's OVMF pair, the private built-in
profile and the stock init. Configuration kept HPET maintenance at 120 ticks,
NPFS flush at 30 seconds and xHCI disabled. The peer was the same host on Wi-Fi
at `192.168.0.51`; these results do not measure the port's 1 Gbps ceiling.

```sh
make -j16 image PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=$HOME/opt/pyxis-cross/bin/x86_64-unknown-pyxis- \
  NETWORK_CONFIG=/tmp/pyxis-thinkpad-network.lua
```

Host `socat -u TCP4-LISTEN:5001,bind=192.168.0.51,reuseaddr OPEN:<capture>,creat,trunc`
received guest `ttcp -t -n 8192 -l 8192 192.168.0.51`, three times per revision.
Independent host byte counts were 67,108,864 for all six transfers, and their
SHA-256 values matched. Timing includes TCP closure:

| Revision | Seconds, three runs | Mean seconds | Range seconds |
| --- | --- | --- | --- |
| `eaeb417` baseline | 25.107563, 24.645992, 24.734602 | 24.829386 | 0.461571 |
| `36c45f2` | 24.736208, 25.213221, 25.092571 | 25.014000 | 0.477013 |

The mean rose 0.74%; the difference is smaller than within-revision variation.
No profiler was attached during the timed workloads.
Both revisions passed gateway ping and the existing UDP echo tool. The updated
revision also passed loopback ping; all commands ran through the real-port
remote terminal at `192.168.0.50:2323`.

On the baseline the owner unplugged during an 8 GiB `ttcp` request and replugged
after about ten seconds. Link down/up was observed, the NIC stayed active and
new gateway ping succeeded. At subsequent debugger inspection 191,830 submitted
TX descriptors had all completed, with none outstanding and no malformed RX.
This run did not observe device-owned TX during carrier loss and did not
reproduce a permanent stop. The long transfer was cancelled after inspection.

Hardware tally snapshots on `36c45f2` before/after the three TX runs showed
`rx_missed=91` unchanged, with zero TX/RX/alignment errors. This count was
inherited from earlier VFIO runs; qualification does not reset it. For receive,
host `socat -u OPEN:<64-MiB-capture> TCP4-LISTEN:5002,bind=192.168.0.51,reuseaddr`
served the same payload to guest `tcp 192.168.0.51 5002 | sha256sum`.
All three 64 MiB receive runs returned the host payload's SHA-256. After them,
`rx_missed` was still 91 and hardware TX/RX/alignment errors were still zero:

| Snapshot | TX packets | RX packets | RX missed |
| --- | --- | --- | --- |
| Before TX workloads | 442,937 | 95,476 | 91 |
| After TX / before RX | 819,770 | 175,301 | 91 |
| After RX | 1,055,272 | 557,313 | 91 |

The worker had received 461,868 frames with zero malformed completions and no
outstanding TX at the final snapshot. These bounded runs give no evidence for
increasing the 32-entry rings. They do not establish line-rate capacity or rule
out a full wrap of the 16-bit counter.

The owner repeated the ten-second unplug on `36c45f2` during another 8 GiB
`ttcp` request. A breakpoint in the carrier-down branch observed zero TX
outstanding, descriptor OWN clear and the controller active, then immediately
resumed service. After replug the controller remained active with no stop reason;
1,184,275 submitted descriptors had completed and RX malformed remained zero.
A new remote session passed three gateway pings and UDP echo. This checks link
recovery but leaves the device-owned TX case unobserved. The long transfer was
cancelled after inspection.

GDB snapshots stop the BSP in scheduler code with kernel mappings and IF=0,
outside locks. On this GDB/QEMU combination an ordinary injected call's dummy
return executed a non-executable stack address and halted the guest; that boot
was discarded. Qualification instead uses an executable return breakpoint,
preserving/restoring the caller registers and stack. Counter stops are between
workloads, not during timed transfers. The final untimed link check used a
one-shot breakpoint to read ownership at the carrier-down branch. Raw captures
remain under ignored `build/rtl-task5-*`.

A separate stock-profile boot kept both VirtIO and the real RTL present
(`VIRTIO_NET=1`, `TCP_FORWARD=2323:2323`, same CPU/RAM/KVM configuration).
VirtIO activated while the RTL remained prepared with DMA/delivery disabled.
Remote terminal, three `10.0.2.2` pings and UDP echo passed through VirtIO.
Both image builds passed; no kernel warning was emitted. No new tests or CI jobs
were added. QEMU, GDB and host echo/transfer jobs are stopped.

## Native cold/PXE boot

Native completion is owner-reported in [PR #368](https://git.internal/PyxisOS/pyxis-os/pulls/368)
on 2026-10-04, from the saved private-profile pair on a cold/PXE boot with the
dock attached. XID `541` prepared and activated, XID `502` was diagnosed as
unsupported without variant-specific writes, and link rose after activation.
The owner recorded:

```text
rtl8111 5:0.0 XID=541: firmware-free PHY prepared; RX/TX, DMA and delivery disabled
rtl8111 2:0.0 XID=502: unsupported XID; no variant-specific writes
rtl8111: RX/TX active, 32 buffers per ring, link down; BSP worker owns completions
rtl8111: link up
```

No FIFO recovery-reset message appeared. This demonstrates the observed UEFI
handoff; it does not establish every firmware or power state.

The wired desktop at `192.168.0.213` received 7/7 ping replies from
`192.168.0.50`: first reply 3.4 ms, subsequent replies 0.58–0.68 ms. Through the
remote terminal at `192.168.0.50:2323`, the owner ran
`ttcp -t -p 5001 -n 8192 -l 8192 192.168.0.213` against the desktop's `socat` sink.
The guest reported 67,108,864 bytes in 2.192838 seconds, 29.186 MiB/s including
closure. Desktop `wc -c /tmp/rx.bin` confirmed 67,108,864 bytes; content was not
hashed. The firewall exception was temporary.

The same native boot fetched `https://example.com` through `cat` and piped
`cat https://duckduckgo.com | sha256sum`. These owner observations exercise name
lookup, outbound TCP/TLS, native HTTPS reads and the shell pipeline. The owner
independently fetched the DuckDuckGo response with desktop `curl | sha256sum`
and confirmed the same SHA-256 as the native fetch. This verifies content
integrity for that observed response; the page digest is not a stable fixture.
The separate 64 MiB `ttcp` payload has byte-count verification only.
Native and VFIO timings use different peers/environments; their
roughly elevenfold difference does not isolate Wi-Fi, VFIO/QEMU or stack costs.
Native CPU count and repeated throughput samples were not reported, so this
single native result is functional evidence rather than a matched performance
comparison.

The remote-terminal completion goal is met in VFIO and natively with the dock
attached. Native unplug and device-owned TX at carrier loss remain unobserved;
only XID `541` is supported, addressing is static, and line-rate throughput is
unqualified. No further owner qualification is required for this milestone.

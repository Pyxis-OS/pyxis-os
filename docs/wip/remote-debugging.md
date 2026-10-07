# Remote debugging without serial

Status: **milestone, agreed 2026-10-07.** A second Codex instance implements it,
alongside the [ACPI](acpi-and-bar-widgets.md) and
[display drivers](display-drivers.md) milestones. Each task starts when the owner
says so.

## Goal

Develop and test directly on the ThinkPad over PXE, without its screen and
without a serial port. This is the path for native driver work such as the
[display drivers](display-drivers.md#after-the-milestone), where a broken screen
must not mean a blind machine.

- **The kernel log stays readable.** A program can read it, including over the
  remote terminal.
- **Pyxis connects to the host.** The reverse remote terminal means nobody has
  to know or type the ThinkPad's address.
- **The kernel sends its log over UDP.** A panic message reaches the host even
  when userspace and the screen are gone.

## Today

- **The kernel log is retained.** The [read-only log interface](../interfaces/kernel-log.md)
  keeps the most recent lines in a static 256 KiB ring; `log` and `log -f`
  can read it in local and remote shells. Capture is independent of the screen.
- **The ThinkPad has no usable serial port.** DASH serial-over-LAN through the
  board's second Realtek controller is
  [parked](thinkpad-next-steps.md#4-parked-dash-serial-over-lan-for-boot-logs).
  Telnet text redirection to its UART works, but the owner decided on
  2026-10-07 not to rely on it because the setup is fragile.
- **The remote terminal supports reverse discovery.** An opt-in kernel option
  selects a host beacon by name; the configured Remote space connects out.
  Without the option, its existing port-2323 listener remains available.
  See [reverse connections](../userland/remote-terminal.md#reverse-connections).
- **The network works natively.** The built-in RTL8111 works over PXE with DHCP.
  The privileged wildcard UDP endpoint for broadcast reception exists for DHCP
  ([networking](../devices/networking.md)).

## Decisions

From 2026-10-03, for the reverse terminal (previously in the ThinkPad next steps):

- **No authentication for now.** Reverse mode is unauthenticated and unencrypted
  like the existing remote terminal, until Pyxis has authentication. A host that
  answers gets the configured shell's authority, so this assumes a trusted LAN.
  It is recorded in technical debt with that revisit point when implemented.
- **Opt-in only.** Reverse mode starts only when the configuration asks for it,
  never by default.
- **A non-secret name in the beacon.** Pyxis is configured with a name and
  connects only to a beacon carrying it, so that several development machines
  can share a network. It is not a security measure.

Accepted by the owner on 2026-10-07:

1. **The kernel keeps its log in a fixed ring of 256 KiB, from the first line.**
   - It is static storage, because early boot cannot allocate.
   - It holds every `klog` line, and trace lines when tracing is enabled.
   - When it is full, the oldest lines are dropped and counted.
2. **A read-only `log` grant for every space, remote shells included.**
   - A native `log` command prints the ring, and `log -f` keeps printing new lines.
   - A remote `log -f` therefore shows everything up to a hang.
   - Kernel addresses become readable from the LAN. This is accepted under the
     trusted-LAN model and recorded in technical debt.
3. **A kernel command-line option names the beacon to follow.**
   - A host's PXE entry turns on reverse mode without rebuilding the image.
   - Boot init passes the option on, and the Remote space connects out instead of
     listening.
   - The kernel today stops on unknown options, so the option is added to its
     list ([boot command line](../userland/init.md#boot-command-line)).

Also from the owner on 2026-10-07: the kernel sending its log over UDP is wanted.
It is task 3.

Accepted reader contract for task 1 on 2026-10-07:

- Readers own independent sequence/byte-offset cursors. Reads return text, a
  next cursor and the number of overwritten lines missed by that reader.
- `log` captures an end and exits after that snapshot; `log -f` prints history,
  then polls through the existing clock service every 100 ms.
- Fatal output retains its independent emergency path. Ring capture is best
  effort when a panic interrupts a lock owner; guaranteed fatal transmission
  belongs to task 3.

## Tasks

- [x] **1. The kernel log ring and the `log` command.**
  - The ring (decision 1), the read-only grant and its forwarding through boot
    init, session and the shell (decision 2), and the native `log` command with
    `-f`.
  - The accepted reader contract above is implemented by the
    [native log ABI](../../include/abi/log.h).
  - **Validation:** complete kernel/SDK/image builds, interactive four-CPU KVM
    QEMU with 2 GiB RAM and VirtIO networking, at info and trace log levels.
    Local and remote `log` returned retained boot history; `log -f` displayed
    post-start scheduler/process-exit lines. A second remote snapshot completed
    while the first followed. Remote Ctrl+C returned terminated completion.
    Read-only GDB inspection confirmed boot retention and natural trace-driven
    overflow (first line 1562, next line 7386, 262101 occupied bytes).
  - **Memory:** 262144-byte ring including four-byte line headers; 262208 bytes
    of BSS including bookkeeping/alignment. It replaces the old 32768-byte
    early-log store. Bounded kernel reply storage is 1056 bytes per read.
  - **Limits:** oversized-line and clipped partial-snapshot cases were reviewed
    in code, without fault injection or new tests. No native ThinkPad run or
    isolated boot-time/presentation-cost measurement is claimed. An attempted
    GDB allocator call faulted at its NX-stack return trampoline; runtime
    validation used a fresh boot and read-only debugger inspection afterward.
  - **Review follow-up:** retain boot-init serial-mirrored console output,
    reduce `LOG_READ_MAX` to 1024 to bound stack use, and record the cost of
    locating cursors under the ring lock in technical debt. Baseline at main
    `30e127b` and the follow-up source used the same nested-KVM configuration
    (4 CPUs, 2 GiB, VirtIO networking, info logging) and compiler flags. Three
    `log` sessions completed in 0.02 s each before and after, measured with
    `/usr/bin/time -p` around `pyxis-remote --machine --no-shell-echo`; each
    session received `log` on stdin and then EOF. This measures connect, read
    and teardown together at 0.01 s reporting resolution, not isolated ring
    latency. Output grew from 4131 to 4393 bytes because it now includes five
    boot-init messages, including the unavailable HOST volume. All six
    snapshots exited successfully. Compiler stack reservations fell from
    4184 to 1112 bytes in `log_call` and 4208 to 1136 bytes in TTY replay
    (disassembled `sub rsp`, excluding pushes and callees).
  - **Delivery:** userland [PR #140](https://git.internal/PyxisOS/pyxis-userland/pulls/140)
    at `bb66de52cd7fb11bf1d701548d61c8ef9e95529d` and Pyxis
    [PR #465](https://git.internal/PyxisOS/pyxis-os/pulls/465) are merged.
    `fj pr status` cannot parse userland's
    empty combined state (`unknown variant`); dependency CI is unavailable.

- [x] **2. The reverse remote terminal.**
  - A host server broadcasts a small UDP beacon about once a second. The
    beacon is sent to `255.255.255.255` or the subnet broadcast address. It
    carries:
    - a protocol tag and version;
    - the server's TCP port;
    - the name.
  - Pyxis receives on an agreed UDP port, takes the sender's address from a
    beacon with the configured name, and connects over TCP. The session then
    works like today's remote terminal, with the same framing, file transfer and
    machine mode.
  - **Accepted on 2026-10-07:** `remote.beacon=NAME`, case-sensitive names up
    to 63 bytes, UDP 2324 with a tag/version, TCP port and name. Only the
    reverse-mode trusted Remote daemon holds broadcast-opening authority;
    remote shells keep UDP OPEN alone. The discovery endpoint closes before
    connection and reopens after old-group cleanup. The host accepts one
    session and exits at its end; restarting it advertises a fresh session.
  - **Implemented:** the [shared beacon format](../../include/remote/beacon.h),
    boot-option handoff, separate `udp_beacons` grant, host `--listen` mode and
    single-session discovery/reconnection. No authentication is added.
  - **Qualification:**
    - [x] In QEMU, the option connects Pyxis to `pyxis-remote --listen` without
      entering its address; mismatched beacon names are ignored, `log` and
      `log -f` work, and an abrupt host disconnect during a Lua loop permits a
      fresh session after cleanup. The guest has no TCP listener in reverse
      mode (read-only GDB `listener_count` is zero).
    - [x] A literal `t14${ARCH}` name survives Limine configuration expansion
      and matches the advertised name. Generated entries use a one-pass macro.
    - [x] Interactive reverse file upload completed with SHA-256 matching the
      host source; Ctrl+] closed with acknowledged group termination.
    - [x] Without the option, the ordinary listener remains active (GDB
      `listener_count` is one), and remote `log` and `ls` exit successfully.
    - [x] On the ThinkPad over PXE (main after the #467–#470 merges,
      2026-10-07), the owner's listener received the connection from the
      beacon, and `log -f` showed the boot from its first line, including boot
      init's lines, until Ctrl+C.
  - **Measured:** baseline direct `log` connect/read/teardown at `7a5682f`
    reported 0.02 s in each of three samples. Reverse chained sessions reported
    1.02, 0.03 and 0.09 s; the first followed an abrupt disconnect with a Lua
    loop running, including cleanup and a fresh one-second beacon tick. These
    are nested-KVM end-to-end samples, not isolated kernel timings. Both use
    4 CPUs, 2 GiB, VirtIO networking, info logging and the same host tool's
    machine/quiet mode, with `/usr/bin/time -p` at 0.01 s resolution. Reverse
    output was 4519 bytes versus the 4393-byte baseline after added termination
    messages; TCP transport and log reads are unchanged. Ordinary-mode samples
    after the change all remained 0.02 s and 4393 bytes, matching the baseline.
  - **Remaining:** macOS listener mode was not checked.
  - **Delivery:** branch `debug/reverse-terminal` in
    `/home/chronium/src/pyxis-remote-debugging`; userland
    [PR #141](https://git.internal/PyxisOS/pyxis-userland/pulls/141) at
    `d81475f799afb0162d7584587d83973bf48691ba` and parent
    [PR #469](https://git.internal/PyxisOS/pyxis-os/pulls/469) are merged.
    [PR #471](https://git.internal/PyxisOS/pyxis-os/pulls/471) records the
    owner's native qualification. All task-owned QEMU, debugger and
    host-client jobs are stopped.
- [ ] **3. The kernel log over UDP.**
  - The kernel sends each log line as UDP datagrams to the host, and keeps
    sending during a panic, when locks may be held and userspace is gone.
  - [x] Implement selected VirtIO/RTL8111 fatal TX ownership, asynchronous ring
    following and the host receiver, with `log.udp=1` opt-in.
  - [x] In QEMU, receive the boot log and deliberate panic messages, including
    an interrupted log-lock owner and interrupted TX publication.
  - [x] With the option off, observe no UDP log traffic, including during panic.
  - [ ] On the ThinkPad, receive the boot log and a deliberate panic's message;
    repeat the disabled check. This remains owner qualification, not an inferred
    result from QEMU or the earlier native reverse-terminal run.
  - Usage, wire format, ownership and limits are in
    [kernel log](../interfaces/kernel-log.md#udp-capture).

### Task 3 accepted contract, 2026-10-07

Branch `debug/kernel-udp-log` in `/home/chronium/src/pyxis-remote-debugging`
starts from main `800f979`, after the owner's native reverse-terminal check in
PR #471. PR #469's non-blocking host-build/firewall/interface notes are carried
into the remote-terminal guide. The owner accepted these choices on 2026-10-07:

1. Opt in with `log.udp=1`, sending IPv4 limited broadcast to UDP port 2325.
   A separate host log receiver prints text and packet/fragment sequence gaps.
   This exposes enabled log traffic across the trusted LAN and avoids ARP,
   routing and reverse-terminal beacon dependence.
2. Begin after the already selected net0 driver activates, without changing
   NIC selection or requiring DHCP. Use source zero before IPv4 assignment;
   replay retained ring history once ready, then follow new text asynchronously
   on the BSP network worker. Panics before driver activation cannot use UDP.
3. Reserve fatal TX capacity only when enabled: ordinary VirtIO capacity is
   15 of 16 descriptors and RTL8111 capacity is 30 of 32. Panic irreversibly
   hands the selected TX path to the first panicking CPU, with bounded handoff
   and completion polling, preallocated storage and no heap, lwIP or held-lock
   acquisition. Normal networking ends until reboot. Never reuse device-owned
   storage after a timeout; unavailable hardware or a failed CPU handoff stops
   the UDP attempt safely. Delivery remains best effort, and the host must
   tolerate duplicate fatal packets.

Code inspection found that panic currently halts only the faulting CPU, so an
AP panic can race the BSP driver. Both drivers can be interrupted between their
software producer update and DMA publication. The driver gates cover that
window explicitly, exclude reset and normal DMA mutations during takeover,
and preserve device ownership. A separate RTL priority ring has not been
qualified; the implementation uses existing rings rather than assuming it works.

### Task 3 implementation and qualification, 2026-10-07

The implementation is on `debug/kernel-udp-log`, based on main `800f979`.
The final implementation revision is `8a4b944`; no dependency pins changed.
Ordinary image builds and `make -C tools log remote -j16` passed. Kernel and
host changes compile without warnings; existing third-party port warnings are
not counted as a warning-free full build. No compiler-container rebuild is
needed. The UDP logger object reserves 2369 bytes of static BSS, including
separate normal/fatal staging; driver metadata grows but DMA allocations do not.

Manual QEMU qualification used Q35, four CPUs, 2 GiB, nested KVM, QEMU 10.2.2,
VirtIO-net and the matching `/usr/share/OVMF/OVMF_{CODE,VARS}.fd` firmware pair.
An isolated TAP interface used host `10.77.0.1/24`; the image's local network
profile selected VirtIO with static `10.77.0.2/24`. The host receiver and packet
capture were started before boot. Capture showed the first log packet with
source zero, then packets with the assigned address. The receiver printed the
retained boot log from its first byte, including boot-init messages.

Fatal-path checks used the `c22bffa` implementation before the subsequent
normal-polling/disabled-interrupt refinements. GDB redirected a selected CPU to
the nonreturning `panic()` entry with a manual qualification message:

- BSP and AP messages reached the receiver. The AP check recorded fatal owner
  APIC 2, a permanently closed gate and no fatal TX failure.
- A breakpoint inside `log_ring_putc` interrupted the BSP with both ring and
  presentation locks held. Its panic message arrived while debugger inspection
  confirmed both locks remained held; fatal capture did not require retention.
- A breakpoint between the VirtIO software available-index store and DMA
  available-index store recorded indices 1 and 0. Panic sent its message using
  the reserved descriptor; checked used/available indices reached 2. The host
  reported the intentionally lost normal staging sequence 0 as a packet gap.

The disabled capture used final implementation `8a4b944`, an active NIC and a
successful ordinary remote log read. It captured 26 TCP packets and zero UDP
2325 packets across boot, that session and a deliberate BSP panic. The receiver
printed no text; GDB recorded `enabled=false`, sequence zero and no reserved
VirtIO descriptor. RTL8111 runtime, its ambiguous-slot duplication, stalled-NIC
abandonment and native ThinkPad panic output remain code-inspected rather than
hardware-qualified. Delivery remains best effort.

Matched measurements used the default DHCP profile and QEMU user-mode network,
with `CPUS=4 MEMORY=2G ACCEL=kvm QEMU_DISPLAY=none VIRTIO_NET=1
TCP_FORWARD=12323:2323` and the same OVMF pair. They ran after boot, without GDB
or packet capture attached. Each guest `ttcp -t -p 5001 -n 2048 -l 8192
10.0.2.2` sent 16777216 bytes to a manually started Python TCP sink bound to
`0.0.0.0:5001`, accepting one connection and reading 64 KiB chunks through EOF.
The sink checked the byte total for every run. Guest timing includes closure.
Three one-shot remote `log` commands used `--machine --no-shell-echo` and
`/usr/bin/time -p`, which reports wall time at 0.01-second resolution.

| Revision and mode | TCP seconds, three samples | TCP MiB/s | Remote log seconds | Log bytes |
| --- | --- | --- | --- | --- |
| Main `800f979`, before implementation | 12.378594, 12.321483, 12.367179 | 1.293, 1.299, 1.294 | 0.03, 0.02, 0.02 | 4394 |
| `8a4b944`, `LOG_UDP=0` | 13.070744, 12.054140, 11.837561 | 1.224, 1.327, 1.352 | 0.03, 0.03, 0.02 | 4394 |
| `8a4b944`, `LOG_UDP=1` | 12.602450, 12.440037, 12.462936 | 1.270, 1.286, 1.284 | 0.02, 0.03, 0.02 | 4393 |

The enabled mean is 1.280 MiB/s versus baseline 1.295 MiB/s, about 1.2% lower;
the disabled samples' larger variation does not establish an isolated kernel
cost. These are nested-VM end-to-end timings, not native driver throughput or
panic latency. Reserving one of sixteen VirtIO descriptors and two of thirty-two
RTL descriptors is the accepted capacity tradeoff, not a measured 6.25%
throughput loss. Idle following adds a 100 ms deadline; caught-up reads honor
it even when unrelated traffic keeps the worker running.

Remaining owner action: qualify enabled and disabled boot/panic capture on the
ThinkPad's RTL8111 with a host receiver on its LAN. Keep task 3 and the milestone
open until that result is recorded. All task-owned QEMU, debugger, host-client,
receiver and packet-capture jobs are stopped; the temporary TAP is removed.

## Working rules

- **Diagnostics:** output needed only to check the work goes to `ktrace` or is
  removed. The ring makes traces worth keeping, but `klog` lines stay for what
  the user needs.
- **Collisions:** the display milestone looks at the panic and early-console
  path that `kernel/log.c` feeds. Keep log-ring changes out of how the screen is
  drawn, and rebase rather than reshape the other milestone's code.
- **Measurements:** record any boot-time or presentation cost of the ring, with
  revisions and configuration, using existing tools.

## Out of scope

- Authentication and encryption, which wait for Pyxis authentication.
- Keeping the ring across a warm reset.
- DASH serial-over-LAN. It works but is fragile, so it is not relied on (owner,
  2026-10-07); the findings are kept in the
  [ThinkPad notes](thinkpad-next-steps.md#4-parked-dash-serial-over-lan-for-boot-logs).

## Related

[Remote terminal](../userland/remote-terminal.md),
[remote file transfer](remote-file-transfer.md),
[networking](../devices/networking.md), [DHCP](../devices/dhcp.md),
[ThinkPad target notes](../targets/t14-gen1-amd/notes.md) and
[ThinkPad next steps](thinkpad-next-steps.md).

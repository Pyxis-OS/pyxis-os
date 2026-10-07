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
    `d81475f799afb0162d7584587d83973bf48691ba` must merge before the parent
    integration PR. All task-owned QEMU, debugger and host-client jobs are stopped.



- [ ] **3. The kernel log over UDP.**
  - The kernel sends each log line as UDP datagrams to the host, and keeps
    sending during a panic, when locks may be held and userspace is gone.
  - Settle with the implementer:
    - a transmit path in the network drivers that does not depend on lwIP or
      locks a panic might interrupt;
    - where the datagrams go (broadcast, the beacon's host or a configured
      address) and whether sending starts before DHCP;
    - how it is turned on, and the host-side receiver.
  - **Finish when:**
    - in QEMU and on the ThinkPad, a host receives the boot log and a deliberate
      panic's message;
    - with the option off, nothing is sent.

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

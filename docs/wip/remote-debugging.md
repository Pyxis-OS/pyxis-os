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

- **The log is not kept.** `kernel/log.c` writes each line to serial, the early
  console and the Caelum tab. It keeps only the first 32 KiB, and only until
  the Caelum tab takes over. After that the tab scrolls and earlier lines are
  gone. No program can read the log.
- **The ThinkPad has no usable serial port.** DASH serial-over-LAN through the
  board's second Realtek controller is
  [parked](thinkpad-next-steps.md#4-parked-dash-serial-over-lan-for-boot-logs).
  The owner is exploring a Linux configuration path for it.
- **The remote terminal only listens.** The [remote terminal](../userland/remote-terminal.md)
  server listens on port 2323 in the live Remote space. A host must know the
  ThinkPad's address and connect after boot.
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

## Tasks

- [ ] **1. The kernel log ring and the `log` command.**
  - The ring (decision 1), the read-only grant and its forwarding through boot
    init, session and the shell (decision 2), and the native `log` command with
    `-f`.
  - Settle the read interface with the implementer: how a reader resumes after
    new lines arrive, and what it sees when lines were dropped.
  - **Finish when:**
    - in QEMU, `log` shows the boot log from its first line;
    - `log -f` shows lines logged after it started;
    - a remote shell can run both;
    - the ring's memory cost is recorded.

- [ ] **2. The reverse remote terminal.**
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
  - Settle with the implementer:
    - the beacon's format and the UDP port;
    - who holds the broadcast-receive authority (it is privileged today);
    - what happens when the connection closes, for example waiting for the next
      beacon and reconnecting;
    - how `pyxis-remote` runs as the listening server.
  - **Finish when:**
    - in QEMU, with the option set, Pyxis connects to a listening
      `pyxis-remote` without anyone typing its address;
    - without the option, nothing changes;
    - on the ThinkPad over PXE, the owner gets a shell and `log` this way.

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
- DASH serial-over-LAN, unless the owner's exploration makes it a separate
  task.

## Related

[Remote terminal](../userland/remote-terminal.md),
[remote file transfer](remote-file-transfer.md),
[networking](../devices/networking.md), [DHCP](../devices/dhcp.md),
[ThinkPad target notes](../targets/t14-gen1-amd/notes.md) and
[ThinkPad next steps](thinkpad-next-steps.md).

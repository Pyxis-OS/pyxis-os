# System hostname

Status: **proposal, 2026-10-09, awaiting owner decisions.** Nothing here
authorizes code.

Pyxis has no system name. The only name is the `remote.beacon=NAME` kernel
option, which selects a host for
[reverse connections](../userland/remote-terminal.md#reverse-connections). The
[UDP kernel log](../interfaces/kernel-log.md#udp-capture) identifies a machine
only by its network hardware address, and ports that call `gethostname()` have
nothing to call. [DNS](../userland/dns.md) resolves other hosts' names but never
names this one.

## Proposed contract

- **The name.** One RFC 1123 label: 1–63 ASCII letters, digits and hyphens, not
  starting or ending with a hyphen. No dots, so no domain part. It is stored as
  written; consumers that compare names, such as DNS and mDNS, ignore case.
- **Where it lives.** Live images and installed systems differ, because the
  installed boot archive on the ESP is replaced by every
  [system update](../userland/system-updates.md) and can only be changed by
  rebuilding it. A per-machine name cannot live there.
  - **Live and PXE.** A top-level `hostname` string in `live.lua`. The
    packaged `userspace/config/live.lua` carries a default, and
    `make image HOSTNAME=NAME` writes that name into the `live.lua` it packages,
    as `DISPLAY_SIZE` is applied at assembly today. A PXE entry needs no kernel
    option; the name is not one of the kernel command-line switches.
  - **Installed.** A small machine-settings record on the pool,
    `system://config/machine.lua`, returning `{ hostname = "t14" }`. Updates
    leave existing volumes untouched, so the name survives them. Boot init reads
    it after mounting `system`. `installed.lua` carries only the default,
    `hostname = "pyxis"`, used when the record is missing or invalid; boot init
    says so on the Caelum tab, as it does for the pool override.
  - **Not the boot override.** The record is separate from
    `system://config/boot.lua`, which sets inits and grants and is ignored whole
    when invalid. A syntax error there must not rename the machine, and the
    rescue entry, which ignores the override, still reads the record and boots
    with the real name. The record holds machine identity only. Other settings
    join it only by their own proposal.
  - **Reinstall.** Reinstall formats the pool, so the record is lost. The
    installer asks for the name and writes it, and may offer the old record's
    name as the default when the old pool is readable.
- **Validation.** Boot init validates every source with the rules above. An
  invalid value in `live.lua` or `installed.lua` stops that configuration like
  any other invalid field; an invalid pool record only falls back to the default.
- **Reading it.** A new `SYSTEM_INFO_HOSTNAME` query on the existing
  [system information](../interfaces/system-information.md) READ right returns
  the name as a fixed-size NUL-terminated string. The kernel stores the name;
  boot init sets it once per boot, before it creates any space, through a
  separate set-once right that only boot init holds. A second set fails. So
  every program sees a name from its first instruction, and none can change it.
- **libc.** `gethostname()` is built on that query through the `system_info`
  grant, like other libc functions that use named startup grants. A launch
  without the grant gets an error from `gethostname()`, not an invented name.
  There is no `sethostname()`; ports that need it can patch it out.
- **When a change takes effect.** At the next boot. There is no runtime
  rename, and no running program sees a different name.
- **Who may change it.** The image build for live images, the installer for a
  new system, and any space holding `system` read-write on an installed one: the
  `pyxis` space, through a later `hostname NAME` command that replaces the
  record atomically. The record sets a label, not authority, but it names the
  machine to the network, so it is writable only where the system volume is.

## Consumers

Each is a later task, in the order below.

- **Shell prompt and Fastfetch.** The prompt would read `NAME tmp://notes> `
  and Fastfetch would gain a hostname line.
- **Remote beacon.** The beacon name defaults to the hostname, so
  `pyxis-remote --listen NAME` and the machine agree without a second setting.
- **UDP log.** The log's wire header carries the name beside the hardware
  address, and `pyxis-log` prints it. Packets sent before boot init sets the
  name carry an empty one; the receiver keeps the last non-empty name per
  machine. The header is shared with a host tool in the same repository, so both
  change together.
- **DHCP option 12.** The userspace DHCP policy reads the name through the same
  query and offers it to the server. Whether the server registers it is up to
  the network.
- **mDNS.** Answering `NAME.local` needs a multicast responder, which does not
  exist yet. It would use this name unchanged.
- **Tailscale.** A later, Go-dependent direction. Its node name would default to
  the hostname.

## Owner decisions

1. **Where the installed name lives.** Default: the separate pool record
   `system://config/machine.lua`, read by boot init, with the archive's
   `hostname = "pyxis"` as the fallback. It survives updates, can change without
   an archive rebuild and survives a broken boot override. The alternatives are
   a `hostname` field in the pool override `boot.lua`, which needs no new file
   but loses the name whenever the override is invalid, or the boot archive,
   which every update replaces. Neither survives a reinstall.
2. **Read path.** Default: a kernel-stored name read through `system_info`, as
   above. The kernel copy is needed anyway for the UDP log, and a program cannot
   alter it. The alternative is a startup environment variable such as `TZ` and
   `DNS_SERVER`, set by the session launcher. It needs no kernel or ABI change,
   but the kernel log could not use it, and launches outside session would lack
   it.
3. **`remote.beacon`.** Default: retire the kernel option. The `remote` space
   instead sets a boolean in its configuration to take the reverse connection,
   named by the hostname, and `make image REMOTE_BEACON=` goes away. Nothing
   keeps the old form working. The alternative keeps `remote.beacon=NAME` as the
   switch with an explicit name that overrides the hostname, so one machine can
   have two names.

The unset name is the fixed `pyxis`, which the installer prompts to change.
Two machines left at the default collide in the UDP log, DHCP and mDNS; that is
accepted rather than inventing a random name the owner did not choose.

## Tasks

1. **Name and query.** The `hostname` field, `machine.lua`, validation,
   `HOSTNAME=` image assembly, the set-once right and `SYSTEM_INFO_HOSTNAME`,
   libc `gethostname()`, and a print-only `hostname` command. The owner can
   build an image with `HOSTNAME=t14`, or write the record on an installed
   system, reboot, and run `hostname` to see it, with an update leaving it
   alone.
2. **Prompt and Fastfetch.** The owner can see which machine a terminal belongs
   to from its prompt.
3. **Remote beacon.** The owner can run `make image HOSTNAME=t14` and
   `pyxis-remote --listen t14` with no separate beacon setting.
4. **UDP log.** The owner can read the machine's name in `pyxis-log` output.
5. **Installer and rename command.** The owner can choose a name at install and
   change it later with `hostname NAME` instead of by editing Lua.
6. **DHCP option 12.** The owner can find the machine by name in the router's
   lease list.

mDNS and Tailscale wait for their own proposals.

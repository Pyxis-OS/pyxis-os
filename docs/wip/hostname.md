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
- **Where it lives.** A top-level `hostname` string in the
  [boot configuration](../userland/init.md#boot-configuration), in both
  `live.lua` and `installed.lua`. It is not a kernel command-line option: the
  name is machine configuration, and the command line is for boot switches.
  Boot init validates it. An invalid value stops that configuration the way any
  other invalid field does.
- **Live and PXE images.** `userspace/config/live.lua` carries the packaged
  default. `make image HOSTNAME=NAME` makes image assembly write that name into
  the `live.lua` it packages, as `DISPLAY_SIZE` and `REMOTE_BEACON` are applied
  at assembly today. A PXE entry needs no kernel option, and a differently named
  machine is a different `make image`.
- **Installed systems.** `installed.lua` carries the same default. The
  [pool override](../userland/init.md#pool-override) `system://config/boot.lua`
  may set `hostname`, which replaces the default like any other override field.
  An invalid override is already ignored whole, so the default name applies.
  The rescue entry ignores the override and therefore boots with the default
  name, not the installed one.
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
  new system, and anyone with write access to `system://config/boot.lua` on an
  installed one. That file already chooses which inits run, so it stays
  administrative. A later `hostname NAME` command may write the override for
  the `pyxis` space, which holds the system volume read-write.

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

1. **Read path.** Default: a kernel-stored name read through `system_info`, as
   above. The kernel copy is needed anyway for the UDP log, and a program cannot
   alter it. The alternative is a startup environment variable such as `TZ` and
   `DNS_SERVER`, set by the session launcher. It needs no kernel or ABI change,
   but the kernel log could not use it, and launches outside session would lack
   it.
2. **`remote.beacon`.** Default: retire the kernel option. The `remote` space
   instead sets a boolean in its configuration to take the reverse connection,
   named by the hostname, and `make image REMOTE_BEACON=` goes away. Nothing
   keeps the old form working. The alternative keeps `remote.beacon=NAME` as the
   switch with an explicit name that overrides the hostname, so one machine can
   have two names.
3. **Default name.** Default: the fixed name `pyxis` in both packaged
   configurations, which the installer later prompts to change. Two machines on
   one network with the default would collide in the UDP log, DHCP and mDNS. The
   alternative generates `pyxis-` plus a few random hex digits at install time;
   it avoids collisions but gives a name the owner did not choose.

## Tasks

1. **Name and query.** The `hostname` field, validation, `HOSTNAME=` image
   assembly, the set-once right and `SYSTEM_INFO_HOSTNAME`, libc `gethostname()`,
   and a print-only `hostname` command. The owner can build an image with
   `HOSTNAME=t14`, or edit the config, and run `hostname` to see it.
2. **Prompt and Fastfetch.** The owner can see which machine a terminal belongs
   to from its prompt.
3. **Remote beacon.** The owner can run `make image HOSTNAME=t14` and
   `pyxis-remote --listen t14` with no separate beacon setting.
4. **UDP log.** The owner can read the machine's name in `pyxis-log` output.
5. **Installer and rename command.** The owner can choose a name at install and
   change it later with a command instead of by editing Lua.
6. **DHCP option 12.** The owner can find the machine by name in the router's
   lease list.

mDNS and Tailscale wait for their own proposals.

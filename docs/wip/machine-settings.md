# Machine settings and the system hostname

Status: **proposal, 2026-10-09, awaiting owner decisions.** Nothing here
authorizes code. It develops the "machine settings that survive updates"
entry in [later directions](later-os-directions.md), with the hostname as the
first setting.

Pyxis has no per-machine settings and no system name. The only name is the
`remote.beacon=NAME` kernel option, which selects a host for
[reverse connections](../userland/remote-terminal.md#reverse-connections). The
[UDP kernel log](../interfaces/kernel-log.md#udp-capture) identifies a machine
only by its network hardware address, and ports that call `gethostname()` have
nothing to call. The installed boot configuration lives in the boot archive on
the ESP, which every [system update](../userland/system-updates.md) replaces
and only a rebuild can change, so a per-machine value cannot live there.

## The store

The owner wants what is good in the Windows registry, kept inside the
filesystem. The store is a directory tree on the system pool, outside the boot
archive, so updates leave it alone.

- **Location.** `system://config/machine/`, beside `boot.lua`. User settings
  would be a separate tree under `home://` later. This proposal covers only the
  machine tree.
- **Keys.** A subtree is a directory and a key is a small file, so the registry's
  named hierarchy is `machine/network/hostname`, and `ls` and `cat` work.
  Nothing is packed into one opaque file.
- **Values.** A key's bytes are its value as text, with one trailing newline.
  The type is defined by the key's schema entry, not stored in the file: the
  first schema entry is `network/hostname`, one RFC 1123 label (1–63 ASCII
  letters, digits and hyphens, not starting or ending with a hyphen, no dots,
  compared case-insensitively and stored as written). Later entries may be
  booleans, integers or enumerations.
- **One validator.** The schema is shared userspace source, used by the writer
  and by boot init, so there is a single authority for what a key may hold.
- **Defaults.** The archive's boot configuration keeps the declarative
  defaults, `hostname = "pyxis"` in `live.lua` and `installed.lua`. The store
  holds only per-machine overrides; a missing key means the default.
- **Atomic writes.** The writer creates a temporary file in the key's
  directory, writes and syncs it, then renames it over the key, so the pool
  journal commits either the old value or the new one. The libc
  [directory-sync limit](../technical-debt.md#atomic-save-limits) applies: a
  crash can lose the new name, and can leave a stray temporary file that
  readers ignore because they open exact key names.
- **Live and PXE images.** A live boot has no pool, so there is no store. The
  value is the default in `live.lua`, and `make image HOSTNAME=NAME` writes that
  name into the `live.lua` it packages. A differently named live machine is a
  different image.
- **The boot override stays separate.** `system://config/boot.lua` sets inits and
  grants and is ignored whole when invalid. A syntax error there must not rename
  the machine, and the rescue entry, which ignores the override, still reads the
  store and boots with the real name.

### Validation and authority

Validation happens in two places for different reasons. The writer rejects a bad
value before it reaches the pool, which is the useful part for the person
typing. Every reader also validates, because anything holding write access to
the system volume can create a file by hand. A bad value is reported and the
default applies.

Authority is capabilities. A reader needs a read-only directory grant to the
subtree it uses, attenuated from the system root with the existing directory
rights, such as `machine/network` for a DHCP client. Only the config tool and the
installer get write access, to the part of the tree they own. Today the `pyxis`
space holds the system volume read-write for every program it runs, so write
access is limited by convention, not enforced. Narrowing it, so that only the
tool holds a write grant to `machine/` from trusted init, is a later task.

### How it is exposed

Two shapes give validation and atomicity:

- **Plain directory with conventions and a validating writer.** The tree is
  ordinary npfs files. The `config` tool validates and writes; everything else
  reads files. Boot init needs nothing new, because it already mounts `system`
  and reads `boot.lua` from it, and it can read a key before any service runs.
  There is no running code to keep alive.
- **A `settings://` provider.** A userspace service owns the directory, so
  writes cannot bypass validation, and it can implement change notification. It
  must be running before readers use it, boot init still has to read the files
  directly, and it adds a process, a namespace binding and a failure mode.

The recommendation is the plain directory. The provider can front the same
files later without changing them.

### Change notification

Registry-style notification maps to Pyxis's readiness waits: a reader would
wait on its subtree grant until the directory's generation differs from the one
it supplies. The generation counter exists, but `wait_many` accepts no directory
objects today, so this needs a new readiness bit on directory grants. No setting
needs a live change yet, since the hostname applies at boot, so it is not built
until a consumer such as DHCP re-announcing a rename needs it.

## The hostname

- **Where the installed value lives.** `machine/network/hostname` in the store.
  Boot init reads it after mounting `system`, falls back to the archive default
  when it is missing or invalid, and says so on the Caelum tab. An invalid
  `hostname` in `live.lua` or `installed.lua` stops that configuration like any
  other invalid field.
- **Not a kernel option.** The name is machine configuration, and the command
  line is for boot switches.
- **Reading it.** A new `SYSTEM_INFO_HOSTNAME` query on the existing
  [system information](../interfaces/system-information.md) READ right returns
  the name as a fixed-size NUL-terminated string. The kernel stores the name,
  checking only length and printable ASCII. Boot init sets it once per boot,
  before it creates any space, through a separate set-once right that only boot
  init holds. A second set fails. Every program sees a name from its first
  instruction and none can change it. Programs read the name through this query,
  not the files, so they need no store grant.
- **libc.** `gethostname()` is built on that query through the `system_info`
  grant, like other libc functions that use named startup grants. A launch
  without the grant gets an error, not an invented name. There is no
  `sethostname()`.
- **When a change takes effect.** At the next boot. No running program sees a
  different name.
- **Who may change it.** The image build for live images, the installer for a
  new system, and the `config` tool on an installed one.
- **Reinstall.** Reinstall formats the pool, so the store is lost. The installer
  asks for the name and may offer the old store's name as the default when the
  old pool is readable.

## Consumers

Each is a later task, in the order below, and reads the name through the query.

- **Shell prompt and Fastfetch.** The prompt would read `NAME tmp://notes> `
  and Fastfetch would gain a hostname line.
- **Remote beacon.** The beacon name defaults to the hostname, so
  `pyxis-remote --listen NAME` and the machine agree without a second setting.
- **UDP log.** The log's wire header carries the name beside the hardware
  address, and `pyxis-log` prints it. Packets sent before boot init sets the
  name carry an empty one; the receiver keeps the last non-empty name per
  machine. The header is shared with a host tool in the same repository, so
  both change together.
- **DHCP option 12.** The userspace DHCP policy offers the name to the server.
  Whether the server registers it is up to the network.
- **mDNS.** Answering `NAME.local` needs a multicast responder, which does not
  exist yet. It would use this name unchanged.
- **Tailscale.** A later, Go-dependent direction. Its node name would default to
  the hostname.

## Owner decisions

1. **Exposure.** Default: the plain directory with conventions and a validating
   writer, as above. The alternative is the `settings://` provider, which
   enforces validation on writes and can notify, at the cost of a running
   service that boot init cannot depend on.
2. **Read path.** Default: the kernel-stored name read through `system_info`. The
   kernel copy is needed anyway for the UDP log, and a program cannot alter it.
   The alternative is a startup environment variable such as `TZ` and
   `DNS_SERVER`, set by the session launcher. It needs no kernel or ABI change,
   but the kernel log could not use it, and launches outside session would lack
   it.
3. **`remote.beacon`.** Default: retire the kernel option. The `remote` space
   instead sets a boolean in its configuration to take the reverse connection,
   named by the hostname, and `make image REMOTE_BEACON=` goes away. Nothing
   keeps the old form working. The alternative keeps `remote.beacon=NAME` as the
   switch with an explicit name that overrides the hostname, so one machine can
   have two names.

The unset name is the fixed `pyxis`, which the installer prompts to change. Two
machines left at the default collide in the UDP log, DHCP and mDNS; that is
accepted rather than inventing a random name the owner did not choose.

## Tasks

1. **Store and hostname.** The store convention and the hostname schema entry,
   shared validation, boot init reading the key (validated on read) with the
   archive default as fallback, `HOSTNAME=` image assembly, the set-once right
   and `SYSTEM_INFO_HOSTNAME`, libc `gethostname()`, and a print-only `hostname`
   command. No writer yet. The owner can write the key by hand on an installed
   system, reboot, run `hostname`, then update the system and see the name kept,
   or build a live image with `HOSTNAME=t14`.
2. **The `config` tool.** `config get`, `set` and `list` with schema validation
   and atomic writes. The owner can run `config set machine/network/hostname t14`,
   see a bad name rejected, and have the new one apply at the next boot.
3. **Prompt and Fastfetch.** The owner can see which machine a terminal belongs
   to from its prompt.
4. **Remote beacon.** The owner can run `make image HOSTNAME=t14` and
   `pyxis-remote --listen t14` with no separate beacon setting.
5. **UDP log.** The owner can read the machine's name in `pyxis-log` output.
6. **Installer.** The owner can choose a name at install, and keep the old one
   when reinstalling.
7. **DHCP option 12.** The owner can find the machine by name in the router's
   lease list.

Narrowing write access to the tool, directory change notification, a user
settings tree under `home://`, `config validate` as the validation half of
Polaris (see the [boot configuration checker](boot-configuration-checker.md)),
mDNS and Tailscale wait for their own proposals.

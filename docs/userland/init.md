# Init and session startup

The kernel starts one trusted **boot init** from the boot archive, in Caelum's
log space. Boot init reads the boot configuration, mounts each configured
volume once, creates the configured spaces in order and starts each space's
init, then exits. Each space gets its own tab, terminal, display and input and
runs one trusted init. The number of spaces does not depend on the CPU count.
Children stay in their parent's space. This introduces no migration,
supervision or global PID 1.

Space inits publish services through their caller-bound
[process environment](../interfaces/environment.md), then explicitly stage the
selected bindings for session handoff. Environment authority is independent of
space membership; controllers remain private unless deliberately delegated.

Space inits share the read-only `boot://` archive and writable RAM-backed
`tmp://` tree. It is not private per space and disappears on reboot. Its root
also carries FILESYSTEM_INFO, so a program can confirm it is RAM-backed, as
`pkg upgrade` does for its publisher workspace (chronium, 2026-10-23). Spaces
normally start in `home://`: the pool's `home` volume on installed systems and
a RAM volume on live boots. Raw
installer resources are issued only on the separate install path. Init scripts
are trusted setup policy; the session handoff delegates resources to ordinary
applications.

## Boot command line

The kernel accepts options in a command line of at most 4095 bytes without
quoting or escaping. Unknown options warn and are ignored; duplicates warn and
the last occurrence wins. Invalid final values warn and use their safe fallback.
See the [complete option/fallback list](../development/configuration.md#kernel-boot-option-recovery).

| Option | Meaning |
| --- | --- |
| `init=boot://IMAGE` | The boot init, a native PXE or shebang archive entry; default `boot://boot-init.pxe`. |
| `mount.disk=GUID` | The disk native mounts may use. |
| `mount.pool=N` | With `mount.disk`, the pool's one-based GPT partition; boot init then reads the [installed configuration](#installed-configuration). |
| `boot.install=1` | The install entry: boot init also receives the raw installer resources. |
| `boot.default_config=1` | The rescue entry: boot init starts the built-in rescue space. |
| `remote.beacon=NAME` | The configured `remote` space discovers a host beacon and connects out. |
| `log.udp=1` | Enable the [UDP kernel log](../interfaces/kernel-log.md#udp-capture) after networking initialization. |
| `log.level=LEVEL` | The lowest [log level](../interfaces/kernel-log.md#levels) drawn on serial, the early console and the Caelum tab; default `info`. |
| `display.size=WIDTHxHEIGHT` | Set an exact initial Bochs pixel size; see [QEMU display selection](../development/qemu.md#display-device). |

Normal live and installed boots use `init=boot://boot-init.pxe`; an absent or
malformed init option selects it. Invalid mount GUIDs select no disk, invalid
install/write settings grant no authority, and invalid/incomplete debugger
settings leave the debugger off. The entire command line exceeding 4095 bytes
still stops boot, because truncation could change meaning.
Options are parsed once before display and AP initialization into retained kernel
storage; boot init consumes the same result later. Malformed/unsupported
`display.size` keeps firmware geometry with a warning; backend geometry/capacity
validation applies to the selected Bochs device, not VirtIO or an unsupported
native GPU. `DISPLAY_SIZE` supplies it during image assembly, including rescue
and installer entries; an empty build setting omits it.

Boot init runs on CPU 0 in Caelum's space. Its messages go to the Caelum tab and
to serial. It receives console output but no input, the bootstrap services, the
`boot://` and `tmp://` roots, the `space_factory` resource, a private RAM
directory as the `ram` resource, the HOST and native mount authority when
present, and, on the install entry, the raw installer resources. Arguments add `--installed` when a disk is bound,
`--pool N` for its `mount.pool` or `--pool-malformed` when that value is
malformed, and `--default-config` for the rescue entry.

The normal stock trusted boot-init entry holds private MEMORY MANAGE and
CREATE_SHARED authority. Installer and custom-init entries retain their existing
MANAGE-only grant; see
[shared memory](../interfaces/shared-memory.md).

`remote.beacon` adds `--remote-beacon NAME`. The name is 1–63 printable ASCII
bytes without spaces, compared case-sensitively. Boot init supplies it only to
the configured space named `remote`, together with separate BROADCAST-only
`udp_beacons` authority. The trusted session bootstrap passes both to the
remote daemon; its shells receive neither. The option creates no space when
the boot configuration lacks `remote`. See [reverse connections](remote-terminal.md#reverse-connections).

## Boot configuration

Boot init reads `boot://config/live.lua`, or the pool's
`system://config/boot.lua` when the boot entry names `mount.pool` (see
[installed configuration](#installed-configuration)). Each file returns a
`hostname` default, named `volumes`, a list of `spaces` and, when a volume uses
`ram-npfs`, `ram_npfs_bytes`:

```lua
return {
  volumes = {
    host = { kind = "virtio-fs" },
    system = { kind = "npfs", partition = 2, volume = "system" },
    home = { kind = "npfs", partition = 2, volume = "home" },
    scratch = { kind = "ram" },
  },
  spaces = {
    { name = "development", title = "Development", init = "boot://init",
      network = true, focus = true, launch = true, screenshot = true, cpus = { 1, 2, 3 },
      roots = { host = { access = "read-write", optional = true },
                system = "read-only", home = "read-write" } },
    { name = "scratch", title = "Scratch", init = "boot://init-readonly",
      roots = { scratch = "read-write" }, start = "scratch" },
  },
}
```

- **Volumes.** An `npfs` volume names a one-based GPT partition on the bound
  disk and a volume name. In the pool's `boot.lua`, an `npfs` volume without a
  `partition` is on the pool that `mount.pool` names. A `virtio-fs` volume is the HOST export. A `ram`
  volume is an empty directory in boot init's RAM directory, named after the
  volume; spaces that name it share it until reboot. A `ram-npfs` volume takes a
  volume name like `npfs` but has no disk: it is a named npfs volume in a RAM-backed
  pool shared by every `ram-npfs` volume, which keeps npfs semantics and is lost at
  reboot. The archive-level `ram_npfs_bytes` sets that pool's capacity and is required
  by any `ram-npfs` volume; it must be a multiple of 4096 from 8 MiB to 64 GiB. The
  packaged live configuration uses 2 GiB for its `system`, `packages` and
  `http-work` volumes. Ordinary `ram` volumes are unchanged. Names use 1–31 characters
  from `a-z`, `0-9` and `-`, other than `boot`, `tmp` and `bin`, and become the
  root's scheme.
- **Spaces.** `name` follows the same rules, other than `rescue` and `caelum`,
  and identifies the space in logs; it grants nothing. `title` defaults to the
  name. `init` names a `boot://` entry or, in the pool's configuration, a
  `system://` file such as `system://config/init-installed`. `cpus` lists boot CPU indices, the
  space's ceiling; without it the space may use every CPU. Tab order follows
  the list.
- **Roots.** Each root names a volume and its access, `read-only` or
  `read-write`, or a table that can also mark it `optional`. A volume is
  mounted once, read-write if any space needs that, and each space receives only
  the access it asks for. FAT volumes instead use fixed configured admission access
  and ordinary RO roots; see [selected FAT grants](#fat32-volumes-and-selected-grants).
- **Start.** A space's working directory is `home://`. `start` names another of
  its roots, such as `tmp`, `boot`, `bin` or a configured one; naming a root
  the space does not have is an error. A space without the root it would start
  in, such as one with no `home` root or a missing optional root, starts in
  `tmp://` and boot init says so.
- **Network owner.** At most one space sets `network = true`. Boot init first
  defines the ordered network profile, binds its selected controllers and sets
  route, DNS and management policy. Only the owner's trusted init receives
  private policy authority, each declared interface's configuration facet and
  its pinned DHCP broadcast factory for `session --configure-network`.
  Other spaces receive registry READ and scoped ordinary transport grants.
  Without an owner, no session applies static addresses or maintains DHCP.
- **Declared TCP listeners.** `tcp_listen` is an optional boolean, defaulting to
  false. It supplies a private TCP LISTEN source to trusted startup, local/mux
  sessions and Remote handoffs. Only a bundle explicitly declaring TCP `listen`
  receives it, from that held source; ordinary children retain CONNECT only.
  Every shipped space leaves it false. This is interface-scoped authority over
  concrete IPv4/nonzero-port binds, not a configured-port allowlist, and it grants
  no network configuration rights. See [Caddy](caddy.md) and the
  [TCP contract](../devices/tcp.md#listening-and-admission).
- **Boot focus.** `focus` is an optional boolean, defaulting to false. At most
  one space in the configuration may set it. See [boot focus](#boot-focus-space)
  for readiness and input cancellation.
- **Ordinary child launch.** `launch` is an optional boolean, defaulting to
  `false`. With `launch = true`, boot init supplies a separate `child_launcher`
  resource with LAUNCH alone, and a private `interpreter_root` resource, the boot
  archive root from which trusted shells open their script interpreter.
  Trusted init and session handoff preserve both;
  the shell delegates it as `launcher` to ordinary foreground commands, including
  every foreground pipeline stage. Exact held-shell scripts
  [retain their shell's launch sources](shell.md#script-runtime-grants) in
  foreground, pipeline, background and capture positions, with each launcher
  bound to the permitted execution scope. Ordinary plain background commands and
  services do not receive a launcher through this profile; bundles still use
  their explicit declarations. LAUNCH includes scoped subgroup creation; it does
  not delegate group supervision or add terminal input rights.
- **Declared native jobs.** `jobs` is an optional boolean, defaulting to false.
  It makes a private RAM provisioning tree available to trusted startup and
  explicit successor handoff. A bundle must request `job_work`, plus usable
  launcher/pipe/clock grants; the trusted launch path creates a fresh leaf for
  that client. Ordinary commands receive neither leaf nor ancestor implicitly.
  Live Development, Multiplexer and installed `pyxis` enable it; other spaces
  can opt in.
  The setting does not enable ordinary launch when `launch` is false. See
  [declared contexts](program-bundles.md#private-job-contexts).
- **Generated code.** `jit` is an optional boolean, defaulting to false. True
  provisions a private JIT source through trusted startup/session/mux/Remote
  handoffs. Exact held-shell scripts retain an available source and offer it to
  explicitly declaring bundles just as their launching shell does. Shell bundles
  remain subject to their declarations, including optional JIT; ordinary native
  children and Lua interpreters keep their existing launch rules. Live
  Development, Multiplexer and Remote enable it; live Read-only and installed
  defaults leave it false. A script cannot enable this switch or mint a missing source.
  This source gate does not revoke capabilities explicitly transferred by a
  native caller. See
  [JIT memory](../interfaces/jit.md).
- **Multiplexer.** `multiplexer` is an optional boolean, defaulting to false.
  It supplies a distinct trusted `mux_terminal` grant. After configuration and
  service setup, the session launcher selects `bin://mux.pxe`, giving it
  terminal CREATE and launcher LAUNCH. It does not grant either resource
  to ordinary shell commands. See the [multiplexer guide](multiplexer.md).
- **Clipboard.** `clipboard_local` and `clipboard_shared` independently request
  the [store grants](../interfaces/clipboard.md). Stock local interactive spaces
  set both true; remote startup receives neither. If omitted, each setting
  retains the existing `multiplexer` default; explicit false withholds that layer.
  Local session/shell and mux pane handoffs forward each on foreground startup;
  background commands and services receive neither. Grants alone authorize no
  read: graphics calls require display/keyboard ownership and physical activation.
- **Remote multiplexer.** `remote_multiplexer` is an optional boolean, defaulting
  to false. It preserves a distinct terminal CREATE grant through trusted startup
  to the remote daemon, which brokers panes for explicit `pyxis-remote --mux`
  requests. It does not enable local mux, pointer control or clipboard stores.
  The packaged live Remote space enables it; installed defaults have no Remote
  space, so nothing sets it there. See
  [remote mux](remote-terminal.md#remote-multiplexer) for admission and LAN exposure.
- **Power.** A space that sets `power = true` receives the kernel's `power`
  resource, with power-off and restart rights, for the shell's
  [`poweroff` and `reboot`](shell.md#power-off-and-restart). Any number of spaces
  may set it. Live Development, Multiplexer and installed `pyxis` do, and boot
  init's built-in rescue space always has it. Read-only does not.
  `remote_power` is a separate optional boolean, defaulting to false: it grants
  the same object as `remote_power` through trusted session handoffs, and the
  remote supervisor binds it as `power` only in its root shell. Ordinary local
  `power` never authorizes remote shells. Live/PXE Remote sets
  `remote_power = true`; installed defaults leave it absent and keep `pyxis`'s
  local power unchanged. The unauthenticated live terminal lets any reachable
  LAN peer reboot or power off the machine, an owner-accepted sole-user home-LAN
  exposure. Ordinary programs and service providers inherit neither grant.
- **Screen capture.** `screenshot` is an optional boolean, defaulting to `false`.
  With `screenshot = true`, boot init delegates the separate `screen_capture`
  resource with CAPTURE authority. It observes the whole shown local screen,
  independently of per-space DRAW and filesystem authority. Trusted init,
  session, remote-daemon and shell handoffs preserve it explicitly; ordinary
  foreground commands, every pipeline stage and background commands inherit it
  from a shell that holds it. This is a space policy, not an executable-name
  restriction. Live Development, Multiplexer, Remote and installed `pyxis` opt in;
  Read-only and the built-in rescue space do not. If an opted-in space's boot
  init lacks the resource, that space does not start and reports the reason.
  See [screen capture](../interfaces/screen-capture.md) and the
  [PNG command](screenshot.md). The same switch starts a private per-space saver
  for [Print Screen](screenshot.md#print-screen); ordinary programs receive no
  saver notification or navigation-feedback authority.
- **ACPI tables.** `acpi_tables` is an optional boolean, defaulting to `false`.
  With `acpi_tables = true`, boot init binds the kernel's read-only
  [table root](../kernel/acpi.md#table-export) as the `acpi` root, so
  `acpi://` lists and reads the firmware tables. It is a root like any
  configured volume: session, remote-daemon and shell handoffs carry it on and
  every command in such a shell inherits it. Live Development and Remote opt
  in; Multiplexer, Read-only, installed `pyxis` and the built-in rescue space
  do not, and an installed space may opt in by configuration. If an opted-in
  space's boot init lacks the root, that space does not start and reports the
  reason. See the [acpidump command](acpidump.md).

The scheduler places each task on the least-loaded CPU its space allows and
balances tasks between CPUs; see [placement and migration](../kernel/smp.md#placement-and-migration).
CPU 0, the BSP, runs userspace too, but placement ties prefer the other CPUs,
because it also runs the kernel workers and boot init. A new space's init has
no placement preference. Boot init creates spaces one at a time, and an earlier
init is usually blocked by then, so on four CPUs all three live inits start on
CPU 1; balancing later moves runnable tasks to idle CPUs.

Every space also receives `bin://`, read-only. On installed boots it is the live
command view of the installed packages ([packages](../interfaces/packages.md));
without a packages volume it is the archive, and boot init says so. On live boots
it is the archive itself. It also supplies the exported read-only `path`
list: `bin://`, then a distinct `boot://`; on a live boot bin and boot are one
entry, followed by the live package-command view when a packages volume is
mounted.
Space/session/mux/remote handoffs preserve that list. Search never recovers
omitted entries from the independently held bin/boot roots.

Space inits receive the bootstrap services, `boot://`, `tmp://`, `bin://`,
their configured roots, the working directory their start selects and `OS_NAME`, plus their space's own
console, keyboard, pointer, display and space handle from the kernel. They do
not receive raw mount authority or the space factory by default; `sync PATH...`
still works on their roots. Explicit [FAT opt-ins](#fat32-volumes-and-selected-grants)
may supply mount-only selection or selected MOUNT/data grants.

A space whose required volume is missing or fails to mount, whose CPU set names
an absent CPU, or whose init cannot be opened is created but not started. Its
tab and the log say why, and the other spaces start. If no space starts, boot
init creates the built-in `rescue` space running `boot://init-shell.pxe` with
`boot://`, `tmp://` and `bin://`, starting in `tmp://`. It runs the core archive
alone: its `bin://` is the archive and its command list holds no installed
packages. On an installed boot it also receives the pool's `system://`
read-write, so it can repair the configuration, and package management of the
pool's `packages` volume, so `pkg` can reinstall or remove packages
([decision 18](../wip/pkg-upgrade.md)); without that volume it starts without
`pkg`. It also gets HTTP staging on the pool's `http-work` volume. To fetch,
start DHCP and the HTTP provider from the archive's copy of the services script:

```text
session boot://session.pxe --configure-network --start-services boot://share/installer/system/init-services
pkg install base
``` It owns the network with the archive's built-in profile,
`boot://config/network.lua`; `session boot://session.pxe --configure-network`
from its shell starts DHCP. If that profile cannot be applied either, rescue
has loopback only.

The archive `hostname` defaults to `pyxis` and follows the
[machine-settings label schema](machine-settings.md#hostname-schema). Installed
boots select the persistent key after mounting system, including rescue boots;
the `hostname` in `boot.lua` is only its fallback. Boot init sets the kernel name once before creating
spaces and forwards only system-information READ authority. Live image builds
may override the archive value with `make image HOSTNAME=NAME`.

### Installed configuration

An installed boot reads its whole configuration from the `system` volume of
the pool that `mount.pool` names, at every boot. Boot init mounts that volume
read-write for the spaces and the rescue space but only reads these files:

| File | Contents |
| --- | --- |
| `system://config/boot.lua` | Hostname fallback, input defaults, volumes and spaces, as above. |
| `system://config/session.lua` | The [session configuration](session-configuration.md). |
| `system://config/network.lua` | The network profile, as in `boot://config/network.lua`. |
| `system://config/init-installed` | The `pyxis` space's init script. |

Install copies these from `boot://share/installer/system/` once; afterwards they
belong to the administrator, and Update never changes them (chronium,
2026-10-22). Update does create `system://config/` if it is missing and seeds a
missing `packages.lua`, the package client's configuration, as before. An edit applies at the next boot. Nothing in the archive merges
with them or replaces them. The three Lua files are declarative: a literal
`return { ... }` table, read by the same literal parser as `config validate`,
bounded by its 1 MiB source limit and never executed. Session and network
bytes reach the spaces as private READ-only handoff files.

A missing or invalid file, a network profile that names absent controllers or
cannot be applied, or a system volume that cannot be mounted starts only the
built-in rescue space. Boot validates all of it before applying any, so a
rejected configuration contributes nothing: the hostname fallback, input
defaults, Wi-Fi policy and RAM pool size are built-in, the spaces get no
session/network handoff, and the network uses the archive's profile, as for
the rescue space above. A present but
malformed `mount.pool` names no pool, so its rescue space has no `system://`.
One line names the file and the reason, on the Caelum tab, serial and `log`
(info):

```text
boot-init: system://config/boot.lua: line 35 column 1: unexpected bytes after return table; starting the rescue space
```

The rescue space receives `system://` read-write, so the files can be fixed or
restored from the release's copies, for example:

```sh
cp boot://share/installer/system/boot.lua system://config/
cp boot://share/installer/system/* system://config/
```

The second form copies every file in one command ([wildcards](shell.md#wildcards)). A valid
configuration whose spaces all fail to start also starts the rescue space.

Menus written before `mount.pool` existed lack it. Update does not recognize
their entries as its own, so it refuses or leaves boot activation pending:
those machines reinstall (decision 28 of [package upgrades](../wip/pkg-upgrade.md)).

The installed disk's second boot entry, **Pyxis OS (rescue)**, passes
`boot.default_config=1` and starts the rescue space without reading the pool
configuration. Write access to `system://config/` chooses which inits run with
forwarded grants on the next boot, so treat it as administrative.

A development build with `MOUNT_DISK` binds a disk but names no pool. It reads
`boot://config/live.lua`, whose `npfs` volumes then name their partitions
(chronium, 2026-10-22).

### Development builds

Live spaces exist only in `DISTRIBUTION=full` images; the default core image
boots Install and Rescue only ([decision 64](../wip/pkg-upgrade.md)). In a full
image, edit `userspace/config/live.lua`, also for builds with `MOUNT_DISK`, to
change spaces, CPU sets and roots. `INIT`, `LIVE_CONFIG`, `HOSTNAME` and
`GO_HELLO` need `DISTRIBUTION=full`. `INIT` names one host file,
relative to the repository root or absolute, staged as `boot://init`, the
Development space's init. It can be a native PXE executable or a shebang script:

```sh
make run DISTRIBUTION=full CPUS=4
make run DISTRIBUTION=full INIT=/tmp/init.pxs
make image DISTRIBUTION=full INIT=build/userspace/shell.pxe
make image DISTRIBUTION=full       # restore the packaged init
```

Additional custom archive entries belong in the
[boot assembly manifest](../development/boot-archive.md). Kernel-only builds do
not select or package init.

## Install boot selection

The build setting `BOOT_MENU_TIMEOUT` defaults to `5` seconds on a core image and
`0` on a full image, which boots normally without a menu delay. The `Install Pyxis` entry is generated for either timeout. A core
image's menu has only `Install Pyxis`, its first and default entry, and then
`Pyxis OS (rescue)`, which starts the built-in rescue space
*(chronium, 2026-10-23)*. Install writes nothing without its typed consent
word. A full image keeps its live entries, `Install Pyxis` and the FAT32 tools
in their existing order. Its
command line is `init=boot://boot-init.pxe boot.install=1`, and only on that
entry does boot init receive `disks` (it also keeps `boot_kernel` and
`boot_archive` instead of closing them). Without the one-shot input policy and
hostname right, which the kernel issues only off this entry, it applies no
machine settings.

Holding `disks`, boot init reads no configuration and starts one built-in
`install` space ([decisions 63 and 66-70](../wip/pkg-upgrade.md)). Its init is
`boot://installer-init.pxe`, the network owner for the archive's
`config/network.lua`, with HTTP staging in a RAM `http-work` directory and Wi-Fi
READ and CONNECT when the radio exists. Boot init installs the world-domain Wi-Fi
channel policy (country `00`, channels 1-13; [Wi-Fi](../interfaces/wifi.md#authority-and-startup))
for that boot, since the install entry reads no configuration. Nothing joins
Wi-Fi in 0.0.8; they are there for the TUI installer's join
([decision 70](../wip/pkg-upgrade.md#decided)). It alone receives the installer
capability as private resources: `disks` with every installer disk right,
`boot_kernel`, `boot_archive`, `power` narrowed to restart,
`installer_diagnostics` (a WRITE copy of the Caelum console) and
`installer_scratch` (a private RAM directory). Boot init then closes its own
copies. If the space cannot start, the rescue space starts instead.

installer-init applies the network profile with the shared network runtime and
starts `boot://httpfs.pxb` itself, receiving its `http` and `https` provider
roots. A thread keeps the network serviced from then until the installer exits,
so a late lease still completes. It then offers Install,
Update and Replay. For Install, or a Replay of an Install plan, it waits up to
30 seconds for the network's DNS choice and restarts httpfs if it is missing or
started before DNS was known; only then does the installer get the providers
and a child launcher, with the chosen action's narrowed disk rights. Without
network Install is refused; Update needs none. It returns to the action list if
the installer fails. No
shell or session runs in this space. The installer implements the
[interactive installation flow](installer.md). See
[installer authority](../devices/installer-authority.md) for the source-file,
raw-claim and handoff contracts.

The installed menu has two entries and a three-second menu:
`init=boot://boot-init.pxe mount.disk=<GUID> mount.pool=<N>`, and the rescue
entry, which adds `boot.default_config=1`. The initial
[installed configuration](#installed-configuration) starts the `pyxis` space
with `system://config/init-installed`, `system://` and `home://` read-write,
starting in `home://`, and network ownership. `home` is optional: if the volume
cannot be mounted, `pyxis` still starts, in `tmp://`. `system` is required. That init starts the
ordinary local session and configures networking. `tmp://` remains RAM-backed.
The packaged live Development, Multiplexer and Remote, and installed `pyxis`
entries set `launch = true`; the live Read-only entry leaves it disabled.
Live boots give Development and Multiplexer read-write RAM `home://`, and
Read-only the same root read-only. Remote has it read-write and starts in
`tmp://`. Multiplexer follows Development in tab order and starts mux using
`boot://init-readonly` to leave network setup with Development; its other flags,
grants and roots match Development. Installed disks omit the installer
entry; enter install mode through live media.

## Boot focus space

Caelum starts active and stays the pinned first tab. The shipped live configuration
sets `focus = true` on Development; the installed configuration sets it on `pyxis`.
The built-in Install and Rescue spaces also request it, so their prompt or
menu replaces Caelum's log once it draws. This applies to both unmounted and
pool-backed Rescue, including fallback from a rejected configuration.
After successful initial launch, the presenter selects that space once it has
nonblank terminal content or a validated submitted graphics frame. A silent
startup stays on Caelum. Creation or initial-launch failure leaves Caelum active
with the reason; a later runtime failure does not switch back. Panic still takes
over the display.

Physical key presses and pointer button clicks cancel the automatic selection,
even before the space is created. Pointer motion, wheel-only reports, releases
and tagged synthetic input do not cancel it. Held input discovered at attachment
or after lost continuity is not a fresh press. Manual tab/layer choices win and
there is no retry. This boot policy grants no focus right to ordinary children.

The shared `libconfig` schema and plan validation admit `focus` for boot init
and `config validate`. More than one `focus = true` is a configuration error:
the configuration is rejected and the built-in Rescue starts with its own
boot-focus request. Omitting focus from valid configured spaces keeps Caelum
active.

## Native disk configuration and mounting

`MOUNT_DISK=<canonical-GPT-GUID>` selects the deployment disk and generates
`mount.disk=<GPT-GUID>` in Limine configuration, plus the rescue entry. It
names no `mount.pool`, so boot init reads `boot://config/live.lua`.
Omission disables native mount authority. Malformed, zero or duplicate
configuration fails setup; kernel parsing also rejects malformed or duplicate
options outside the Make generator. The GUID selects a disk but authenticates
neither it nor its contents. The filesystem has no on-disk principal or
permission policy.

Prepare a populated GPT image using the
[native adapter instructions](../devices/filesystem-native-adapter.md#prepare-a-disposable-disk),
describe its volume in `userspace/config/live.lua`, then attach it with
configuration matching that image:

```lua
volumes = { data = { kind = "npfs", partition = 1, volume = "system" } },
spaces = {
  { name = "native", init = "boot://init", network = true,
    roots = { data = "read-write" } },
},
```

```sh
make run CPUS=4 INIT=/tmp/init-native.pxs \
  VIRTIO_BLK_IMAGE=/absolute/path/to/development.raw \
  MOUNT_DISK="<GPT_GUID>"
```

Replace the GUID placeholder only in local configuration. Do not change an
attached image from the host. The space's init receives `data://` and can hand it to a session:

```text
#!boot://init-shell.pxe
session boot://session.pxe --start-services boot://init-services
```

Use `read-only` roots and `VIRTIO_BLK_READONLY=1` for a read-only device.
Read-only pool opening refuses a committed journal; writable opening validates
and replays it before exposing records. Unknown required features prevent
opening; unknown read-only-compatible features prevent writes and replay.

For boot-present USB BOT media, enable `CONFIG_XHCI=y` and attach the existing
image read-only. An ISO can select its actual disk GUID without rebuilding that
USB image. For the sample image, the installed configuration can name
`usb = { kind = "npfs", partition = 2, volume = "usb-test" }` and give a space
a read-only `usb` root. Build with `make image MOUNT_DISK=<actual-GPT-GUID>`
and attach the selected disk through xHCI; see the
[USB storage bring-up record](../development/usb-storage-bringup.md).
For explicitly writable attachment of a selected disposable image, the root can
be `read-write` instead. The mount requires known WP-clear media and
successful blocking cache-synchronization qualification; unqualified or latched
write-failed disks refuse writable opening. Qualified USB mounts use the existing
filesystem write, sync and replay path. Installer/public raw USB access remains
deferred, and physical write qualification is separate.
`usb://bin/cat.pxe usb://README.txt` captures the executable through the delegated
file grant before launching it. The
[persistent USB development walkthrough](../development/edit-build-run.md#persistent-usb-development)
shows editing, compiling, explicit sync and reuse of the same private disk.

Boot init receives the configured disk scope. The
`native_mount` resource is issued unless inventory establishes hardware absence.
Pending discovery retains the scope rather than caching a permanent failure.
Mount waits for sealed discovery and all terminal GPT scans, selects the sole
observed matching GUID, rejects duplicate observed matches, and selects a
one-based partition entry and volume name. A known unique match may mount under
partial discovery, including unsupported EHCI; unseen disks could conceal
another matching GUID. No match is NOT_FOUND for complete discovery and
UNAVAILABLE for partial discovery. Selected-device failures do not fall back.
A live raw claim prevents opening that device.
`MOUNT_RIGHT_OPEN_ROOT`, `MOUNT_RIGHT_OBSERVE` and `MOUNT_RIGHT_WRITE` are independent:
requesting root mutation rights requires WRITE; requesting filesystem information
requires OBSERVE. Boot init requests observation whenever its authority holds
OBSERVE. An `optional` root skips a volume that is missing or fails to mount;
otherwise the space does not start.

Space inits and ordinary applications receive independently retained
directory/file grants, with no mount or raw-block authority. Boot init exiting
does not revoke those roots. Attenuated read-only grants cannot mutate. Observation
provides [identity and shared-pool capacity](../interfaces/directories.md#scoped-filesystem-information),
not usage or a writable allowance.

File and directory sync commit the whole current pool, including ordered data from
other files. `sync --disk` synchronizes all mounted pools on the configured disk;
it requires mount WRITE, which only boot init holds, and accepts no disk
selector. `sync PATH...` remains the path-based utility. Ordinary sessions can use
sync on their granted files/directories. Closing a file promises no durability;
dirty data and writeback errors survive in the mounted pool after its final handle
closes. Call sync explicitly before reporting that persistent work is complete.

## Packaged scripts

The userland repository supplies shebang scripts using `boot://init-shell.pxe`.
Only a `DISTRIBUTION=full` archive carries them as `boot://` files; Install
copies `init-installed` and `init-services` to `system://config/` from
`boot://share/installer/system/`:

- `init/development.pxs`, installed as `boot://init`, receives the optional host
  export read-write from `live.lua` and hands off with
  `session boot://session.pxe --configure-network --start-services boot://init-services`.
- `init/readonly.pxs`, installed as `boot://init-readonly`, receives the same
  optional export read-only and hands off with
  `session boot://session.pxe --start-services boot://init-services`, leaving
  network settings alone.
- `init/services.pxs`, installed as `boot://init-services` and copied to
  `system://config/init-services`, publishes the HTTP
  provider using the configured session environment, then starts a separate
  optional HTTPS provider with read-only trust grants and hands off to the
  interactive shell. A reported HTTPS setup failure leaves HTTPS unpublished
  and permits that handoff. It runs only when session names it with
  `--start-services`.
- `init/remote.pxs`, installed as `boot://init-remote`, receives optional HOST
  read-write and hands off through
  `session boot://session.pxe --start-remote-services boot://init-remote-services`.
- `init/installed.pxs`, which Install copies to
  `system://config/init-installed`, receives `system://` read-write from the
  pool's `boot.lua` and hands off like the development script, with
  `system://config/init-services`.
- `init/remote-services.pxs`, installed as `boot://init-remote-services`, starts
  HTTP/optional HTTPS with the configured environment and hands off through
  `session boot://session.pxe --remote-server 2323`. The trusted launcher waits
  for network assignment and delegates one listener to the
  [remote terminal server](remote-terminal.md).

The development profile delegates host write grants for regular-file creation,
writes, resize and `mkdir`, `rm`, `rmdir` and `mv`; the read-only profile delegates
only host read grants. Both still start in writable, shared RAM `tmp://` and
must explicitly address or enter `host://` to use the export. A host READ grant
can load a native executable with the existing launcher authority. An absent
or failed export leaves the optional `host://` root unbound. The host daemon's `--readonly` and host file permissions
independently restrict mutations; a guest read-write grant only authorizes
attempts. See the [host setup and walkthrough](../devices/virtio-fs.md#start-the-host-service).

The session launcher applies per-space [terminal/environment configuration](session-configuration.md)
and starts the interactive shell. Only the network owner, Development in
`live.lua` and `pyxis` in the pool's `boot.lua`, can apply the declared interface
settings and maintain DHCP.
Boot does not order init execution or wait for one init's setup before running
another. Other sessions may start before networking is configured. Super+Left/Right switches the active tab.

An explicitly selected init script can instead hand off with
`session boot://session.pxe --configure-network --tcp-server ADDRESS PORT`,
optionally adding `--tcp-count COUNT`. The trusted launcher creates an exact
bound listener and starts the [concurrent TCP echo consumer](../devices/tcp.md#concurrent-echo-server)
with only that listener, memory, clock and output streams. Space inits have
separate TCP LISTEN authority; ordinary session startup delegates only CONNECT.
This opt-in handoff replaces that init's shell and does not expose a remote shell.

Trusted setup mints stock TCP, UDP and echo factories for exactly `lo` plus the
boot profile's declared interface IDs. Copies retain that immutable set; later
definitions never enter existing grants. Route, DNS, Remote, log and debug
selections are independent, with no fallback to another interface.

The network owner's session uses a separate configuration facet and pinned UDP
BROADCAST factory for each DHCP interface. Clients, timers and reported phases
remain independent. Its maintenance process survives successor handoff and shell
exit, retaining private network ownership and maintenance resources while closing
unrelated bootstrap grants and input. Static-only setup exits once applied;
optional absent interfaces do not delay working local sessions.

Ordinary local and remote sessions receive registry READ and scoped UDP OPEN,
TCP CONNECT and echo authority. Launchers snapshot the selected DNS server and
derive `dns_udp` inside their permitted UDP scope for each new child. A missing
selected server or permitted interface supplies no resolver. Existing processes
keep their startup snapshot. Remote startup captures its selected interface facet and pinned
factories, including ingress identity for reverse-beacon connections.

Trusted init also receives a `terminal` service with CREATE authority for
[independent terminal sessions](terminal-sessions.md). Ordinary session startup
does not delegate that service or an attachment. Application terminal handles
use the same named input/output and standard-stream forwarding as framebuffer
consoles. The trusted remote startup path retains creation authority until it
launches the supervisor; ordinary remote shells receive application handles only.

## Affinity setup

Trusted init can narrow its space's CPUs within the configured ceiling before
anything else runs there:

```text
#!boot://init-shell.pxe
affinity 2-3
session boot://session.pxe --start-remote-services
```

`affinity LIST` takes comma-separated boot CPU indices and inclusive `A-B`
ranges. The `space` grant that each space init receives carries
`SPACE_RIGHT_SET_AFFINITY`. The `session` handoff forwards
only the title right, so sessions, shells and commands cannot change affinity.

The setup window closes permanently at the space's first launch request, whether
by init or anyone else, and whatever its outcome. Creating the space and
starting its init does not count.
Until then, repeated requests each replace the set. Children then inherit the
narrowed set. A request fails without changing anything in these cases:

| Case | Status |
| --- | --- |
| Empty set, or a CPU this boot does not have | BAD_REQUEST |
| A CPU outside the ceiling, or no grant | DENIED |
| After the first launch | ENDPOINT_CLOSED |

If the caller's own CPU is excluded, it moves to an allowed CPU before it
returns to userspace. A failed `affinity` stops an init script, like any other
failed command.

## Space titles

`title "Development"` sets the caller's tab label. A space's initial title
comes from its configuration entry, so the packaged scripts no longer set one.
Every space init receives the title grant. `title --optional` ignores only a
missing capability; malformed text and operation failures still stop a script.

The initial `space` resource grants `SPACE_RIGHT_SET_TITLE` and, for
[affinity setup](#affinity-setup), `SPACE_RIGHT_SET_AFFINITY`. It is bound to
that init's space, and the kernel also requires the caller to belong to the
same space. Boot init receives no title resource for Caelum. The shell's `session`
handoff passes the title right to the session launcher, which passes it to the
interactive shell. Ordinary foreground/background commands receive no title
grant. An explicit native launcher can delegate it within its space using the
existing capability machinery.

The [space protocol](../../include/abi/space.h) accepts 1–63 printable ASCII bytes,
including spaces, and returns no payload. Invalid requests leave the previous
title unchanged. Libpyxis exposes `space_set_title(handle, text)` through
`<space.h>`. The space owns the copied text: process exit and closing the last
handle do not reset it. Titles may repeat; they are display labels, not names
for lookup, identity or authority. Longer labels are clipped visually to the
existing tab width, without altering the stored text. The presenter snapshots
the title under a short lock before drawing; updates do not allocate.

## Space creation

The `space_factory` resource has one right, CREATE, and only boot init holds it;
it is never forwarded. The [space protocol](../../include/abi/space.h) appends a
space with a name, a title and either a launch request or an unstarted reason:

- **Launch.** The CPU set becomes the space's ceiling and must name only boot
  CPUs. The request is an ordinary launch request with no streams. The kernel
  adds the space's console as `input`, `output` and the three streams, its
  `keyboard`, `pointer`, `display` and `space` grants, and rejects a request
  that names those resources itself. The reply is a WAIT process handle; the
  first process is in no execution group.
- **Unstarted.** The space gets no CPUs and its tab shows
  `space NAME not started: REASON`.

Names are unique and identify spaces in logs. A malformed request or a taken
name creates nothing; a failure while preparing the first process leaves the
space unstarted, showing the status. Libpyxis exposes `space_create_started()`
and `space_create_unstarted()` through `<space.h>`, and `program_create_space()`
through `<launcher.h>` for script inits. Spaces are never destroyed.

## Space bar

The presenter draws one tab per space in registry order, Caelum first, between a
`<` and a `>` slot that are always reserved. All tabs have equal width:

- Each tab is at least as wide as the widest title among all spaces, plus one
  character cell of margin on each side.
- When every tab fits at that width, the tabs share the bar equally.
- Otherwise the bar shows as many whole tabs as fit and stretches them to fill
  the space between the chevrons.

Titles are centered, and the selected tab's title is underlined. A title wider
than the whole viewport, which needs a framebuffer under about 550 pixels wide,
is clipped with a three-dot marker. A chevron is light when spaces are hidden
beyond that edge and muted when that end of the list is visible. Chevrons do not
navigate.

After a graphics session's first SUBMIT, its tab's existing right margin cell
shows `+` when graphics is chosen and `−` when its terminal is chosen. Acquisition
without a SUBMIT and spaces without a session have no marker. The cell stays
reserved, preserving tab widths, title centering, clipping and underlining. Even
when the title cannot fit, the marker is drawn if its whole cell fits; otherwise
it is omitted. An inactive tab records its saved layer choice.

On a machine with a battery, a fixed box just inside the `>` slot shows the
charge, and the tabs share the remaining width. It always holds three
characters: `100` when full, `10%` to `99%`, and `00%` to `09%`. Its background
follows the charge along a gradient from `#a00` at 0% through `#730` at 25% to
`#690` at 100%. It refreshes with the [ACPI](../kernel/acpi.md#embedded-controller-and-battery)
reading every five seconds and is hidden without a battery.

Super+Left/Right moves the selection and stops at both ends. Moving right
scrolls so that the selected space and the next one are visible. Moving left is
symmetric. At the end of the list, the selection may sit in the edge slot. The
selection stays visible when only one tab fits, and when a title change alters
how many tabs fit. Layout follows the current titles on every frame, so renaming
a space can change every tab's width.

Super+Down shows the selected space's terminal; Super+Up restores its graphics.
The shortcuts follow [space navigation's modifier and held-arrow rules](../devices/keyboard.md).
They are consumed without changing state before a session's first SUBMIT.
Further SUBMIT calls, REPLACE, resize and space switches preserve the choice.
Hidden and unselected programs keep running; only the chosen surface of the
selected space is copied. See [graphics layers](../interfaces/graphics.md#choosing-the-visible-layer).

## Startup grants and lifetime

Boot init and space inits must name exact `boot://` archive entries. A native
init receives that URI as `argv[0]`. A script interpreter receives its own URI
as `argv[0]`, the init URI as `argv[1]`, and a READ resource named `script`.
Interpreter lookup stays inside the boot archive and does not recursively
interpret scripts. LF/CRLF and bounds follow the [script-launch contract](script-launch.md).

Each space init receives its space's title, terminal, display, keyboard and
pointer grants from the kernel, and from boot init private memory, launch,
clock, randomness, registry READ and scoped networking services, private network
policy/configuration/DHCP authority only for the network owner, power authority
when the space sets `power`, caller-scoped [memory profiling](../development/allocation-profiling.md), explicit
[endpoint creation](../interfaces/endpoints.md) through the `service` resource,
read-only boot and writable tmp roots, its configured roots, the working
directory its start selects and the initial environment. Space configuration
chooses which trusted init runs, its roots and whether to supply `child_launcher`;
it is not an authority ceiling. A space name or CPU does not grant workload
authority.

Stock boot-init/session/mux/Remote handoffs retain held shared-memory creation
authority as a private launch source, without another space setting. Shell and
Lua bundles explicitly request the optional private `memory_create_shared`
binding with `memory/manage` plus `memory/create_shared`; their required ordinary
`memory` binding stays MANAGE-only. Exact held interpreter continuations preserve
the source under the existing inventory rules. Ordinary native child profiles
do not add creation; bundles receive it only through an explicit declaration,
and required declarations refuse when the source is absent. See
[bundle declarations](program-bundles.md) for attenuation and optional omission.

Mount authority stays with boot init; the selected directory binding list travels
through session, service and remote-server handoff and ordinary child launch.
The list contains at most 16 roots, including boot, tmp, bin and HOST. Each launch queries
and copies the selected grants' actual rights and transport masks; explicit
read-only attenuation also applies to the working-directory chain. A restricted
launcher can select fewer roots or rights. It does not recover missing authority
from a URI label or the capability table. Root/working-directory names and other
startup data still share the 64 KiB capture bound. Network-configuration authority
reaches the session launcher but not its interactive shell. Ordinary foreground
commands receive LAUNCH only when the shell holds `child_launcher`; `session`
preserves that separate resource as well as the shell's own launcher. The
kernel enforces capability rights, while
the trusted programs choose what to delegate. Neither URI names nor selecting
a different script creates authority beyond the supplied grants. A directly
invoked exact-shell script keeps the shell's available sources, including
authority for privileged builtins; it is not an attenuation boundary. To run a
reduced shell, use an explicit native grant list or declaring bundle with the
desired sources. Nested scripts keep that reduced context and cannot recover
omitted grants. A rootless shell's held image selects interpreter code and
confers no trusted root or new ownership role.

Failure to load boot init stops the boot. A space init that cannot be opened
or launched leaves its space unstarted, with the reason on its tab. Boot init
prints the full reason as `boot-init: space NAME not started: REASON`; the tab
holds at most its first 96 bytes, with bytes outside printable ASCII shown as
`?`. An unstarted space carries none of its configured terminal, clipboard or
screenshot flags. Boot init sizes each space's launch tables from its
definition. It refuses a space whose count overflows (`launch table size
overflows`) or exceeds what one launch request can carry (`launch needs N grants
and M entries; at most G and E fit`). If an addition ever finds its table full,
the space is refused with `launch tables exceed their counted size` instead of
writing past the table. A running
script stops on its first failed command; EOF exits without opening a prompt.
Inits and sessions are not restarted. Their space, terminal contents and shared
filesystem trees survive exit. A space whose init is `boot://init-shell.pxe` opens a
shell directly, as the `rescue` space does. See [later lifecycle work](../wip/later-os-directions.md#execution-lifecycle)
for deferred supervision and process replacement.

LAUNCH on the ordinary launcher includes caller-scoped group creation. A
supervisor can create an [execution group](../interfaces/execution-groups.md) and
use its bound launcher while remaining outside that group. The trusted shell
`session` handoff preserves the actual launcher LAUNCH rights and
terminal CREATE grant; the remote supervisor then delegates only a group-bound
LAUNCH grant to each shell.
With Remote `launch = true`, the supervisor also supplies
`child_launcher` from that same group-bound launcher. Foreground commands and
their launched descendants then remain in the remote execution group. The
packaged live Remote entry enables this (owner accepted, 2026-10-11), allowing
bundles such as Neovim to launch helpers. Installed defaults contain no Remote
space; owner-added Remote entries use the same setting. Only LAUNCH is delegated,
not group supervision.

## FAT32 volumes and selected grants

Two opt-in kinds extend the existing volume table. `boot-esp` has no selector and
is always RO through the kernel's fixed boot-provenance issuer; optical/PXE boots
without provenance do not guess a disk. A `fat32` volume requires local
`disk_guid`, one-based `partition` and fixed `access = "read-only" | "read-write"`.
Duplicate matching GUIDs refuse persistent selection; conflicting modes for two
names of the same selector reject the plan before admission. All ordinary FAT
roots must request RO, even when the instance is RW.

```lua
volumes = {
  esp = { kind = "boot-esp" },
  fat32 = { kind = "fat32", disk_guid = "<DISK_GUID>",
            partition = <GPT_ENTRY>, access = "read-write" },
}
```

Replace placeholders only in local configuration. Disk GUIDs and serials never
belong in repository documents/examples. No shipped configuration opts in.

A space's optional `fat32_mount = "read-only" | "read-write"` explicitly delegates
a mount-only factory as `source.fat_mounts`; no raw OPEN/READ/WRITE/CLAIM authority
comes with it. RW factory holders may select any supported partition. Programs
receiving only selected data do not receive that factory.

A space's optional `package_upgrade = true` delegates the boot-origin ESP
issuer with only OBSERVE and UPGRADE_BOOT_ESP as `source.boot_esp_upgrade`, and
boot init's READ-only boot evidence as `source.boot_esp_evidence`, for
[`pkg upgrade`](../wip/pkg-upgrade.md). The evidence is
`PYXIS-BOOT-EVIDENCE 1` followed by `kernel`, `archive` and `record` lines of size
and SHA-256: the kernel and archive this boot loaded, hashed from the kernel-issued
FILEs, and the committed boot record (`EFI/pyxis/config/config.record`) the early
reader read. A final `pool N` line names the boot entry's `mount.pool`, which
`pkg upgrade` writes into the new entry. The evidence exists only
when that record was read and hashed on an entry naming its pool, never on
rescue or live boots, and records
what was loaded, not which configuration was selected. Without it neither source
is delegated. The installed `pyxis` space opts in
([decision 74](../wip/pkg-upgrade.md#decided)); only the `pkg` bundle declares
the two sources.

Separately named `grants` supply selected sources outside ambient URI roots:

```lua
grants = {
  fat32_write = { volume = "fat32", kind = "directory", access = "read-write" },
  fat32_control = { volume = "fat32", kind = "mount", access = "read-write" },
}
```

Kinds are `directory` and `mount`; access is explicit and `optional` defaults to
false. The installed `pyxis` space selects the `packages` volume this way as an
optional read-only `packages` source, and the `pkg` and `sdk` roots name the
package views instead of a volume (`pkg = { view = "pkg" }`,
`sdk = { view = "sdk" }`); see
[reaching package files](../interfaces/packages.md#reaching-package-files). DIRECTORY sources get exact RO/content rights. MOUNT sources get
OPEN_ROOT/OBSERVE/RETIRE, with WRITE/INTERVAL only for RW. Sources referring to
RO admission cannot request RW; mount sources require a FAT volume. Sources-only
volumes are admitted too. Up to eight named sources, including the factory opt-in,
are permitted per space. Default startup still has no selected sources.

Actual startup names are `source.NAME`. Only trusted local session/mux/shell
handoff preserves these typed resources; remote-service startup omits them.
Bundle launch resolves explicitly declared sources and reports actual received
grants. It never adds source directories to inherited roots automatically.
Boot init drops its own data references after handoff; published controls retain
the accepted mount lifetime. A successful mount not handed to a
space attempts explicit retirement after local data drains and reports refused
cleanup; failed admission publishes no pair and rolls back in the kernel.

## Declared private HTTP staging

Ordinary profiles explicitly select optional `http_staging` DIRECTORY authority
from the private `http-work` RAM volume. Boot init creates a per-space leaf;
trusted service startup creates separate HTTP/HTTPS leaves and supplies only each
provider's declared `staging` request. The bootstrap keeps its ancestor through
service initialization and consumes it before local/mux/remote client handoff.
It never becomes a URI root, cwd or interactive selected source.

The default RAM body cap is 16 MiB, configurable with trusted provider startup
`--staging-cap BYTES`. Large uploads opt into an explicitly provisioned NPFS
volume through the same selected-source schema. Boot validation rejects any
public/alternate alias of that backing, including equivalent NPFS volume tuples
and the installed program volume. Runtime backing unavailability never switches to RAM, repartitions or creates a
volume on update. An invalid boot configuration starts only the rescue space,
which declares no staging. Missing optional backing keeps GET and refuses POST;
required source declarations still refuse startup. See
[prepared HTTP authority](../interfaces/http-transactions.md).

### Space-root startup failures

After launching all configured roots (or rescue), boot-init releases its setup
views and authority publications, then retains their WAIT-only process observers.
Each returned WAIT|TERMINATE handle is copied down to WAIT and the original
closed; failure to retain an observer is reported without stopping the root.
A runtime START_FAILED result prints the startup step and native cause on the
Caelum tab. Observation is nonblocking across roots, so a healthy long-running
root cannot hide another root's failure. It neither restarts roots nor creates
another rescue space. Boot-init remains resident while root observers remain;
ordinary application completion policy is unchanged.

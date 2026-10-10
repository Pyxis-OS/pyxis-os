# Init and session startup

The kernel starts one trusted **boot init** from the boot archive, in Caelum's
log space. Boot init reads the boot configuration, mounts each configured
volume once, creates the configured spaces in order and starts each space's
init, then exits. Each space gets its own tab, terminal, display and input and
runs one trusted init. The number of spaces does not depend on the CPU count.
Children stay in their parent's space. This introduces no migration,
supervision or global PID 1.

Space inits receive an explicit namespace-creation service. The packaged active
init scripts create a [service namespace](../interfaces/namespaces.md) before session handoff;
it is delegated independently of space membership.

Space inits share the read-only `boot://` archive and writable RAM-backed
`tmp://` tree. It is not private per space and disappears on reboot. Spaces
normally start in `home://`: the pool's `home` volume on installed systems and
a RAM volume on live boots. Raw
installer resources are issued only on the separate install path. Init scripts
are trusted setup policy; the session handoff delegates resources to ordinary
applications.

## Boot command line

The kernel accepts the following options, each at most once, in a command line
of at most 4095 bytes without quoting or escaping:

| Option | Meaning |
| --- | --- |
| `init=boot://IMAGE` | Required. The boot init, a native PXE or shebang archive entry. |
| `mount.disk=GUID` | The disk native mounts may use; boot init then reads the installed configuration. |
| `boot.install=1` | The install entry: boot init also receives the raw installer resources. |
| `boot.default_config=1` | The rescue entry: boot init ignores the pool override. |
| `remote.beacon=NAME` | The configured `remote` space discovers a host beacon and connects out. |
| `log.udp=1` | Enable the [UDP kernel log](../interfaces/kernel-log.md#udp-capture) after networking initialization. |
| `display.size=WIDTHxHEIGHT` | Set an exact initial Bochs pixel size; see [QEMU display selection](../development/qemu.md#display-device). |

Anything else, a repeated option or a non-`boot://` init stops the boot.
Normal live and installed boots use `init=boot://boot-init.pxe`.
Options are parsed once before display and AP initialization into retained kernel
storage; boot init consumes the same result later. `display.size` is an exception
to fatal value validation: malformed/unsupported geometry keeps the firmware
framebuffer with a message. It applies to the selected Bochs device, not VirtIO
or an unsupported native GPU. `DISPLAY_SIZE` supplies it during image assembly,
including rescue and installer entries; an empty setting omits it.

Boot init runs on CPU 0 in Caelum's space. Its messages go to the Caelum tab and
to serial. It receives console output but no input, the bootstrap services, the
`boot://` and `tmp://` roots, the `space_factory` resource, a private RAM
directory as the `ram` resource, the HOST and native mount authority when
present, and, on the install entry, the raw installer resources. Arguments add `--installed` when a disk is bound and
`--default-config` for the rescue entry.
`remote.beacon` adds `--remote-beacon NAME`. The name is 1–63 printable ASCII
bytes without spaces, compared case-sensitively. Boot init supplies it only to
the configured space named `remote`, together with separate BROADCAST-only
`udp_beacons` authority. The trusted session bootstrap passes both to the
remote daemon; its shells receive neither. The option creates no space when
the boot configuration lacks `remote`. See [reverse connections](remote-terminal.md#reverse-connections).

## Boot configuration

Boot init reads `boot://config/live.lua`, or `boot://config/installed.lua` when
`mount.disk` is bound. Each archive file returns a `hostname` default, named `volumes` and a list of `spaces`:

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
      network = true, launch = true, screenshot = true, cpus = { 1, 2, 3 },
      roots = { host = { access = "read-write", optional = true },
                system = "read-only", home = "read-write" } },
    { name = "scratch", title = "Scratch", init = "boot://init-readonly",
      roots = { scratch = "read-write" }, start = "scratch" },
  },
}
```

- **Volumes.** An `npfs` volume names a one-based GPT partition on the bound
  disk and a volume name. A `virtio-fs` volume is the HOST export. A `ram`
  volume is an empty directory in boot init's RAM directory, named after the
  volume; spaces that name it share it until reboot. Names use 1–31 characters
  from `a-z`, `0-9` and `-`, other than `boot`, `tmp` and `bin`, and become the
  root's scheme.
- **Spaces.** `name` follows the same rules, other than `rescue` and `caelum`,
  and identifies the space in logs; it grants nothing. `title` defaults to the
  name. `init` must name a `boot://` entry. `cpus` lists boot CPU indices, the
  space's ceiling; without it the space may use every CPU. Tab order follows
  the list.
- **Roots.** Each root names a volume and its access, `read-only` or
  `read-write`, or a table that can also mark it `optional`. A volume is
  mounted once, read-write if any space needs that, and each space receives only
  the access it asks for.
- **Start.** A space's working directory is `home://`. `start` names another of
  its roots, such as `tmp`, `boot`, `bin` or a configured one; naming a root
  the space does not have is an error. A space without the root it would start
  in, such as one with no `home` root or a missing optional root, starts in
  `tmp://` and boot init says so.
- **Network owner.** At most one space sets `network = true`. Only its init
  receives `net_config` WRITE and UDP broadcast authority, so only it can run
  `session --configure-network`; the others get READ and ordinary UDP. A
  configuration without an owner is valid and leaves the network unconfigured.
- **Ordinary child launch.** `launch` is an optional boolean, defaulting to
  `false`. With `launch = true`, boot init supplies a separate `child_launcher`
  resource with LAUNCH alone. Trusted init and session handoff preserve it;
  the shell delegates it as `launcher` to ordinary foreground commands,
  including every foreground pipeline stage. Background commands and services
  do not receive it. This does not delegate the trusted launcher's CREATE_GROUP
  authority or add terminal input rights.
- **Multiplexer.** `multiplexer` is an optional boolean, defaulting to false.
  It supplies a distinct trusted `mux_terminal` grant. After configuration and
  service setup, the session launcher selects `bin://mux.pxe`, giving it
  terminal CREATE and launcher CREATE_GROUP. It does not grant either resource
  to ordinary shell commands. See the [multiplexer guide](multiplexer.md).
- **Clipboard.** `clipboard_local` and `clipboard_shared` independently request
  the [store grants](../interfaces/clipboard.md). Stock local interactive spaces
  set both true; remote startup receives neither. If omitted, each setting
  retains the existing `multiplexer` default; explicit false withholds that layer.
  Local session/shell and mux pane handoffs forward each on foreground startup;
  background commands and services receive neither. Grants alone authorize no
  read: graphics calls require display/keyboard ownership and physical activation.
- **Power.** A space that sets `power = true` receives the kernel's `power`
  resource, with power-off and restart rights, for the shell's
  [`poweroff` and `reboot`](shell.md#power-off-and-restart). Any number of spaces
  may set it. The live Development space and the installed `pyxis` space do, and
  boot init's built-in rescue space always has it. Read-only does not.
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
  restriction. Live Development and Remote and installed `pyxis` opt in;
  Read-only and the built-in rescue space do not. If an opted-in space's boot
  init lacks the resource, that space does not start and reports the reason.
  See [screen capture](../interfaces/screen-capture.md) and the
  [PNG command](screenshot.md).

The scheduler places each task on the least-loaded CPU its space allows and
balances tasks between CPUs; see [placement and migration](../kernel/smp.md#placement-and-migration).
CPU 0, the BSP, runs userspace too, but placement ties prefer the other CPUs,
because it also runs the kernel workers and boot init. A new space's init has
no placement preference. Boot init creates spaces one at a time, and an earlier
init is usually blocked by then, so on four CPUs all three live inits start on
CPU 1; balancing later moves runnable tasks to idle CPUs.

Every space also receives `bin://`, read-only. On installed boots it is
`bin/REVISION` on the system pool, for the running kernel's revision; the
[installer](installer.md) writes it. On live boots it is the archive itself,
which holds every program. When that directory is missing on an installed boot
it is also the archive, but an installed archive holds only the rescue set.
Boot init reports which. The shell resolves bare names through `bin://`, then
`boot://`.

Space inits receive the bootstrap services, `boot://`, `tmp://`, `bin://`,
their configured roots, the working directory their start selects and `OS_NAME`, plus their space's own
console, keyboard, pointer, display and space handle from the kernel. They do
not receive mount authority or the space factory, so `mount` and `sync --disk`
are unavailable to them; `sync PATH...` still works on their roots.

A space whose required volume is missing or fails to mount, whose CPU set names
an absent CPU, or whose init cannot be opened is created but not started. Its
tab and the log say why, and the other spaces start. If no space starts, boot
init creates a `rescue` space running `boot://shell.pxe` with only `boot://`,
`tmp://` and `bin://`, starting in `tmp://`; it cannot repair the pool.

The archive `hostname` defaults to `pyxis` and follows the
[machine-settings label schema](machine-settings.md#hostname-schema). Installed
boots select the persistent key after mounting system, including rescue boots;
`boot.lua` cannot override it. Boot init sets the kernel name once before creating
spaces and forwards only system-information READ authority. Live image builds
may override the archive value with `make image HOSTNAME=NAME`.

### Pool override

Installed boots also read `system://config/boot.lua` from the default `system`
volume. It accepts only `volumes` and `spaces`; `hostname` belongs to the archive
default/store and is rejected here. An entry with a default's name replaces it
whole, new names follow the defaults, and nothing can be removed. It cannot
redefine the `system` volume. Roots may name volumes from either file. A
missing override is reported and the default boots. An invalid override (a Lua
error, an unknown key, a bad value, an undefined volume, a start outside the
space's roots or a second network owner) is ignored whole, with the reason on the Caelum tab and serial.

The override can replace a default space with an unusable one. The installed
disk's second boot entry, **Pyxis OS (rescue)**, passes
`boot.default_config=1` and boots the archive default; it restores the
configuration, not the pool. Write access to `system://config/boot.lua`
chooses which inits run with forwarded grants on the next boot, so treat it as
administrative.

### Development builds

Edit `userspace/config/live.lua`, or `installed.lua` for builds with
`MOUNT_DISK`, to change spaces, CPU sets and roots. `INIT` names one host file,
relative to the repository root or absolute, staged as `boot://init`, the
Development space's init. It can be a native PXE executable or a shebang script:

```sh
make run CPUS=4
make run INIT=/tmp/init.sh
make image INIT=build/userspace/shell.pxe
make image                         # restore the packaged init
```

Additional custom archive entries belong in the
[boot assembly manifest](../development/boot-archive.md). Kernel-only builds do
not select or package init.

## Install boot selection

The build setting `BOOT_MENU_TIMEOUT` defaults to `0`, booting normally without
a menu delay. Set `BOOT_MENU_TIMEOUT=5` when building install media to allow menu
selection. The `Install Pyxis` entry is generated for either timeout. Its
command line is `init=boot://init-install.pxe boot.install=1`, and only that
boot init receives `disks`, `boot_kernel` and `boot_archive`.

`init-install` is a boot init. It creates the `install` space with
`boot://installer.pxe` as its first process, forwards its bounded installer
resources and the read-only `boot://` root, drops the space factory and waits,
then reports the installer's result on the Caelum tab. The installer implements
the [interactive installation flow](installer.md). See
[installer authority](../devices/installer-authority.md) for the source-file,
raw-claim and handoff contracts. These resources do not enter ordinary session
or child launch automatically.

The installed configuration has two entries and a three-second menu:
`init=boot://boot-init.pxe mount.disk=<GUID>`, and the rescue entry, which adds
`boot.default_config=1`. The archive's installed configuration starts the
`pyxis` space with `boot://init-installed`, `system://` and `home://`
read-write, starting in `home://`, and network ownership. `home` is optional:
if the volume cannot be mounted, `pyxis` still starts, in `tmp://`, in both
the normal and the rescue entry. `system` is required. That init starts the
ordinary local session and configures networking. `tmp://` remains RAM-backed.
The packaged live Development and installed `pyxis` entries set `launch = true`;
the live Read-only and Remote entries leave it disabled.
Live boots give the Development and Read-only spaces the RAM `home://`,
read-write and read-only, and the Remote space read-write, starting in
`tmp://`. Installed disks omit the installer
entry; enter install mode through live media.

## Native disk configuration and mounting

`MOUNT_DISK=<canonical-GPT-GUID>` selects the deployment disk and generates
`mount.disk=<GPT-GUID>` in Limine configuration, plus the rescue entry.
Omission disables native mount authority. Malformed, zero or duplicate
configuration fails setup; kernel parsing also rejects malformed or duplicate
options outside the Make generator. The GUID selects a disk but authenticates
neither it nor its contents. The filesystem has no on-disk principal or
permission policy.

Prepare a populated GPT image using the
[native adapter instructions](../devices/filesystem-native-adapter.md#prepare-a-disposable-disk),
describe its volume in `userspace/config/installed.lua`, then attach it with
configuration matching that image:

```lua
volumes = { data = { kind = "npfs", partition = 1, volume = "system" } },
spaces = {
  { name = "native", init = "boot://init", network = true,
    roots = { data = "read-write" } },
},
```

```sh
make run CPUS=4 INIT=/tmp/init-native.sh \
  VIRTIO_BLK_IMAGE=/absolute/path/to/development.raw \
  MOUNT_DISK=01234567-89ab-cdef-0123-456789abcdef
```

The GUID is illustrative, not a default. Do not change an attached image from the
host. The space's init receives `data://` and can hand it to a session:

```sh
#!boot://shell.pxe
session boot://session.pxe --start-services
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

The userland repository supplies shebang scripts using `boot://shell.pxe`:

- `init/development.sh`, installed as `boot://init`, receives the optional host
  export read-write from `live.lua` and hands off with
  `session boot://session.pxe --configure-network --start-services`.
- `init/readonly.sh`, installed as `boot://init-readonly`, receives the same
  optional export read-only and hands off with
  `session boot://session.pxe --start-services`, leaving network settings alone.
- `init/services.sh`, installed as `boot://init-services`, publishes the HTTP
  provider using the configured session environment, then starts a separate
  optional HTTPS provider with read-only trust grants and hands off to the
  interactive shell. A reported HTTPS setup failure leaves HTTPS unpublished
  and permits that handoff. It runs only when session selects `--start-services`.
- `init/remote.sh`, installed as `boot://init-remote`, receives optional HOST
  read-write, creates the service namespace and hands off through
  `session boot://session.pxe --start-remote-services`.
- `init/installed.sh`, installed as `boot://init-installed`, receives
  `system://` read-write from `installed.lua` and hands off like the
  development script.
- `init/remote-services.sh`, installed as `boot://init-remote-services`, starts
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
`live.lua` and `pyxis` in `installed.lua`, can request global network setup.
Boot does not order init execution or wait for one init's setup before running
another. Other sessions may start before networking is configured. Super+Left/Right switches the active tab.

An explicitly selected init script can instead hand off with
`session boot://session.pxe --configure-network --tcp-server ADDRESS PORT`,
optionally adding `--tcp-count COUNT`. The trusted launcher creates an exact
bound listener and starts the [concurrent TCP echo consumer](../devices/tcp.md#concurrent-echo-server)
with only that listener, memory, clock and output streams. Space inits have
separate TCP LISTEN authority; ordinary session startup delegates only CONNECT.
This opt-in handoff replaces that init's shell and does not expose a remote shell.

Trusted network setup uses UDP BROADCAST authority, which only the network
owner's init receives, for [DHCP](../devices/dhcp.md).
Its setup session remains alive after successor handoff and shell exit to maintain
leases or continue link selection/discovery after an initial offline timeout.
Pending DHCP selection retains UDP creation authority until binding and opening
the endpoint, then closes it. Pending static selection exits after applying settings.
It closes unrelated bootstrap grants and input, retaining only maintenance
and diagnostic authority.
Ordinary local and remote sessions receive UDP OPEN and NET_CONFIG READ only;
launchers read chosen DNS for new child environments without configuring net0.
The temporary manual broadcast echo handoff has been removed.

Trusted init also receives a `terminal` service with CREATE authority for
[independent terminal sessions](terminal-sessions.md). Ordinary session startup
does not delegate that service or an attachment. Application terminal handles
use the same named input/output and standard-stream forwarding as framebuffer
consoles. The trusted remote startup path retains creation authority until it
launches the supervisor; ordinary remote shells receive application handles only.

## Affinity setup

Trusted init can narrow its space's CPUs within the configured ceiling before
anything else runs there:

```sh
#!boot://shell.pxe
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
clock, randomness, networking services, network configuration (WRITE only for
the network owner), power authority when the space sets `power`, caller-scoped [memory profiling](../development/allocation-profiling.md), explicit
[endpoint creation](../interfaces/endpoints.md) through the `service` resource,
read-only boot and writable tmp roots, its configured roots, the working
directory its start selects and the initial environment. Space configuration
chooses which trusted init runs, its roots and whether to supply `child_launcher`;
it is not an authority ceiling. A space name or CPU does not grant workload
authority.

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
a different script creates authority beyond the supplied grants.

Failure to load boot init stops the boot. A space init that cannot be opened
or launched leaves its space unstarted, with the reason on its tab. A running
script stops on its first failed command; EOF exits without opening a prompt.
Inits and sessions are not restarted. Their space, terminal contents and shared
namespace roots survive exit. A space whose init is `boot://shell.pxe` opens a
shell directly, as the `rescue` space does. See [later lifecycle work](../wip/later-os-directions.md#execution-lifecycle)
for deferred supervision and process replacement.

Trusted init also holds CREATE_GROUP on its ordinary launcher. The default session
program attenuates this to LAUNCH when starting the shell. An explicitly authorized
supervisor can create an [execution group](../interfaces/execution-groups.md) and
use its bound launcher while remaining outside that group. The trusted shell
`session` handoff preserves the actual launcher LAUNCH/CREATE_GROUP rights and
terminal CREATE grant; the remote supervisor then delegates only a group-bound
LAUNCH grant to each shell.
If Remote explicitly opts into `launch = true`, the supervisor also supplies
`child_launcher` from that same group-bound launcher. Foreground commands and
their launched descendants then remain in the remote execution group. The
packaged Remote entry leaves this opt-in disabled.

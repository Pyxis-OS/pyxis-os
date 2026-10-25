# Release history

Pyxis versions have three numbers and are annotated tags on `main`. The
tag message is the authoritative record of a release; this page keeps the
history readable in one place. Add an entry here whenever a version is tagged.

From 0.0.6 on, each release candidate is checked in QEMU first: install the previous release, keep data
in `home://` and a `system://config/boot.lua` override, update, and boot the
result. Launch `nvim` there and quit with `:q`; it exercises child spawn with
extra resources, which the 0.0.6 check missed. Before the owner's native update, pack
the release-tied packages (the game packages and `clang-hosted`) from the tagged
checkout into horse's trees and prune other revisions, following
[packing a revision](../tools/package-repository/README.md#packing-a-revision); a
package built at another revision refuses to install or launch on the release.
A build deployed to the owner's stick between releases is packed the same way,
from that build's checkout, before the owner updates. The owner's stick then gets the update natively. The stick usually
carries a personal build with opt-in programs such as
[DevilutionX](userland/devilutionx.md); releases describe the shareable build.

## 0.0.8 (2026-10-23)

The tag is on the merge commit of the PR that adds this entry, which also adds
`"0.0.8"` to `boot/os-release.lua`. 0.0.8 is the big release: the system is
rebuilt around packages, signed repositories and an in-place updater. It is the
last release that needs a reinstall; later releases arrive through
`pkg upgrade`.

- **The core archive and packages (step 2):** every program except the core
  archive is a package. Packages are read as `pkg://ID`, the C SDK and library
  development packages are merged into `sdk://`, `bin://` is the installed
  packages' command view, and Install fetches the base set online.
- **Signed repositories and the updater (step 3):**
  - indexes signed by the Pyxis Software Signing CA, with rollback refusal;
  - `pkg upgrade` installs the next release's kernel and core archive as the
    new boot entry and keeps the running one as `Pyxis OS (previous)`;
  - packages and releases are served from `https://packages.pyxis.nexus/`.
- **Release ISO:** the install ISO is published at
  `https://packages.pyxis.nexus/images/pyxis-0.0.8.iso` beside a signed
  `SHA256SUMS`
  ([checking an ISO](../packages/signing/README.md#checking-an-install-iso)).
- **Install experience:** the install ISO boots Install by default, with rescue
  second and a five-second menu. The installer opens on its own tab. A blank or
  fully marked disk installs on the default **Blank or marked disks** path;
  anything else needs **Any disk (may erase data)**. Install is Ethernet-only:
  use wired or dock Ethernet.
- **Go:** native TCP, UDP, DNS and HTTP with TLS, standard-library adapters, and
  Caddy as an optional package with explicit listener authority.
- **Terminal UI:** raw terminal mode for C and Go, ncursesw on the native
  terminal contract, and hexcurse.
- **Graphics and games:** raylib with SDL-backed audio, `raylib-dev` and
  `sdl2-dev` through `sdk://`, and Minesweeper as a public package.
- **Library development packages:** twelve new `-dev` packages beside
  `sdl2-dev`, `raylib-dev` and `ncursesw-dev`.
- **Wi-Fi:** AX200 WPA2-Personal, and RTL8922AE first steps (firmware readiness
  and scan inputs). There is no Wi-Fi Install.
- **ABI:** `include/abi/` becomes the first release contract
  ([ABI](interfaces/abi.md)).

Release notes:

- **Back up `home` and reinstall.** Earlier installs are not recognized by the
  0.0.8 installer, and 0.0.8 cannot update them.
- **Packages are read as `pkg://ID`:** a fresh install's `boot.lua` has the
  `pkg = { view = "pkg" }` root. A hand-kept `boot.lua` with an old
  `package = "ID"` root fails validation and boots the rescue space.
- **Run `pkg reindex` on old package sets:** a set made before task 2c-2 has no
  `sets/N.sdk`, and `sdk://` reports it as damaged until `pkg reindex` or the
  next `pkg install`.
- **Testers: try QEMU first.** Install from the ISO in QEMU before real
  hardware.

## 0.0.8 scope

Decided by chronium, 2026-10-23. A checklist of what 0.0.8 must contain and how it
is cut; the release entry is above. The design is in
[ABI model, core archive and updater](wip/pkg-upgrade.md) and
[the `sdk://` view](wip/sdk-view.md).

There are no release branches: a release ships whatever is on main at its tag
(chronium, 2026-10-23).

**Contents**

- [x] **Step 2 complete** (2026-10-23):
  [step 2](wip/pkg-upgrade.md#step-2-programs-out-of-the-archive-implemented)
  moved every program but the core archive into packages, with `pkg://`,
  `sdk://` and hosted Clang on it (2c), the core archive and `bin://` (3a) and
  online Install (3b). An install from before task 3 that is updated has no
  shell until it is reinstalled or repaired from Rescue
  ([repair](userland/system-updates.md#installs-from-before-the-core-archive)).
- [x] **Go task 4, native networking** ([Go on Pyxis](wip/go-runtime.md), proposal
  in pyxis-os [#1140](https://git.internal/PyxisOS/pyxis-os/pulls/1140)): the
  concurrent completions budget, dynamic transport records and DNS over UDP.
  Chronium put it in 0.0.8 scope on 2026-10-23. Merged in #1155, with Caddy in
  #1172.
- [x] **raylib as a `-dev` package** (beta): `raylib-dev`, with its dependency
  `sdl2-dev`, merged into `sdk://` so `#include <raylib.h>` and `-lraylib -lSDL2`
  work with no flags. The raylib work is in pyxis-os
  [#1107](https://git.internal/PyxisOS/pyxis-os/pulls/1107),
  [#1124](https://git.internal/PyxisOS/pyxis-os/pulls/1124) and
  [#1130](https://git.internal/PyxisOS/pyxis-os/pulls/1130), all merged.
- [x] **Library `-dev` sweep** ([library development packages](development/library-dev-packages.md),
  plan #1158, ports #153, packages #1161): twelve new `-dev` packages (`fmt-dev`,
  `libpng-dev`, `libuv-dev`, `lpeg-dev`, `lua-dev`, `lua5.1-dev`, `luv-dev`,
  `mbedtls-dev`, `picohttpparser-dev`, `tree-sitter-dev`, `utf8proc-dev` and
  `zlib-dev`) beside `sdl2-dev` and `raylib-dev`. Each carries headers, static
  archives and `.pc` files that point at `sdk://`, with dependencies by name
  (`libpng-dev` depends on `zlib-dev`). `wpa-rsn` stays private to the native
  supplicant, with no public `-dev` package
  ([qualification](development/experiments/library-dev-packages.md)).
- [x] **Raw terminal mode** ([terminal UI](wip/terminal-ui.md), gates 1 and 2):
  the native raw terminal API for C and Go, the `reset` and `clear` built-ins,
  and foreground terminal restoration. Merged on main in #1144 and #1142,
  2026-10-23. ncursesw (#1162) and hexcurse (#1175) also landed in 0.0.8.
- [x] **The updater is in 0.0.8.** [Step 3](wip/pkg-upgrade.md#step-3-the-updater):
  `pkg upgrade` fetches the kernel and the core archive from a releases
  repository, verifies them and publishes them as the new menu entry, keeping the
  previous entry. Then 0.0.8 to 0.0.9 is an Update, and 0.0.8 is the last
  release that forces a reinstall. Step 3 tasks 1–4 and the release ISO are
  merged; the native check (task 5) follows the publish.

**ABI at the tag**

- At the 0.0.8 tag, `include/abi/` becomes the first release contract
  ([ABI](interfaces/abi.md)). Later releases only add to it, unless a break is
  deliberate and listed in their notes. Until the tag, ABI stays fluid and
  changes in place.
- Work that would change existing ABI lands before the tag. Purely additive work
  does not gate the tag; Go task 4 is in scope for its own reasons (above).
- [x] Settle before tagging: the in-flight RTL8922AE status additions. They stay
  out of 0.0.8 and land in 0.0.9 as additive ABI (#1174; chronium, 2026-10-23).

**Release mechanics**

- [ ] The release-tied packages are rebuilt from the tag: `pyxis-dev`,
  `libcxx-dev`, `clang-hosted` and the game packages, following
  [packing a revision](../tools/package-repository/README.md#packing-a-revision).
- [x] The release is signed with an intermediate CA that chronium issues from his
  homelab root. The keys stay with him. The CA and the signer-2026 leaf are in
  [packages/signing](../packages/signing/README.md).
- [ ] The candidate check is a fresh install: install, boot, and launch `nvim` as
  above, with no update step. Earlier installs are not recognized by the 0.0.8
  installer ([decision 28](wip/pkg-upgrade.md#decided)).
- [x] The release notes say (see the 0.0.8 entry):
  - back up `home` and reinstall;
  - packages are read as `pkg://ID` ([task 2c-1](wip/sdk-view.md)): a fresh
    install's `boot.lua` has the `pkg = { view = "pkg" }` root, and a hand-kept
    `boot.lua` with an old `package = "ID"` root fails validation and boots the
    rescue space;
  - a set made before task 2c-2 has no `sets/N.sdk`, and `sdk://` reports it
    as damaged until `pkg reindex` or the next `pkg install`.

## 0.0.9 direction

Decided by chronium, 2026-10-24, replacing the 2026-10-23 list. 0.0.9 is a ports
and userland release, shipped around Monday 2026-10-26. It lists what is merged on
main, and what is not merged yet is marked "if merged": it ships only if it lands
first.

- **Git on go-git** (merged): `git` with clone, log, status, add, commit, diff,
  push, pull and init ([Git](userland/git.md)).
- **OpenArena and SDL2 OpenGL on Mesa softpipe** (merged): OpenArena on ioquake3
  with Mesa softpipe, and SDL2 OpenGL through OSMesa
  ([Quake 3 on Mesa llvmpipe](wip/quake3-llvmpipe.md)). OpenArena is a public
  package; the Quake 3 Arena and Team Arena data stays private. llvmpipe is not in
  0.0.9: see the next kernel push below.
- **blurgtext and fonts** (merged): [blurgtext](userland/blurgtext.md) with the real
  FreeType and HarfBuzz, and the expanded fonts package with CJK, emoji, Roboto and
  the Plex italics ([fonts](userland/fonts.md)).
- **Reusable ImGui and the SoH shared `-dev` libraries** (if merged): ImGui on
  softpipe (#1198) and the shared libraries Ship of Harkinian needs as `-dev`
  packages in the SDK view (#1199 to #1208, #1210 and #1214).
- **Readable `boot.lua`** (if merged): the installed `boot.lua` and the live
  `live.lua` expanded to one key per line, with no change in meaning (pyxis-userland
  #376, pyxis-os #1212). Existing installs keep the file Install copied.
- **Ports and userland tools:** the remaining curses work of the
  [terminal UI](wip/terminal-ui.md) track: the curses ports beyond ncursesw and
  hexcurse, which landed in 0.0.8 with raw terminal mode (decided 2026-10-23).
- **Installer:** an interactive Wi-Fi join (choose a network, enter its key), as
  a step of the curses TUI installer. 0.0.8 has no Wi-Fi Install: use wired or
  dock Ethernet ([decision 70](wip/pkg-upgrade.md#decided)).
- **Remote on installed systems** (if merged; chronium, 2026-10-24): ship the Remote startup
  with the `remote-terminal` package. On 0.0.8 the core archive has no
  `boot://init-remote` or `init-remote-services`, and the package carries only
  `remote-terminal.pxe`, so an owner adding a Remote space hand-writes two init
  scripts in `system://config`: `init-remote` (starts `textfs`, then
  `session --start-remote-services`) and `init-remote-services` (starts the HTTP
  providers, then `session --remote-server 2323`), and points the space at them.
  The 0.0.9 goal is `pkg install remote-terminal` plus one space entry, with the
  scripts shipped in the package and a documented recipe in the
  [manual](manual/README.md). There is still no authentication, so the recipe keeps
  the LAN-only caveat. Direction only: it authorizes no work.
- **One `power` and one `multiplexer` setting** (proposed, chronium, 2026-10-24; not
  decided): the local and remote space keys are confusing: `multiplexer` (the local
  tab multiplexer) versus `remote_multiplexer` (offered to remote clients), and
  `power` (local `poweroff` and `reboot`) versus `remote_power` (the same actions for
  the remote root shell), as [init](userland/init.md) describes them. The proposal is
  to consolidate: a single `power` and a single `multiplexer`, and to remove
  `remote_power` and `remote_multiplexer`. A Remote space does not serve a normal
  local shell, which stays as it is, so the meaning is unambiguous: on a Remote
  space the two settings apply to its Remote clients only, and on an ordinary space
  to its local shells. `config validate` rejects the old names with a hint naming
  the new key, and there are no compatibility aliases. Direction only: it authorizes
  no work.

**Next kernel push.** Removing BSP-owned allocation and VM mutation, the
[planned next kernel milestone](wip/boot-sdk-ports.md#planned-next-kernel-milestone),
moves out of 0.0.9. It leads the next kernel push, which starts Tuesday 2026-10-27
(chronium, 2026-10-24), followed by the JIT grant and Mesa llvmpipe. The order
decided in [Quake 3 on Mesa llvmpipe](wip/quake3-llvmpipe.md#decided-by-chronium-round-1-2026-10-23)
is unchanged: the JIT grant comes after the BSP removal.

## 0.0.7a (2026-10-20)

Main `81313e0e` (the #1055 merge). A fix release for a blocker in 0.0.7's package
management on installed systems. Updated natively on the owner's stick from 0.0.7
through the PXE installer's Update; `pkg repository add` and `pkg install
clang-hosted` work there.

- **Installed spaces:** installed boots read spaces from `system://config/boot.lua`
  again at every boot (declarative literals only, for now). 0.0.7 built them only
  from the copy frozen onto the ESP at Install or Update, so later edits, such as
  `package_management`, never applied.
- **Bad files:** a file that fails to parse or starts no space falls back to the
  stock spaces, with one line naming the file and the reason (console, serial,
  log).
- **Rescue:** the rescue entry boots the stock spaces with `system://` writable,
  as in 0.0.6, so a bad file can always be fixed.
- **Configuration tools:** `config validate` checks the pool file that boot reads,
  and `config edit` points at it.
- **Also included:** capability pipes task 9a (restricted description), merged
  after 0.0.7.

A tagged fix release is used only for a blocker in a major release feature
(chronium, 2026-10-20).

## 0.0.7 (2026-10-20)

Main `eb61f264`, updated natively on the owner's stick from 0.0.6 through the PXE
installer's Update (Classic origin marker, ESP committed, bin `eb61f264`). 134
pull requests since 0.0.6. The release finishes what was in flight: every
milestone below closed with native checks.

- **Packages, slice 1:** package formats and host packer, the packages volume,
  the `pkg` client (install, verify, remove, rollback, progress display,
  repository add with checks), live RAM packages, the LAN repositories on
  horse, fonts and the hosted Clang/clangd toolchain as packages, and private
  game data packages. The boot archive no longer carries licensed data.
- **Explicit durability:** npfs namespace and writeback in the running
  transaction, and batched FAT32 ordering. On the native ThinkPad ABBA check,
  package extraction plus sync fell from 441-455 s to 76-79 s and FAT32 copy plus
  sync was about 17% faster
  ([record](development/experiments/explicit-durability-measurements.md)).
- **Capability pipes, first slice:** ordered value segments in the kernel, libc
  reading values through, and `wc -c` answering from SIZE (32 MiB in about 25 ms
  instead of about 7 s, mostly the old `wc` decoding).
- **Wi-Fi:** AX200 firmware, passive scan with the firmware's regulatory channel
  set, and open-network association, DHCP and data on 2.4 GHz.
- **Installer:** Classic ESP publication with previous and rescue retention,
  Install or Update chosen first, list-based prompts, read-only Update discovery
  and the Classic origin marker.
- **Shell and userland:** conditions, merged output and script runtime,
  declarative startup settings (`home://.config/shell/startup.lua`), confined job
  work in scripts, handle-based `rm -r`, redirected output that appends across a
  script's commands, command search lists, `net`, the Copilot-key space picker
  and Print Screen.
- **Neovim:** clangd with completion and highlighting, Tree-sitter on by
  default, external jobs and an example `init.lua`.
- **Hardware and kernel:** multiple network interfaces with scoped routes and
  link-rate preference, native hard links, USB hub/HID recovery and NCM receive
  fixes, panic and fault backtraces, userland fonts and power overlay clicks.

Update notes:

- The installer requires the Classic origin marker (`system://.pyxis-classic`)
  on existing installs. The one install that predated the marker was marked by
  hand.
- Coexist stays on its manual boot entry until its publisher path is qualified.

## 0.0.6 (2026-10-16)

Main `106eba83`, updated natively on the stick from vela's own PXE live system
over a USB-A RTL8156BG adapter (Install → classic → Update): the first update
run on vela itself. 53 pull requests since 0.0.5.

- **vela touchpad:** [HID-over-I2C precision input](wip/vela-i2c-touchpad.md):
  LPSS, GPIO-gated polling at 4 ms, Input Mode 3, velocity-based acceleration,
  bottom-right and two-finger right click, click-drag, two-finger scroll, palm
  and typing rejection, bounded recovery and an `input.touchpad_speed` setting.
- **vela hardware:** PCI BAR assignment from firmware apertures, UCSI connector
  events, SystemCMOS, HD Audio (Conexant SN6140), recovered SCI routing, high RAM
  and HPET calibration.
- **Networking without the dock:** CDC-NCM USB Ethernet (the Valve dock and
  RTL8156BG adapters).
- **Capability model milestone B complete:** typed shell capability values and
  command capture, libc capability streams, and HTTP rebuilt on the model; the
  interim HTTP surfaces are gone.
- **ACPI:** uACPI 72f6fdc, which carries our NT reference-semantics fix upstream
  ([uACPI #167](https://github.com/uACPI/uACPI/issues/167), merged as
  [#169](https://github.com/uACPI/uACPI/pull/169)); `acpi://` table export and
  [acpidump](userland/acpidump.md).
- **System:** kernel log levels, idle screen blanking, NVMe coexistence
  authority and native ESP installation, FAT32 mirror repair, `sleep` and
  `pyxis.sleep`, mux pane zoom (Ctrl+B z).

Update notes:

- The installer asks for an authority profile first; a whole-disk Pyxis install
  is `classic`.
- An Update keeps `system://config/boot.lua`. A space that replaces a built-in
  space by name needs the HTTP staging grant, or `http://` and `https://` report
  "Resource unavailable" in it:

  ```lua
  grants = { http_staging = { volume = "http-work", kind = "directory",
                              access = "read-write", optional = true } },
  ```

  `acpi://` also needs `acpi_tables = true` on the space.

Known limits: USB Ethernet receive is slow (about 1 MiB/s, send up to about
90 MiB/s); on vela, a two-finger press with one finger in the bottom-left zone
is a left click.

## 0.0.5 (2026-10-15)

Main `2aa4e13e`, installed natively on the stick from the ThinkPad's PXE live
system and booted on vela, the IdeaPad Slim 5 (Panther Lake) and the first
Intel target. 113 pull requests since 0.0.4.

- **Capability model:** kernel capability forwarders, one process environment
  for every held name, typed open with immutable native error bodies.
- **vela bring-up:** x2APIC delivery, Limine 12.9.3, an exact framebuffer extent,
  CPUID 0x15 timers without the PIT, xHCI capability lists past 4 KiB, MSI
  without MSI-X, NVMe 2.0 (read-only) and control-method power buttons.
- **Kernel and devices:** kernel terminals rendered in the presenter (about
  530 ms to 255 µs per write natively), exit latency under 0.8 ms, local USB
  subtree retirement, HD Audio link-reset retry, Renoir flips on by default,
  recovery from invalid kernel options and from GPT images copied to larger
  devices.
- **Userland:** the quiet installer with progress bars (the Update ran about
  10× faster), FAT32 mounted reads and writes, incremental HTTP GET and POST,
  native jobs and pipelines, native Clang and LLD bundles, threads with static
  TLS, Synaptics absolute input with palm rejection and two-finger scrolling,
  and Chocolate Quake, EDuke32 and DevilutionX bundles with sound.

## 0.0.4 (2026-10-11)

Main `df7ec8ab`, installed natively on the stick from PXE (Install → Update)
and booted from the stick. 125 pull requests since 0.0.3.

Neovim, locally and remotely; the remote multiplexer with live resize; threads
with parking; USB report-protocol mice; the read-only NVMe driver with clean
shutdown; installer progress; the Ctrl+Alt+Delete power overlay; the network
debugger.

## 0.0.3 (2026-10-09)

Main `dc91a4c6`, the third native stick Update.

SDL2 game ports, sound and volume, the clipboard, program bundles, 128 MiB
capture, TSC timekeeping, faster file transfers, remote reboot and the hostname.

## 0.0.2 (2026-10-06)

Main `bd670816`. Runtime SMP across all CPUs, with local private memory.

## 0.0.1 (2026-10-05)

Main `4dc804aa`. The first native installation: the ThinkPad T14 Gen 1 AMD,
booting Pyxis from a USB stick.

# Release history

Pyxis versions have three numbers and are annotated tags on `main`. Each entry
below describes what changed for a user and what an owner must do when updating.
Releases from 0.0.8 on arrive on an installed system with `pkg upgrade`. The tag
message is the authoritative record of a release; add an entry here whenever a
version is tagged. How a release is prepared and checked is in the
[release process](development/release-process.md).

## 0.0.9 (2026-10-25)

The tag is on the merge commit of the PR that adds this entry, which also adds
`"0.0.9"` to `boot/os-release.lua`. 0.0.9 is a ports and userland release on the
0.0.8 base: the kernel interface is unchanged, and the work is in speed, new
packages and the tools around them. Update from 0.0.8 with `pkg upgrade`.

- **Intel CPU speed:** on Intel processors that offer hardware P-states (HWP),
  Pyxis now starts them in autonomous mode. Before, Pyxis left HWP off. On the
  Intel IdeaPad (vela) the probe's single-worker rate went
  from 1.70-1.79 GHz to 4.34-4.38 GHz, an ImGui demo from 11.8 to 25 frames per
  second, OpenArena from 5.5 to 13.4, and a Git clone's median from 29.7 to
  24.3 s ([measurements](development/experiments/cpu-clocks.md)). The `cpuclock`
  package measures a CPU's rate.
- **SHA-256:** one shared SHA-256 library, using the SHA-NI instructions where
  the CPU has them, now verifies packages and backs `sha256sum` and Lua's file
  hashing. On vela, hashing memory is 8.6 times faster and reading and hashing a
  file 3.9 times
  ([qualification](development/experiments/sha256/README.md)). The SDK exports
  it as `sha256.h` ([shared SHA-256](userland/sha256.md)).
- **Trust store:** the bundled CA certificates are curl's 2026-10-20 snapshot of
  Mozilla's list, 118 certificates. It removes three eMudhra roots that Mozilla
  distrusts (emSign ECC Root CA - C3, emSign ECC Root CA - G3 and emSign Root
  CA - C1) and adds none.
- **TLS errors:** `cat` and the shell now report the reason a retained HTTPS
  request failed.
- **Git:** the optional `git` package, built on go-git, clones, pushes and pulls
  over HTTPS, initializes a project and sets its remote, and shows `log`,
  `status`, `add`, unsigned `commit` and `diff` ([Git](userland/git.md)).
- **Neovim:** the `lua-language-server` package gives Neovim completion and
  diagnostics for Lua, with an opt-in `lua_ls` configuration
  ([setup](userland/lua-language-server.md)). The documented example
  configuration now sets `completeopt=menu,menuone,noinsert,popup`, so automatic
  completion no longer inserts a candidate by itself; Neovim's own behavior is
  unchanged.
- **Games and graphics:**
  - Mesa softpipe with an off-screen OSMesa frontend, and SDL2 OpenGL on top of
    it (`mesa-dev`, `sdl2-osmesa-dev`);
  - OpenArena on ioquake3, offline, as the public `openarena` package
    ([OpenArena](userland/openarena.md)); Quake 3 Arena's own data is not
    distributed;
  - reusable Dear ImGui (`imgui-dev`, `imgui-sdl2-opengl3-dev`) and an
    `imgui-demo` program;
  - blurgtext with its own FreeType and HarfBuzz, and a larger `fonts` package
    with CJK, emoji, Roboto and real Plex italics
    ([blurgtext](userland/blurgtext.md)).
- **Audio:** the native HDA driver recovers the stream's timing when its position
  moves unexpectedly, and accepts the hardware's position register as byte-granular
  rather than requiring whole frames, so a stream no longer shuts down at its
  first start on real controllers; its terminal diagnostics print the raw
  readings. On the Intel IdeaPad (vela), a Ship of Harkinian run played through
  with 0 timing recoveries and 0 discontinuities, and the largest commit took
  0.09 ms against the 20 ms bound. Sound was clean, without clicks, but arrived
  in chunks at the 4 to 5 frames per second the software renderer manages
  ([record](development/experiments/soh-gameplay.md)).
- **Development packages:** new shared `-dev` libraries in `sdk://`:
  `json-dev`, `tinyxml2-dev`, `thread-pool-dev`, `dr-libs-dev`, `stb-dev`,
  `spdlog-dev`, `monocypher-dev`, `prism-dev`, `libzip-dev`, `libogg-dev`,
  `libvorbis-dev`, `opus-dev`, `opusfile-dev` and `libultraship-dev`. They carry
  the pieces Ship of Harkinian needs, with the `lus-demo` program; the game, its
  build and its data are not part of the release.
- **C library:** `asctime` and `ctime`, `alloca.h`, `open_memstream`, more of
  ISO C's math functions and `nearbyintf`.
- **Configuration:** a fresh install's `boot.lua` is written one key per line
  with nested tables expanded, and so is the live `live.lua`. Nothing changes in
  meaning. An existing install keeps the `boot.lua` Install copied.
- **Manual and site:** the user manual in `docs/manual/` now covers installing in
  QEMU, the shell, files and URIs, packages, compiling with Clang and more. The
  pyxis.nexus site is built with Hugo from the manual and these release notes at
  each tag.
- **ABI:** `include/abi/` is identical to 0.0.8's. 0.0.9 provides the `"0.0.8"`
  and `"0.0.9"` ABI names, so packages built for 0.0.8 keep working
  ([ABI](interfaces/abi.md)).

Release notes:

- **Updating:** run `pkg upgrade`. It installs the 0.0.9 kernel and core archive
  as the new boot entry and keeps the running one as `Pyxis OS (previous)`.
  Your `home` and your `system://config` files stay as they are.
- **Existing `boot.lua` files:** an install keeps the `boot.lua`, `session.lua`
  and `network.lua` it already has, so the reformatted `boot.lua` appears only on
  fresh installs.
- **New CA bundle:** a server certificate that chains only to one of the three
  removed emSign roots no longer verifies.
- **Not in this release:** Rust, bulk file reads through providers, and the
  Ship of Harkinian game itself.

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

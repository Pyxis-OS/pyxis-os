# Release history

Pyxis versions have three numbers and are annotated tags on `main`. The
tag message is the authoritative record of a release; this page keeps the
history readable in one place. Add an entry here whenever a version is tagged.

From 0.0.6 on, each release candidate is checked in QEMU first: install the previous release, keep data
in `home://` and a `system://config/boot.lua` override, update, and boot the
result. Launch `nvim` there and quit with `:q`; it exercises child spawn with
extra resources, which the 0.0.6 check missed. The owner's stick then gets the update natively. The stick usually
carries a personal build with opt-in programs such as
[DevilutionX](userland/devilutionx.md); releases describe the shareable build.

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

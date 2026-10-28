# Pyxis configuration

Host configuration uses Kconfiglib 14.1.0, an ISC-licensed dependency pinned in
[`requirements.txt`](../../requirements.txt), with no local changes. Install
Python 3 with pip and curses using the host's package manager, then install
Kconfiglib into global Python from the repository root:

```sh
sudo python3 -m pip install --break-system-packages -r requirements.txt
```

Alternatively, install into a local virtual environment and select its Python
for each configuration or source-build command:

```sh
python3 -m venv build/config-venv
build/config-venv/bin/python3 -m pip install -r requirements.txt
make menuconfig PYTHON=build/config-venv/bin/python3
make -j16 image PYTHON=build/config-venv/bin/python3
```

`make clean` removes `build`, including this virtual environment; recreate it
after cleaning or keep it outside `build`.

CI installs the same dependency into the existing builder container before the
ordinary build. No compiler or container rebuild is needed for this change.

## Select options

The checked-in [`.config`](../../.config) lists every available option at its
default. Edit it directly or run:

```sh
make menuconfig
```

The options are under **Pyxis Configuration → Caelum**.

**Native filesystem background flush interval (seconds)** sets
`CONFIG_NPFS_FLUSH_SECONDS`, default 30. The worker flushes all dirty pool
data at each interval, without per-page ages. This is nominal: I/O can take longer
under load, and only explicit file/directory/mount sync guarantees durability.
The positive interval fits the scheduler's 32-bit millisecond deadline input.

**HPET maintenance interval (BSP timer ticks)** sets
`CONFIG_HPET_MAINTENANCE_TICKS`, default 120 delivered BSP LAPIC timer interrupts.
At the current nominal 120 Hz this is about one second. It controls maintenance
of a software-extended 32-bit HPET; direct 64-bit counters need no maintenance
reads. The value must fit a positive 32-bit countdown. Clock initialization
rejects a nominal interval of one hardware wrap or longer, using the advertised
HPET period, but interrupt delays must also remain within the sampling bound.
Choose an interval well below wrap time; timer deliveries are triggers, not
elapsed-time accounting. See the [clock contract](../kernel/timekeeping.md).

**XHCI** controls native USB and defaults to enabled (`CONFIG_XHCI=y`).
`CONFIG_XHCI=n` disables
native controller preparation and worker startup; `CONFIG_XHCI=y` enables them.
Menuconfig may write the disabled choice as `# CONFIG_XHCI is not set`; that is
equivalent to `n`. QEMU qualification and limited owner-reported native
observations do not establish broad hardware qualification. See the
[controller reference](../devices/usb-xhci.md) for the hardware limits.

Rebuild normally after saving. Configuration edits are local Git changes;
commit deliberate default changes with their code. `make clean` preserves
`.config`. When adding an option to `Kconfig`, also list its default in `.config`
so it remains directly editable without opening the menu.

## Kernel boot option recovery

Kernel command-line errors log warnings on serial and the Caelum screen,
then continue boot. Unknown options are ignored. Repeated known options warn;
the last occurrence wins, including an empty value or a missing `=`. The final
value is validated: a malformed final occurrence replaces an earlier valid value
with the fallback below. Boolean options accept `0` and `1`.

| Option | Fallback when absent or invalid |
| --- | --- |
| `init` | `boot://boot-init.pxe`; invalid means missing/empty `boot://` archive path. |
| `mount.disk` | No disk mount authority; GUID must be canonical and nonzero. |
| `mount.pool` | Absent: no pool, and boot init reads `boot://config/live.lua`. Needs `mount.disk`; a malformed or zero value with `mount.disk` starts the rescue space. |
| `boot.install` | Off; no raw installer grants. |
| `nvme.write` | Off; no NVMe write authority. |
| `boot.default_config` | Off; the configuration is read, not the built-in rescue space. |
| `remote.beacon` | Absent; no reverse-beacon selection. |
| `log.udp` | Off. |
| `log.level` | `info`; a level deeper than the build's `LOG_LEVEL` ceiling uses the ceiling. |
| `debug.net`, `debug.image`, `debug.wait` | Debugger and initial wait off as a group if any final value is invalid or incomplete; a valid group needs name and image, with wait defaulting off. |
| `display.size` | No requested size; firmware geometry retained. Backend geometry/capacity checks remain authoritative. |
| `display.timing` | `observe`, unsynchronized copying; `off`, `observe` and `blank` are valid. |
| `display.idle_blank` | 300 seconds; a decimal seconds count or `off` is valid. |
| `display.flip` | On only where the existing Renoir backend qualifies; `0` forces off. |
| `ps2.synaptics` | On only for a matching calibrated profile; other pads remain relative. |
| `display.timing.metrics`, `display.flip.metrics`, `display.inventory`, `display.cursor.probe` | Off. |
| `ps2.synaptics.diagnose`, `usb.hid.describe`, `usb.xhci.diagnose` | Off. |
| `pointer.synthetic` | Off; only `schedule` enables it, only in a `POINTER_SYNTHETIC=1` build. Otherwise it is an unknown ignored option. |

Names retain their existing 1–63 printable-ASCII limits. A debugger image needs
64 hexadecimal SHA-256 digits; invalid debug settings never enable the service.
This is command-line recovery, not recovery from an absent/unloadable archive
program, unavailable hardware or failed startup. Build-time image-setting
validation remains strict.

The 4095-byte command-line limit remains fatal, in both the Limine adapter and
parser. Truncation could discard a later disable, select a different duplicate
or shorten an authority-bearing value; neither layer truncates and continues.

## Boot menu timeout

Synaptics absolute input defaults on only for a matching calibrated profile,
currently the owner-qualified T14 Gen 1 AMD. Other Synaptics pads stay relative;
`ps2.synaptics=1` cannot bypass the profile gate, and `ps2.synaptics=0` forces
relative. For diagnosis append `ps2.synaptics.diagnose=1` to the selected PXE
entry's kernel command line. These are normal boot options,
not build flags; diagnosis observes the selected mode. Query/contact summaries stop
after the bounded diagnostic schedule, omit serials/key identities and add no
default output. No compiler container rebuild is needed. See
[mouse input](../devices/mouse.md#synaptics-absolute-input) and the
[qualification record](experiments/pointer-input-quality/README.md).

For the native [Renoir qualification batch](experiments/renoir-presentation/README.md#native-thinkpad-batch),
`DISPLAY_TIMING=off|observe|blank` generates the `display.timing` boot option
for normal, rescue and installer entries. Omission defaults to **observe**:
read-only counter observation with unsynchronized copying. `off` is the
same-revision baseline; `blank` explicitly enables the experimental guarded
copy path when hardware observation is qualified. It remains off by default
pending native qualification. This boot-only policy has no runtime program
setter; unsupported backends retain their existing presentation path.

`DISPLAY_TIMING_METRICS=1` adds `display.timing.metrics=1` to the same entries
for native qualification. Register dumps and periodic summaries then print at
the info log level, without enabling unrelated kernel traces. Its
default `0` omits the flag and keeps one preparation summary plus rare loss
reports; it does not enable timed copies and has no effect with `off` or
unavailable hardware. Native batches must not use `LOG_LEVEL=trace`.

```sh
make -j16 image DISPLAY_TIMING=observe DISPLAY_TIMING_METRICS=1
```

`DISPLAY_IDLE_BLANK=SECONDS|off` generates the `display.idle_blank` boot option
for normal, rescue and installer entries. Omission leaves the kernel default:
blank the screen after 300 seconds without local keyboard or pointer input.
`off` disables blanking. The value is configuration, not a contract. `0` blanks
as soon as presentation starts, for automated runs on OLED machines: local
input wakes the screen without delivering the key or click, and it blanks again
after 300 idle seconds. An invalid value warns and uses the default. This is
unrelated to `DISPLAY_TIMING=blank`, which names the vertical blanking
interval. See [idle blanking](../kernel/display.md#idle-blanking) for behavior,
screenshots and panic output.

```sh
make -j16 image DISPLAY_IDLE_BLANK=0
```

Renoir flips default on for qualifying hardware; other display drivers are
unchanged. Omit `DISPLAY_FLIP` to use the kernel default. `DISPLAY_FLIP=0` adds
`display.flip=0` to normal, rescue and installer command lines and forces ordinary
GOP copies; `DISPLAY_FLIP=1` adds an explicit request without bypassing any
qualification check. For hand-authored PXE entries, remove the old
`display.flip=1` token to exercise the default, or append `display.flip=0` for
rollback. `DISPLAY_FLIP_METRICS=1` independently enables info-level metrics;
metrics stay off by default. See [Renoir flips](../kernel/renoir-flip.md).

For [Renoir flip inventory](experiments/renoir-flip-inventory/README.md),
`DISPLAY_INVENTORY=1` adds `display.inventory=1`. It emits one read-only numeric
inventory at info level; default `0` adds no inventory output. Native task 1 uses
`DISPLAY_TIMING=off LOG_UDP=1` with that flag, so the previous timing observer
cannot interfere. It never allocates scanout storage or enables flips.
Intel `8086:b0a0` display startup is independent of this option; its
[matched-profile admission and inherited-state reads](../wip/intel-inherited-scanout.md)
run normally. No `display.inventory.power` option exists.

For [Renoir cursor task 1](experiments/renoir-cursor-inventory/README.md),
`DISPLAY_CURSOR_PROBE=1` adds `display.cursor.probe=1`: opt-in info-level,
read-only cursor/clock inventory and cumulative software motion/frame/copy
counters. Default `0` adds no probe logging or extra clock-register mapping.
Native inventory requires `DISPLAY_FLIP=1 DISPLAY_FLIP_METRICS=1
DISPLAY_TIMING=off LOG_UDP=1`, with info logging. It allocates no cursor backing
and adds no cursor or GPU write authority; absent/refused hardware retains normal
presentation and reports the cursor inventory unavailable.

`BOOT_MENU_TIMEOUT` is a Make build setting in nonnegative decimal seconds. It
defaults to `5` for a core image, the install media, whose default entry is
Install, so the rescue entry stays reachable. It defaults to `0` for
`DISTRIBUTION=full`, so development boots go straight to the normal entry. It
configures the generated Limine menu independently of kernel Kconfig. To show
the menu on a full image:

```sh
make -j16 image DISTRIBUTION=full BOOT_MENU_TIMEOUT=5
make -j16 usb-image DISTRIBUTION=full BOOT_MENU_TIMEOUT=5
```

The same value applies to generated configurations for ISO, USB and PXE use.
Both normal and `Install Pyxis` entries remain present with either timeout.
Pass the setting on each build that needs it; a later build without it restores
the default. `scripts/configure-boot.sh` fills the timeout and normal command-line
placeholders in the authored template. This rebuilds boot configuration/image
inputs, not the compiler.

## Image network profile

`NETWORK_CONFIG` selects a local Lua network profile for image assembly:

```sh
make -j16 image NETWORK_CONFIG=/private/path/network.lua
make -j16 usb-image NETWORK_CONFIG=/private/path/network.lua
```

Assembly replaces `config/network.lua` in the initrd with the supplied regular
file, without modifying the userspace checkout or its staged bundle. The session
launcher reads it as `boot://config/network.lua`; see
[network configuration](../devices/networking.md#boot-configuration-and-use)
for the profile fields and selector policy. Keep hardware MAC selectors in this
local file, outside the repository and published captures.

Pass the setting on each image build that needs it. With no override, assembly
uses the packaged automatic-wired profile again. Route, DNS and management
selections remain explicit. This changes image contents only; it does not rebuild
the compiler or change kernel configuration.

## Build outputs and bundles

Each kernel source build reads `.config` through Kconfiglib and generates
`build/kernel-config.h` and the complete effective `build/kernel.config`.
Unchanged contents keep their timestamps, so an unchanged configuration does
not recompile the kernel. Changed symbols rebuild the code that includes the
generated header. Kconfig remains authoritative for option types and defaults.

Kernel [bundles](build-bundles.md) include the effective configuration beside
the ELF, with its content hash in the metadata and payload checksum list.
Selecting a prebuilt kernel uses that bundle's configuration; editing local
`.config` takes effect when rebuilding the kernel from source.

## HID-over-I2C polling qualification

`CONFIG_HID_I2C_POLL_MS` is the tunable nominal interval (1–1000 ms), default
4 ms after vela's native rate qualification. `CONFIG_HID_I2C_MEASURE` defaults
off. When enabled it selects descriptor-derived precision mode for qualification
and performs one finite baseline/idle/8–4–2–1 ms/idle sweep after a two-minute
arming delay. Its finite phase summaries report service-time and report-rate
observations; no scheduler instrumentation or pointer publication is added.
See [the transport limits](../wip/vela-i2c-touchpad.md#task-3-implementation-and-limits-2026-10-16).

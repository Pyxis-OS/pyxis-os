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

## Boot menu timeout

For the native [Renoir qualification batch](experiments/renoir-presentation/README.md#native-thinkpad-batch),
`DISPLAY_TIMING=off|observe|blank` generates the `display.timing` boot option
for normal, rescue and installer entries. Omission defaults to **observe**:
read-only counter observation with unsynchronized copying. `off` is the
same-revision baseline; `blank` explicitly enables the experimental guarded
copy path when hardware observation is qualified. It remains off by default
pending native qualification. This boot-only policy has no runtime program
setter; unsupported backends retain their existing presentation path.

```sh
make -j16 image DISPLAY_TIMING=observe
```

`BOOT_MENU_TIMEOUT` is a Make build setting in nonnegative decimal seconds,
default `0`. It configures the generated Limine menu independently of kernel
Kconfig. Ordinary development boots immediately into the normal entry. For
install media, show the menu explicitly:

```sh
make -j16 image BOOT_MENU_TIMEOUT=5
make -j16 usb-image BOOT_MENU_TIMEOUT=5
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
uses the packaged link-selection profile again. This changes image contents only; it
does not rebuild the compiler or change kernel configuration.

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

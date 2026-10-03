# Pyxis configuration

Host configuration uses Kconfiglib 14.1.0, an ISC-licensed dependency pinned in
[`requirements.txt`](../../requirements.txt), with no local changes. Install
Python 3 with pip and curses using the host's package manager, then install
Kconfiglib into global Python from the repository root:

```sh
sudo python3 -m pip install --break-system-packages -r requirements.txt
```

CI installs the same dependency into the existing builder container before the
ordinary build. No compiler or container rebuild is needed for this change.

## Select options

The checked-in [`.config`](../../.config) lists every available option at its
default. Edit it directly or run:

```sh
make menuconfig
```

The menu is **Pyxis Configuration → Caelum → XHCI**. `CONFIG_XHCI=n` disables
native controller preparation and worker startup; `CONFIG_XHCI=y` enables them.
Menuconfig may write the disabled choice as `# CONFIG_XHCI is not set`; that is
equivalent to `n`. Native xHCI is qualified only in QEMU. See the
[controller reference](../devices/usb-xhci.md) for the hardware limits.

Rebuild normally after saving. Configuration edits are local Git changes;
commit deliberate default changes with their code. `make clean` preserves
`.config`. When adding an option to `Kconfig`, also list its default in `.config`
so it remains directly editable without opening the menu.

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

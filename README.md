# Pyxis OS

Caelum is the freestanding GNU C23 x86_64 kernel of Pyxis OS. Development targets
QEMU booted through OVMF/UEFI and Limine, with a native userspace and capability ABI.

Requires GNU Make, a host C compiler, the [Pyxis GCC/binutils toolchain](toolchain/README.md),
QEMU, GNU cpio, xorriso, host Lua 5.4, and a matching raw OVMF code/variables pair.
Userspace, ports, lwIP and the filesystem core are [pinned submodules](docs/development/sdk-and-repositories.md).
See [port builds](docs/development/ports.md) for application dependencies. CI publishes
[independent build bundles](docs/development/build-bundles.md) for local reuse.

```sh
git submodule update --init userspace ports third_party/lwip
make -j16                 # kernel: build/caelum.elf
make -j16 image           # kernel, userspace and ports: build/pyxis.iso
make sdk                  # export build/sdk; see docs/development/sdk.md
make fs-tools             # opt-in formatter/inspector; requires submodule fs
make run CPUS=4
make run CPUS=4 VIRTIO_NET=1
make run ACCEL=tcg        # software emulation when KVM is unavailable
make image INIT=/tmp/init.sh  # optional native PXE or shebang init
make run LOG_LEVEL=trace  # include scheduler idle diagnostics
make debug CPUS=4        # paused; see docs/development/gdb.md
make clean
```

Make defaults to KVM, one CPU, 256 MiB RAM and GTK display. Override
`CROSS_COMPILE`, `QEMU`, `CPUS`, `MEMORY`, `ACCEL`, `QEMU_DISPLAY`, `OVMF_CODE`
and `OVMF_VARS` on the command line as needed. Firmware defaults are
`/usr/share/OVMF/x64/OVMF_CODE.4m.fd` and `OVMF_VARS.4m.fd` in the same directory;
select matching paths for your distribution. Each run copies firmware variables
into build. Serial uses the launching terminal; exit QEMU with Ctrl-a x.
`QEMU_DISPLAY=none` disables the graphics window.

On a four-CPU boot, the first tab is Caelum's live kernel log. Super+Left/Right
switches spaces; select CPU 1 for the development shell or CPU 2 for the read-only
host-access session. Further CPUs run idle init scripts; navigation currently
shows four tabs. A single-CPU boot shares the BSP's terminal with its shell.
[Init scripts](docs/userland/init.md) select these sessions and their grants.

The shell starts at `home://`, which is RAM-backed and lost on reboot. `app://`
contains the read-only boot archive. Optional [virtio-fs setup](docs/devices/virtio-fs.md)
provides persistent `host://` files and executable loading; no overlay is needed.
Networking is opt-in with `VIRTIO_NET=1`; see [network setup](docs/devices/networking.md).
Kernel-only [block storage](docs/devices/block-storage.md) is opt-in with
`VIRTIO_BLK_IMAGE=/path/to/disk.raw`.
See [QEMU troubleshooting](docs/development/qemu.md) for host emulator boot failures.
The [shell guide](docs/userland/shell.md) and [edit/build/run walkthrough](docs/development/edit-build-run.md)
cover ordinary guest use.

Processes stay on their assigned CPU. The BSP owns kernel allocation, VM mutation
and cleanup, and runs preemptible kernel tasks; user syscall paths remain
non-preemptible. See [SMP ownership](docs/kernel/smp.md), [userspace](docs/kernel/userspace.md)
and [memory](docs/kernel/memory.md). Low-level allocation contracts live in
[PMM](include/kernel/mm/pmm.h), [VM](include/kernel/mm/vm.h) and
[heap](include/kernel/mm/heap.h) headers. Task migration, cross-CPU TLB shootdowns,
AVX and physical-hardware support remain outside the current implementation.

Find subsystem references in the [documentation guide](docs/README.md).
For development, follow [AGENTS.md](AGENTS.md) and the
[milestone index](docs/wip/boot-sdk-ports.md). BOOTSTRAP.md is the historical
bring-up assignment, not the current scope.

## License

Original Pyxis material is licensed under [MPL-2.0](LICENSE). See
[LICENSING.md](LICENSING.md) for scope and third-party exceptions.

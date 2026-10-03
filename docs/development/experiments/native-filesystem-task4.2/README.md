# Native filesystem task 4.2 validation

Manual validation on 2026-10-03/04 for branch `fs/installer-authority`, based
on main `cb756e4`. The runtime implementation is in `a08bb1f` (multiple devices),
`61c7227` (original boot files), `055e114` (raw authority/GPT/mount exclusion)
and `cb8ff58` (boot/init integration). Dependencies are userland `2f8b73c`
([PR 107](https://git.internal/PyxisOS/pyxis-userland/pulls/107)), fs `d352c7e`
and ports `bf7667c`. Later documentation/probe commits do not change runtime code.
The [authority reference](../../../devices/installer-authority.md) describes
the implemented contract.

## Builds and functional observations

Ordinary `make -j16 image fs-tools` passed with the existing
`x86_64-unknown-pyxis-` GCC 16.2.0 compiler, Binutils 2.47.20260726 and Python
3.14.3/Kconfiglib. Changed kernel/userland code and the standalone native probe
built without warnings; the full image retains existing upstream port warnings.
No compiler-container rebuild is needed. Example settings used for the normal
mount/iobench image:

```sh
make -j16 image fs-tools CROSS_COMPILE=/path/to/x86_64-unknown-pyxis- \
  PYTHON=/usr/bin/python3 INIT=/path/to/writer-reboot-init.sh \
  INIT_PRIMARY=app://init-idle INIT_CPUS=3=app://init \
  MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab BOOT_MENU_TIMEOUT=5
```

The existing profile mounted `bench` read-write as `data://`, the same volume
read-only as `ro://`, and `empty` read-write as `empty://`; it synchronized
the disk, created a namespace and launched the ordinary network/remote session.
QEMU 10.2.2 ran on nested KVM with q35, `-cpu max`, four CPUs
(one socket/four cores/one thread), 2 GiB and OVMF. Supported devices used
`disable-legacy=on`; disk caching was `writeback`.

- A two-disk boot listed the blank disk and the prepared disk separately.
  GPT reported ABSENT for the blank device and HEALTHY for the prepared one.
  Normal configured-GUID mounting selected the prepared device even when it
  was the second inventory ID. Existing iobench reads verified their fixture.
- GDB dumped both complete borrowed boot views. `cmp` matched the kernel view
  to the actual `caelum.elf`, and the archive view to the entire `initrd.cpio`.
  The original ELF view was 2,815,920 bytes and the probe archive 20,830,208 bytes
  at that observation; these sizes are artifact properties, not format limits.
- The manually selected `Install Pyxis` menu entry launched native init and
  the [temporary probe](probe/README.md). Five disposable devices covered two
  independent prepared 512-byte disks, a read-only blank disk, an unsupported
  transitional device and a modern blank 4 KiB device. Every probe group passed.
  GDB then observed no remaining raw claims and two retained pools on different
  physical IDs.
- The probe changed both GPT disk GUID headers from
  `87654321-4321-4567-89ab-0123456789ab` to
  `87654374-4321-4567-89ab-0123456789ab`, with recalculated CRCs.
  Release published HEALTHY with the new GUID and enabled normal read-only
  opening. Stopped-image `sgdisk --verify` passed, the extracted pool passed
  `fsck.npfs` and matched its original formatter output byte for byte.
  The other prepared disk matched its original image byte for byte.
- The initial probe found a real 4 KiB boundary defect: the reused npfs request
  carried only the smaller FILE payload, so completion overwrote a raw read's
  final eight bytes. The request now reserves the full raw payload and checks
  that it also covers FILE transfers. A fresh-fixture rerun passed, including
  exact 4 KiB comparisons.
- After ordinary rebuilding removed the probe, install init displayed
  `app://installer.pxe is not packaged; installation unavailable`.
  A separate normal-entry image selecting the same native init displayed
  `missing resource disks; use the Install Pyxis boot entry`.
  Selecting the executable name therefore did not grant raw authority.

The owner requested retaining the temporary probe source for review until
task 4.3 writes the installer. Its manual build/packaging instructions are
separate from the normal image and CI. No fault injection or boot/output
automation was added. Failure-latch, publication-window and deferred-cleanup
paths were source-reviewed; ordinary success runs do not establish failure or
power-loss reliability. Physical-media qualification remains unperformed.

## Matched existing workloads

A pre-task-4.2 ISO was preserved before changing the image inputs. Its archived
SDK manifest records parent `e437210`, `source_state=modified`, userland
`71fc784`, fs/ports task-4.1 integration and the same compiler. This is a local
pre-implementation artifact, not a pristine-main build. Its ISO SHA-256 is
`02bffcf39e3dd5f73f4472d05371a92934e3c46100b720e6ce4c2432e41f482c`.
The final comparison image records parent `0687584`, modified only by untracked
validation docs/probe, and userland `2f8b73c`; its ISO SHA-256 is
`c3fcc014b3205f897d6e9481f5bb2b866999f9624bd80f96a5168f785c29c889`.

Each boot used a fresh copy of the same 132 MiB GPT disk, one 128 MiB pool,
8 MiB journal, `bench`/`empty` volumes and a 1 MiB fixture. One modern 512-byte
VirtIO disk, modern network and RNG devices, fresh OVMF VARS, CPU/RAM/cache and
the mount profile matched. The original image SHA-256 remained
`47fca84995aa7343bac14db062ff27614b72e7bec98cad36f24fafb6d8664813`.
Existing remote-terminal input ran:

```text
iobench read data://iobench.bin --buffer 4088 --rounds 5
iobench write data://installer-comparison.bin --buffer 4080 --rounds 5 --sync
```

Profiling was off, with one untimed warmup per command. All five measured
samples verified 1 MiB without failed passes or short transfers. Reads used
257 calls plus one EOF call; writes used 258 calls. Times below are milliseconds,
in sample order, rounded only for presentation:

| Metric | Before, five samples | Final, five samples |
| --- | --- | --- |
| Payload read | 145.996, 142.178, 146.042, 154.014, 154.097 | 151.798, 147.230, 142.460, 140.053, 147.015 |
| Complete read including open/EOF | 158.471, 165.907, 170.449, 178.748, 178.600 | 164.557, 171.591, 171.991, 164.131, 171.021 |
| Write transfer | 245.847, 247.256, 248.908, 246.698, 253.397 | 258.623, 260.424, 249.348, 259.235, 256.045 |
| File sync | 229.280, 232.656, 220.780, 226.077, 227.606 | 232.522, 223.840, 226.207, 227.837, 232.792 |

| Metric | Before median [range] | Final median [range] | Median change |
| --- | --- | --- | --- |
| Payload read | 146.042 [142.178–154.097] | 147.015 [140.053–151.798] | +0.7% |
| Complete read | 170.449 [158.471–178.748] | 171.021 [164.131–171.991] | +0.3% |
| Write transfer | 247.256 [245.847–253.397] | 258.623 [249.348–260.424] | +4.6% |
| File sync | 227.606 [220.780–232.656] | 227.837 [223.840–232.792] | +0.1% |

The write median rose in this boot, with overlapping ranges. An earlier matched
boot before the request-buffer correction had a 251.787 ms write median,
245.646–259.958 ms range, illustrating between-boot variation. This small sample
does not establish a causal regression or a performance improvement. Per-device
queues/workers and serialized filesystem ownership remain deliberate; this task
does not broaden into writer optimization. These are nested-VM measurements,
not the owner's hardware results. The stopped final pool passed host fsck, and
its extracted written file matched the installed 1 MiB fixture.

All validation clients, debuggers and QEMU processes were stopped; the ordinary
image was restored. Exact submitted-revision CI is reported on the PR.

## Parent review follow-up

The owner-accepted review of [PR 364](https://git.internal/PyxisOS/pyxis-os/pulls/364)
replaced the fixed menu delay with `BOOT_MENU_TIMEOUT`, default `0`. The samples
above used the original five-second menu. This follow-up changes configuration
generation and documentation; disk runtime code and the temporary probe source
are unchanged.

Manual `make build/limine.conf` produced timeout zero, and
`make build/limine.conf BOOT_MENU_TIMEOUT=5` produced timeout five. Both retained
the normal and Install entries with their independent command lines. Shell
syntax and document/link review passed. Ordinary image assembly with verified
existing kernel/SDK/userspace/ports bundles passed for both values; the authored
template change was freshly packed into the archive rather than taken from a
stale image. A four-CPU, 2 GiB nested-KVM QEMU/OVMF boot showed both entries and
the countdown at timeout five; manually selecting Install reached native init's
unpackaged-installer diagnostic. A fresh timeout-zero boot reached the normal
init selections without showing the menu.

The owner also accepted raw consent inspection with an in-memory committed
journal overlay and no writes before consent, deferring pool retirement to the
later live-install flow. These are task-4.3 policy refinements, documented as
future behavior rather than implemented consent code. The temporary probe and
protected task-3 experiment directory remain intact.

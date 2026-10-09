# Native filesystem task 4.2 validation

Manual validation on 2026-10-03/04 for branch `fs/installer-authority`, based on main `cb756e4`. The runtime
implementation is in `a08bb1f` (multiple devices), `61c7227` (original boot files), `055e114` (raw
authority/GPT/mount exclusion) and `cb8ff58` (boot/init integration); dependencies are userland `2f8b73c`
([PR 107](https://git.internal/PyxisOS/pyxis-userland/pulls/107)), fs `d352c7e` and ports `bf7667c`. Later
documentation and probe commits do not change runtime code. The
[authority reference](../../../devices/installer-authority.md) describes the implemented contract.

## Builds and functional observations

Ordinary `make -j16 image fs-tools` passed with the existing GCC 16.2.0 compiler, Binutils 2.47.20260726 and
Python 3.14.3/Kconfiglib; changed kernel and userland code and the native probe built without warnings (the full
image keeps existing upstream port warnings), and no compiler-container rebuild is needed. The normal
mount/iobench image used `INIT=…/writer-reboot-init.sh INIT_PRIMARY=app://init-idle INIT_CPUS=3=app://init
MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab BOOT_MENU_TIMEOUT=5`; its profile mounted `bench` read-write as
`data://`, the same volume read-only as `ro://` and `empty` read-write as `empty://`, synchronized the disk,
created a namespace and launched the network/remote session. QEMU 10.2.2 on nested KVM, q35, `-cpu max`, four
CPUs, 2 GiB, OVMF, `disable-legacy=on` devices and `cache=writeback`.

- A two-disk boot listed the blank and prepared disks separately: GPT ABSENT for the blank device and HEALTHY for
  the prepared one, and normal configured-GUID mounting selected the prepared device even as the second inventory
  ID. Existing iobench reads verified their fixture.
- GDB dumped both complete borrowed boot views; `cmp` matched the kernel view to the actual `caelum.elf` and the
  archive view to the entire `initrd.cpio` (original ELF view 2,815,920 bytes, probe archive 20,830,208 bytes at
  that observation; artifact sizes, not format limits).
- The manually selected `Install Pyxis` entry launched native init and the
  [temporary probe](https://git.internal/PyxisOS/pyxis-os/src/commit/f8b12a7/docs/development/experiments/native-filesystem-task4.2/probe/README.md).
  Five disposable devices covered two independent prepared 512-byte disks, a read-only blank disk, an unsupported
  transitional device and a modern blank 4 KiB device; every probe group passed, and GDB then saw no remaining raw
  claims and two retained pools on different physical IDs.
- The probe changed both GPT disk GUID headers from `87654321-4321-4567-89ab-0123456789ab` to
  `87654374-4321-4567-89ab-0123456789ab` with recalculated CRCs; release published HEALTHY with the new GUID and
  enabled normal read-only opening. Stopped-image `sgdisk --verify` passed, the extracted pool passed `fsck.npfs`
  and matched its formatter output byte for byte, and the other prepared disk matched its original image.
- The initial probe found a real 4 KiB boundary defect: the reused npfs request carried only the smaller FILE
  payload, so completion overwrote a raw read's final eight bytes. The request now reserves the full raw payload
  and checks that it also covers FILE transfers; a fresh-fixture rerun passed, including exact 4 KiB comparisons.
- After ordinary rebuilding removed the probe, install init displayed `app://installer.pxe is not packaged;
  installation unavailable`, and a normal-entry image selecting the same native init displayed `missing resource
  disks; use the Install Pyxis boot entry`, so selecting the executable name did not grant raw authority.

The temporary probe source was kept for review until task 4.3 wrote the installer and then removed (history and the
link above keep it, separate from the normal image and CI). No fault injection or automation was added; failure
latch, publication window and deferred cleanup were source-reviewed, success runs do not establish failure or
power-loss reliability, and physical-media qualification is unperformed.

## Matched existing workloads

A pre-task-4.2 ISO (parent `e437210`, `source_state=modified`, userland `71fc784`, fs/ports task-4.1 integration,
same compiler) was preserved before changing the image inputs; it is a local artifact, not a pristine-main build.
The final comparison image has parent `0687584`, modified only by untracked validation docs and probe, and userland
`2f8b73c`. Each boot used a fresh copy of the same 132 MiB GPT disk (one 128 MiB pool, 8 MiB journal,
`bench`/`empty` volumes, 1 MiB fixture), one modern 512-byte VirtIO disk, network and RNG, fresh OVMF variables and
matched CPU/RAM/cache/mount profile, and the original image hash was unchanged. Commands:

```text
iobench read data://iobench.bin --buffer 4088 --rounds 5
iobench write data://installer-comparison.bin --buffer 4080 --rounds 5 --sync
```

Profiling was off with one untimed warmup per command; all five samples verified 1 MiB with no failed passes or
short transfers (reads 257 calls plus one EOF call, writes 258). Times in ms, in sample order:

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

The write median rose in this boot with overlapping ranges; an earlier matched boot before the request-buffer
correction had a 251.787 ms write median (245.646–259.958 ms), showing between-boot variation. This small sample
establishes neither a causal regression nor an improvement. Per-device queues/workers and serialized filesystem
ownership remain deliberate and this task does not broaden into writer optimization. These are nested-VM
measurements, not the owner's hardware; the stopped final pool passed host fsck and its written file matched the
fixture.

## Review follow-ups and integration

The review of [PR 364](https://git.internal/PyxisOS/pyxis-os/pulls/364) replaced the fixed menu delay with
`BOOT_MENU_TIMEOUT` (default `0`; the samples above used the original five-second menu). `make build/limine.conf`
produced timeout zero and `BOOT_MENU_TIMEOUT=5` produced five, both keeping the normal and Install entries with
their independent command lines; ordinary image assembly passed for both values with the template change freshly
packed, a four-CPU 2 GiB nested-KVM boot showed both entries and the countdown at five (Install reached the
unpackaged-installer diagnostic), and a timeout-zero boot reached the normal init selections without the menu. The
owner also accepted raw consent inspection with an in-memory committed journal overlay and no writes before
consent, deferring pool retirement to the live-install flow: task-4.3 policy, documented as future behavior.

After userland PRs [107](https://git.internal/PyxisOS/pyxis-userland/pulls/107) and
[108](https://git.internal/PyxisOS/pyxis-userland/pulls/108) merged, parent integration `94067c7` merged main
`cf78936` and pinned published userland `08e3c4b`; current main supplies the matching explicit network-selector
ABI, preserving its RTL8111 preparation and driver source selection (filesystem, ports and lwIP unchanged). A full
source `make -j16 image fs-tools` passed with a clean SDK manifest, the probe rebuilt against it, a normal-entry
boot reached the remote session through explicit VirtIO selection, and iobench read and write/file-sync each
verified two 1 MiB samples after a warmup. This is an integration check, not a replacement for the five-sample
comparisons above. A fresh-fixture five-device Install-entry run passed every probe group (exact 4 KiB transfers and
last-close claim cleanup included); after stopping QEMU, GPT verification and pool fsck passed and the extracted
pool and the independent prepared disk matched their original images byte for byte.

CI for the earlier follow-up `d56193b` passed filesystem checks but failed the image build fetching the unchanged
Mbed TLS archive from GitHub with HTTP 503; the existing workflow retry on that exact revision passed both jobs.

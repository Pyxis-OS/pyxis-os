# NVMe driver task 1 qualification

[Accepted scope](../../../wip/nvme-coexistence.md#1-nvme-driver--assigned-2026-10-11).
No native run or native write is authorized by this record. The first native
boot is read-only and is staged by the orchestrator through Luna.

## Before-code control

Revision `a617bcd74fcb875b3ddae8ad1ceaaeabae698f9e`, ordinary default/info image,
LLVM 23.1.3 builder `pyxis-llvm23.1.3-49e2c1a`. Verified matching SDK/ports payloads;
userland rebuilt at its pinned revision. The unchanged kernel and image build
passed. Sealed ELF/ISO/initrd and hashes remain in local `build/nvme-control/`.

Host QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), Q35/nested KVM, max CPU, four CPUs,
512 MiB, fresh OVMF variables with the host code/vars pair, standard VGA,
relative PS/2, VirtIO RNG. One disposable 32 MiB NVMe disk uses 512-byte blocks,
`mdts=6,max_ioqpairs=4,msix_qsize=1`; a separate 64 MiB modern VirtIO block disk
uses one queue. Both are regular raw files with writeback caching. No host block
device or native controller is attached.

The stock ISO boot passed without the Fedora-VM AHCI crash. NVMe appears in PCI
but has no block service on the control. GDB observed one block device: VirtIO,
131,072 blocks of 512 bytes, 64 KiB transfers, eight slots, writable/flush flags,
three published and three completed requests. GPT reported absent on that empty
VirtIO disk. The Development shell was reached by tab navigation and inspected
in a QEMU capture. No disk data was written by this exercise. QEMU was stopped;
no debugger remains attached. These are correctness controls, not native timing
or physical power-loss evidence.

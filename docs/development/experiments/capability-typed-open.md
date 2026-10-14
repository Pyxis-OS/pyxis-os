# Typed OPEN integration checkpoint

2026-10-14. Parent `52255e76` based on merged forwarders `4db9e431`;
userland `5d720ee`, ports `91b5e19`, fs `d84f8084`, lwIP `a1aadb91`.
Builder `pyxis-llvm23.1.3-e0801c2`; ordinary `make -j8 kernel`, `sdk` and
`image` passed. The builder lacks Python's Kconfig module; the existing host
`genconfig`/`kconfiglib` modules were supplied through build-local PYTHONPATH.
No compiler-container rebuild or new benchmark/test infrastructure.

Nested QEMU: q35, KVM, 4 CPUs / 1 thread per core, 2 GiB RAM, std VGA,
VirtIO net/RNG, live image, stock Remote space at 80 × 24. Existing
`pyxis-remote --machine --no-shell-echo` carried interactive commands; every
session ended with successful FINAL and complete drain. No timing measurements.

| Existing command | Observed result |
| --- | --- |
| `cat text://welcome` | Expected text, exit 0 |
| `cksum text://guide` | `3242480442 6756`, exit 0 |
| `cat text://missing` | NOT_FOUND diagnostic, exit 1; no successful empty output |
| Write/read `tmp://a3.txt` | Native RAM I/O succeeded |
| `counter --provider-forwarders text tmp a3.txt` | Existing retarget, representation refusal, child lineage, takeover and controller-release qualification passed |

Read-only GDB inspection at `endpoint_description_capture` during the missing
resource OPEN observed NOT_FOUND, ERROR_BODY, a 33-byte OBJECT_FILE / FILE_RAM,
`immutable_snapshot=true`, and no host/npfs/FAT32 backing node. Its stored body
reference count was 1 before handout. Continuing produced the expected reader
failure and complete session drain; own QEMU/GDB jobs were stopped.

This exercised construction and the SDK's native-body attestation. Reading the
handed-out body after provider exit, empty/max-size bodies, invalid publication
and table-capacity refusal still need runtime qualification. Their independence
and sealed WRITE/RESIZE refusal are source-inspected, not claimed as run.
Single-resolution typed directory lookup compiled on all native backends; its
new held-subtree helper needs runtime coverage. Environment name routing and
removal of the old namespace/URI-spelling path await delta's task 2 integration.
Task 3 remains incomplete; native after-A/after-C repeats remain due in the
[baseline record](capability-model-baseline.md).

Raw build, Remote and GDB records stay under the local worktree's `build/`;
they are not committed.

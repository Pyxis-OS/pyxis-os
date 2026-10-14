# Typed OPEN qualification

2026-10-14. A2 integration: parent `0a7972be` based on `0f1aa2f8`,
userland `aad7d2c`, ports `efe8d87`, fs `d84f8084`, lwIP `a1aadb91`.
Builder `pyxis-llvm23.1.3-df87791`; ordinary `make -j8 image` passed,
including kernel, SDK, ports and userspace. Existing host Kconfig modules were
supplied through build-local PYTHONPATH. No compiler rebuild or new test infrastructure.

Nested QEMU: q35/KVM, 4 CPUs / 1 thread per core, 2 GiB, std VGA,
VirtIO net/RNG, live image, stock Remote 80 × 24. Existing
`pyxis-remote --machine --no-shell-echo` carried interactive commands.
Both sessions reached FINAL with exit 0 and complete drain. No timings.

| Commands | Observed result |
| --- | --- |
| `cat text://welcome`; `cksum text://guide` | Expected text; guide checksum `2332343091 7084`, exit 0 |
| `cat text://missing` | NOT_FOUND, exit 1; no successful empty output |
| Write/read `tmp://a3.txt` | Native RAM I/O succeeded |
| `counter --provider-forwarders text tmp a3.txt` | Existing retarget, representation refusal, reply lineage, takeover and controller-release qualification passed |
| `env copy files tmp`; native write/read through `files://` | Same native directory routing |
| `env copy words text`; `cat words://welcome` | Provider alias received the opaque remainder |
| `cat TERM://`; copy TERM to `value` and read `value://` | Exact inline text bytes `pyxis`, exit 0 |
| `cat value://extra`; write to `value://`; unset then read | Unsupported remainder, denied write, NOT_FOUND after unset; no fake success |

Earlier read-only GDB on the pre-A2 `52255e76` checkpoint observed the missing
text result's NOT_FOUND/ERROR_BODY backed by a sealed 33-byte native RAM FILE,
with no host/npfs/FAT32 node. This establishes construction and native-body
attestation, not a read after provider exit.

Daybreak qualification requested on #833: handed-out body read after provider
exit; empty/max bodies; invalid publication preserving the old cache; immediate
full-table CALL_LIMIT; typed held-directory/cwd-chain and captured-value lifetime.
Those remain pending runtime results; source inspection is not a pass. Task 3
remains unchecked. Native after-A/after-C repeats remain due in the
[baseline record](capability-model-baseline.md). Raw captures stay local in `build/`.

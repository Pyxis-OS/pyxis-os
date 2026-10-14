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

## Exact-head lifetime and admission checks

Four-CPU QEMU qualification at parent `24c2d820`, userland `8ac71538` and
ports `efe8d877` found no confirmed contract violation.

| Check | Observed result |
| --- | --- |
| Empty and 65,536-byte error bodies handed out before provider exit | Both remained readable afterward |
| Invalid description publication; sealed body WRITE/RESIZE | Prior body preserved; both mutations refused |
| All 64 handle slots occupied | Describe with body returned CALL_LIMIT; metadata-only describe succeeded; freeing a slot allowed body capture |
| TEXT/CAP name opens, capture across replacement, held-directory traversal | Passed, including refusal to cross the `..` boundary |
| 300 concurrent CREATE rounds | 294 CALL_ALREADY_EXISTS, 6 found the created file; no unexpected result |
| Expired provider deadline; five malformed provider replies | CALL_TIMED_OUT; malformed replies rejected or explicit failure without a handle |

These are observed behaviour, not a heap-leak proof: no GDB post-close object
counts were collected. Raw output remains outside the repository in the review
workspace. Task A3 is qualified; native after-A/after-C repeats remain due in the
[baseline record](capability-model-baseline.md).

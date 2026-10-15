# ESP payload verification cost

2026-10-15, owner-assigned performance bug; disposable nested QEMU media only.
The non-extending 30-second FAT publication interval is unchanged. Progress
reporting must observe work rather than pace it. Installed activation and native
writes are not enabled by this work.

## Attribution

A local, never-merged caller reads and hashes the already-staged 40,375,612-byte
`boot.arc` using the helper's 3,072-byte chunks. It accumulates clock samples
around FILE reads, SHA-256 updates and progress advances, then emits one summary.
The input has SHA-256
`150f50b6334d368274e531fd08373aa970d7216d5e3badc8c625f59e3b179205`.

First attribution result: 13,144 reads; 416.507 s total, 410.115 s inside FILE
reads, 1.130 s hashing and 2.565 s progress calls. The remaining 2.697 s includes
probe clock calls and loop overhead. The final hash matches. Progress is about
0.6% of total, not a 250 ms execution throttle: its elapsed-time gate returns
without sleeping. Synchronous reporting has a cost, but does not explain this
failure. This profiled run included a brief parallel build; it localizes cost,
not a standalone matched speedup claim.

Runtime: earlier stager qualification kernel, main `b030c7b1` kernel source,
SHA-256 `bfe59107ae05e537a7858da7c991897c1b63f43858554350b9ce790c1b2481ed`;
fs `101e1f68`, local caller based on userland `01c22924`. QEMU 10.2.2,
nested KVM, Q35, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA, modern
virtio-blk; 512 MiB mirrored FAT32 ESP, 512-byte sectors, 4 KiB clusters.
The fixture is a clone of the verified staging attempt; only its dirty marker
was cleared offline on that clone to admit a fresh RW mount. The original
qualification artifact is untouched. The probe performs no payload writes.
Raw host logs/images and the probe remain outside the repository.

## Source cause and first fix

Every small mounted FILE read calls `read_object_context` / `validate_object`.
`fat32_locate_entry` validates the entire selected file chain, then
`chain_matches` walks that entire chain again. Each operation resets its FAT
cache. Payload reads additionally submit one 512-byte backing request at a time.
The work therefore grows with both file size and the number of small FILE calls,
with repeated synchronous block-worker round trips.

The first fix uses the already-reserved full-check workspace for bounded FAT
windows of up to 128 sectors during regular-file read/locate, and batches
physically consecutive payload sectors through the existing 4 KiB buffer.
It preserves all current validators and both complete-chain passes. No new
allocation, cache lifetime, public interface, authority or deadline is introduced.
The format-library build and an independent source review pass. Current-main
matched kernels are built; runtime before/after results are pending.

## One possible next decision (not accepted or implemented)

If batching alone cannot meet the budget, the recommended default is to retain
successful **regular-file read validation** (geometry, selected metadata and
complete chain) within one unchanged, exclusively owned mount epoch. The first
read validates fully. Any mutation attempt, explicit CHECK or error invalidates
the proof; the next read validates fully again. Bounds, stopped-state refusal,
actual payload reads, mutation validation, full CHECK and publication hashes
remain. No interval extension or verification omission is proposed.

The mount already excludes external writers and owns canonical records. Reusing
its proof is correct under that ownership, but the current mounted-engine
reference explicitly promises geometry/metadata/chain revalidation for reads.
Changing the reread lifetime would delay detection of silent in-session metadata
corruption until invalidation, CHECK or remount. That change needs the owner's
answer before implementation. The alternative is to retain per-call media
validation and continue optimizing its repeated work; no completion-time claim
is made for either alternative yet.

## Qualification still required

Record exact-head matched current-main A/B costs, progress-enabled/disabled
results and hashes. Then exercise the actual publisher, including BEGIN/CHECK,
all final payload verifications, publication and cleanup under the unchanged
30-second interval. A successful isolated hash pass does not qualify publication.

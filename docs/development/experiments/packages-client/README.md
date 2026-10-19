# Package client qualification

Nested QEMU, 2026-10-19. Baseline: Pyxis `2b001c6e` plus the documentation-only
#981 proposal; userland `a9f6c64a`, ports `7604c85`, fs `2533799`. The owner
builder was `pyxis-llvm23.1.3-3d69e83`; ordinary `make -j8 image`, Q35/KVM host
CPU, four vCPUs, 2 GiB, standard VGA, VirtIO block/net/rng and OVMF. All writes
were to a disposable 1,536 MiB disk. No native, PXE or abrupt power-cut run.

The baseline booted, acquired DHCP, ran shell commands and read the static
repository through HTTP. `pkg` was absent. The existing host packer produced a
4,608-byte one-file package and a 12,776,960-byte package containing the actual
33 font, notice and provenance files. Warm host pack times were 0.050 and
0.109 seconds; these are packer costs, not guest fetch costs.

## Runtime observations

The initial client at userland `2d4a7a5` / Pyxis `a0161a00` completed Classic
Install on the scratch disk, provisioning packages, private npfs HTTP backing
and the initially empty repository configuration. With an explicitly configured
loopback host repository, dependency installation produced set 1. Removing a
required dependency refused; removing its dependent produced set 2. Installing
the later opaque version produced set 3; two rollbacks created fresh sets 4 and
5, returning to versions 1 and 2 respectively. Selected files and dependencies
passed `pkg verify`.

A subsequent clean boot retained exactly sets 4 and 5 and their sidecars.
A package-declaring qualification bundle opened version 2 through its declared
read-only tree. A bundle declaring HTTP and HTTPS providers received exactly
those two, each with provider rights `0x1`; an unavailable optional declaration
received none. A bundle declaring no providers retained the existing five
ambient providers, including the existing HTTP rights `0x7`. Actual handle
queries established these grants; they were not inferred from manifest text.

An installed Update to Pyxis `1c6904f9` / userland `69b3d0c` preserved the
repository configuration and package selection and provisioned filesystem-info
for actual npfs checks. Subsequent CLI checks use the separately rebuilt current
userland `1100eb9` bundle through that installed trusted launch context.
Queued ordinary input after `verify` is preserved.

Two simultaneous Remote sessions demonstrated writer exclusion. Explicit unlock
reported an inactive staging directory, cleared an abandoned lock and kept the
active set unchanged. A logically damaged current refused status, ordinary
mutation and implicit rollback; explicit rollback to a verified retained set
created set 6. A second session then switched to set 7 while a qualification
process retained an already-open version-1 FILE for 12 seconds: the held FILE
still read version 1, while a new launch read version 2.

The font and synthetic 96 MiB payload archives (12,776,960 and 100,667,392 bytes)
installed together and a subsequent full `pkg verify` passed. The one combined
nested run took **388.85 seconds** end to end, including index fetch, existing
selection verification, both fetches, archive/per-file verification, extraction,
synchronization, activation and the final verification. This is not a phase
breakdown or a native throughput measurement. The deadline formula and provider
backing were unchanged; neither a longer timeout nor RAM fallback was used.
A competing writer and the small read-only grant check also ran during this
qualification, so this is a functional workload cost, not an isolated benchmark.

Source inspection: provider spooling and FILE calls transfer roughly 4 KiB at a
time; persistent snapshot reads cross client/provider and provider/native calls.
The reader's fixed 16 KiB scratch requires repeated bounded calls and reads the
snapshot for its archive hash and again for member hashing/extraction. It does
not sync each chunk: extraction has one final directory sync, then the store
move is synchronized. NPFS cache-pressure/background commits also occur.
Provider buffers are 8 KiB and the NPFS data cache is about 4 MiB per pool;
the archive is not retained whole in a client allocation. These are code facts,
not measured phase attribution or a guest peak-memory measurement.

Ordinary repository clones rejected a changed archive hash, a truncated archive
and an unsupported USTAR member after valid whole-archive hashing, without
changing set 8. A delayed HTTP response with `--timeout 1` returned
CALL_TIMED_OUT without activation. Review cancellation preserved the selection;
Ctrl+C during a delayed fetch terminated the child, retained its abandoned lock,
and allowed explicit unlock after observed retirement. A bundle without the
management declaration read status but refused mutation. Damaged predecessor
metadata refused ordinary mutation and implicit rollback, listed candidates,
and allowed explicit verified recovery.

A further boot retained only set 9, set 8 and their sidecars; previously
abandoned staging was gone. A deliberately abandoned writer lock survived that
boot and refused another mutation until explicit unlock; boot did not steal it.

For bounded backing coverage, a disposable GPT clone had an additional empty
16 MiB npfs pool, with the existing authored `http-work` volume binding moved to
that partition. A 100,667,392-byte archive refused with HTTP transaction failure
and the same process stream then read the unchanged active set 9. No RAM
substitute or set publication occurred. An earlier low-space fixture in the
shared system pool also refused and later read unchanged set 9, but immediately
following launch briefly failed with Out of memory; its failure phase and the
transient allocation pressure were not attributed. This is an observed limit,
not proof of a particular native allocator or no-space return path.

## Limits and evidence

The large-transfer fixture is synthetic data, not a hosted compiler or a
converted game consumer. Consumer conversions and native slice qualification
remain later tasks. Raw QEMU captures, command results, recipes and logs are
kept outside the repository under
`/shared/present/packages/task3-20261019/`.

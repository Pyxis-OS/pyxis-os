# HTTP shell transaction checkpoint — 2026-10-13

Task 5 is in review; standalone script delivery awaits the owner contract in
[#780](https://git.internal/PyxisOS/pyxis-os/pulls/780). No native measurements are
claimed. **Pyxis must be able to hit good network targets overall in the future.**
The ThinkPad task 4 1 MiB GET measured about 81–83 MiB/s, versus the historical
85 MiB/s TCP receive reference, not a contract. Relevant changes must track the
gap; these nested results cannot be divided by native TCP to claim a regression.

## Baseline before implementation

Fresh parent `bfd14785`, userland `b38b0e09`, ports `9f407742`, filesystem
`414be0ac`, LLVM23.1.3 builder `4c23a20`. Ordinary image plus SDK/ports/userland/
kernel bundles rebuilt and verified. Nested KVM q35/HPET, max CPU, four CPUs,
2 GiB, VirtIO network/entropy, fresh OVMF; local fixtures only. Direct POST adds
a disposable VirtIO-blk/writeback NPFS disk and the existing manual task 4 probe,
whose private backing is absent from the client's boot/out roots.

| Workload | Median ms (range) |
| --- | --- |
| 1 MiB GET whole, five verified samples | 425.802 (264.685–899.135) |
| 1 MiB RAM whole, five verified samples | 0.731 (0.698–0.874) |
| Ordinary Lua child launch/wait, 20 samples | 8.787 (5.851–37.676) |
| Direct 1 MiB staged NPFS POST, five verified samples | 1502.337 (1496.464–1542.845) |
| POST preparation | 70.694 (50.401–71.409) |
| POST stage/flush | 603.313 (573.997–607.542) |
| POST commit | 852.248 (836.608–864.502) |

Commands: `iobench read http://10.0.2.2:19085/fixed --rounds 5`, RAM control
via `cp boot://share/iobench.bin tmp://http5-ram.bin`, monotonic timing around
`pyxis.run{"echo","-n"}`, and the existing Lua w+ API with 128×8192 z bytes,
explicit flush/commit and complete response/status. Every payload/EOF/hash check
passed; one untimed warmup each. No clock costs subtracted or isolated spawn cost
claimed. Unrelated host compiler load produced broad ranges: these initial
samples do not establish worker overhead/savings. The matched worker comparison
still needs interleaved runs in one window, identical producer/backing/data/CPU
configuration; Lua w+ remains direct and must not be labelled a worker path.

## QEMU functional evidence

Userland `f317f09` broad RAM suite, `61ed1fa` corrected JSON example/NPFS/TLS;
ports `ff2cb09`, parent task 5 RAM ABI/kernel changes. Source/ISO/SDK provenance
and raw captures remain in local build output, outside the repository.

- Stock declared RAM: exact `printf '{}' > json+http://… => reply`, then
  `cat < @reply`, one 2-byte JSON POST and no new GET. Retained response/STATUS
  copies, shared consumption/EOF, drop and name/env/missing-capture refusals pass.
- Failed producer prevents submission and later manual commit. Truncated readers
  and reader pipelines report actual native child exit 1; a successful final
  checksum does not hide the failing reader.
- Exactly 16 MiB stages/uploads with an independent matching SHA-256;
  16 MiB+1 refuses CALL_LIMIT before mutation, prints the NPFS hint and leaves
  submission not started. Read-only debugger inspection confirms DIRECTORY_RAM
  staging and no backing leaf/ancestor/source in the interactive shell.
- Stock declared private NPFS with local TLS: 17/32 MiB POST exact server lengths/
  hashes, 32 MiB GET incremental equality/count, truncation and unauthenticated
  TLS EOF failures, readable early 413/500 bodies, and a 35-second response with
  an explicit 60-second deadline pass. The private volume is absent from every
  client root; normal bootstrap creates provider leaves without a test wrapper.

The final worker `22fcade` aligns pipe reads to FILE_WRITE_MAX_BYTES to avoid
16-byte trailing WRITE calls; its allocated buffer stays 4 KiB. Final-source
multi-GB GET/POST and worker-cost runs remain unrun. Owned qualification jobs are
stopped and disk/ISO checkpoints retained. This is a recoverable checkpoint,
not milestone closure or completion evidence for the pending script surface.

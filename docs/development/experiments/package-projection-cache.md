# Active package projection reuse

2026-10-29, debt #1497. Accepted: read `current` on every fresh live operation,
retain one validated projection keyed by mounted-volume domain and set, and trust
the existing immutable-publication contract. Same-set manual index edits can
remain unseen until eviction, another selection or reboot. The
[interface contract](../../interfaces/command-search.md#live-package-command-directories)
records authority, lifetime, visibility and budget rules.

## Matched QEMU measurements

Nested KVM/q35, host CPU, 4 vCPUs, 4 GiB, OVMF and RAM-backed npfs (128 MiB).
Baseline kernel: main `20c99b3f` plus bulk-HTTP fix `e346cdbd`; candidate adds
only the package cache code from `66dedaff`. Both use the identical frozen
ABI-compatible boot archive built at `2b168e38`, userland `294727e`, and the same
private published package versions. The ordinary current image also builds;
its new build ID does not admit the old private program packages, so it is not
the measurement image. The HTTP fix prevents setup failures and is present in
both kernels; it does not change package lookup.

Each implementation boots a fresh RAM store and OVMF variables. Ordinary
`pkg install` publishes the same four shapes: 4/128/256 command records plus
one root, then 256 commands plus 801 roots (800 small data dependencies).
The largest index is 1,023,208 bytes, 97.6% of its 1 MiB bound. No limit is
changed. Operations have three warm timed batches in one boot, using the native
monotonic clock; no owned build or packer runs during the timed batches.

Values are median microseconds per operation (minimum–maximum):

| Index bytes | Live URI open before | Live URI open after | Fresh capture before | Fresh capture after |
| ---: | ---: | ---: | ---: | ---: |
| 4,872 | 67.515 (60.774–73.724) | 46.245 (46.182–46.420) | 27.993 (27.699–28.228) | 20.100 (20.086–20.128) |
| 124,904 | 118.164 (118.073–121.146) | 47.333 (46.687–51.048) | 91.303 (91.294–95.808) | 20.474 (20.171–20.583) |
| 248,808 | 193.711 (192.303–195.177) | 50.547 (50.060–50.680) | 164.225 (164.084–164.671) | 21.726 (21.491–23.490) |
| 1,023,208 | 772.155 (746.615–806.121) | 138.153 (136.089–138.178) | 614.357 (609.275–666.868) | 27.242 (26.602–28.525) |

Small batches use 2,000 operations, middle batches 1,000, and large live opens
and captures 300. Every live open includes closing the returned file. Three
warm-up opens precede the timed open batches; capture batches begin after the
live/held open measurements have warmed the index.

The large held-directory open control was 34.969 µs before and 32.535 µs after;
explicit fixed-capture reuse was 20.227 µs before and 20.461 µs after. The latter
omits the selector read and is not a cache-hit equivalent. The small held-open
control varied more (39.462 µs before, 22.868 µs after), so do not assign the
whole small-case difference to the cache. Large live opens still pay selector,
linear root-record search and actual target-open costs. These guest-clock
nested-VM samples are not native-media timings or a latency guarantee.

## Runtime checks

- An absent selector captures/lists an empty set, then ordinary publication is
  immediately visible. Eight ordinary 100-package installs succeed in both runs.
- One process holds a command capture, selected root and tree from set 11.
  Another process publishes set 12. Fresh live reads return `y`; held and selected
  trees retain `x`; a reused explicit capture retains set 11. Fixed-source reuse
  of a different set refuses BAD_REQUEST. Command/root modes remain distinct:
  old capture has 256 commands, fresh capture 4, live roots 801. Returned tree
  rights remain read-only.
- Two disposable native RAM volumes at the same set number enumerate different
  projections. Cross-origin capture reuse and binding refuse BAD_REQUEST.
- A missing projection fails IO; supplying it under the same active number
  succeeds. A cached index rewrite stays unseen as accepted; after eviction its
  damage fails IO, with the held old capture still readable. Restoring the same
  index recovers. This exercises the same-number file-publication behavior used
  by reindex, not a runtime invocation of `pkg reindex`.
- Malformed selectors and missing/damaged selected projections fail IO; an
  oversized projection fails LIMIT. None falls back to old cached data.
- A LOOKUP-only grant remains unable to enumerate its cached capture (DENIED).
  Removing `current` returns an empty fresh capture without invalidating a held
  set-2 capture.

The last four checks use ordinary native operations on two disposable RAM npfs
roots in an explicit local-console profile. Remote intentionally strips these
arbitrary sources; an initial Remote launch was refused and is excluded. No
product authority rule was changed to run the checks. A private helper supplies
index files; no regression suite or boot self-test is added.

Source review covers cloned-media UUID separation (kernel domains are never
reused within a boot), refcount overflow/unwind, old-reference budget retention,
and allocation-error paths. No cloned-media runtime run or allocation-failure
injection is claimed. Indexes remain in the existing 4 MiB budget, without a new
cache quota, API, filesystem format, submodule change or compiler rebuild.
Rebuild kernel/images; compatible SDK and consumers need no rebuild.

Small raw evidence, helper source, exact hashes and configuration are in
`/shared/present/pkg-root-debt-1497/`; heavy artifacts stay local. Native hardware
was not touched. Bulk HTTP qualification is recorded separately under #1607.

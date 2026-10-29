# Kernel physical-alias query cost — 2026-10-29

## Evidence and attribution

R4 boots with confirmed cleanup but still uses ordinary copying:
PAT0=PAT1=PAT2=PAT3=`0`. There is no reachable uncached policy to reuse.
Evidence: `/shared/present/vela-tearing/flips-native-20261029-r4/results-vela-B-r4/`.
The native phase deltas are register windows376.914ms, inherited alias202.709ms,
GT identity windows187.890ms, GT cache windows188.020ms; copy-backend-ready
is1175.593ms from display preparation start. GT forcewake acknowledgment is
0.580ms, PAT reads0.267ms and GT cleanup0.292ms. These do not identify a
hardware wait of190ms.

Source attribution: display GMD and remaining register windows each perform a
full kernel physical-alias scan; GT's two identity windows share one scan, and
its remaining cache windows share another. Each scalar page query repeatedly
maps/unmaps page-table scratch slots, invalidating them at each descent.
The window pages themselves are mapped once, not repeatedly. GOP inspection
and VT-d admission add their own scans. VA reservation uses a linked free-range
list, not a bitmap; PMM starts its bitmap search at its maintained first candidate.

A disposable, never-merged observer called through GDB measured the components
with in-guest monotonic timestamps. One matched run per version, nested KVM,
invariant TSC, 8GiB guest, same569479680-byte archive. This is not a native boot
measurement. Baseline registered candidate pages157569, fixed157570 (one extra
kernel page); both have20 VM ranges,40960 PMM bitmap words and first candidate831.

| Component | Before | Leaf-batched query |
| --- | ---: | ---: |
| VA reservation, one page | 0.150ms | 0.152ms |
| PMM allocate one frame | 0.139ms | 0.047ms |
| MMIO map one page | 0.148ms | 0.049ms |
| Protect that page RO | 0.141ms | 0.048ms |
| Alias query, one physical span | 175.304ms | 0.626ms |
| Alias query, sixteen spans | 183.257ms | 1.921ms |
| Sixteen warmed reserve/map/RO/unmap/release cycles together | 0.133ms | 0.132ms |

These samples attribute the long path to alias queries, not PMM or VA allocation;
cold single-call differences are not allocator performance claims. A32GiB guest
could not start (`Cannot allocate memory`), so RAM-size scaling on vela was not
measured. No VM-host memory policy was changed.

The correction is in shared MM/paging: scan each present leaf table through one
scratch mapping, skip absent ancestors' complete spans, and release the short
snapshot guard between leaf tables. Every present frame is checked against the
same requested physical extents; the structural owner, reserved-memory/PCI/GOP
exclusions, corruption checks and cleanup remain. No cached alias proof, retained
scratch pointer, allocator change or hardware write is introduced. This removes
per-page scratch invalidations without weakening the mapping checks.

CPU-only QEMU/GDB checks pass for full2048-page RO/NX/UC spans, native-size5063-page
WC backing/zeroing/cleanup, mapped-RAM aliases in either vector order, zero/overflow
refusal, six first/last leaf slots across three tables, byte4095 intersections,
adjacent absent frames, and an empty8GiB reservation. The observer is excluded
from the final kernel. Small evidence is in
`/shared/present/vela-tearing/window-cost-20261029/`. Native startup cost after
this correction and native flips remain unqualified.


This kernel/mm PR splits the query primitive and optimization out of Intel
flip PR #1563 so it can land independently. The component checks above used
containing Intel branch `ad80bdcc5`; the extracted MM function bodies are
identical. This PR has its own current-main build/CI. Merge this PR before
#1563; PAT or display-programming policy is not changed here.

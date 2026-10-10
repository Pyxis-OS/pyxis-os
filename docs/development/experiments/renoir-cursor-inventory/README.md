# Renoir cursor inventory and software baseline

Task 1 of the [accepted cursor track](../../../wip/renoir-hardware-cursor.md),
2026-10-10. Read-only inventory, optional software counters and the owner-run native
software baseline are complete. Hardware cursor support remains unimplemented. No cursor
writes, cursor backing allocation or new GPU write allowlist is implemented.
Existing opt-in flips still perform their qualified writes.

## Inputs and build

Fresh main `f4a79b69` after #665. Before code edits, a full source image with
`LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 DISPLAY_FLIP_METRICS=1
 DISPLAY_TIMING=off` was built and sealed in `build/cursor-baseline-main/`.
Pins: userland `6e12b5f4`, ports `7c33f3ff`, fs `b427df29`, lwIP `a1aadb91`.
Compiler: existing LLVM23.1.3 builder, no compiler rebuild or dependency change.

Code commit `2c2cfcba` adds the opt-in probe. The local native set is
`build/cursor-task1-native/`, with kernel/initrd/config, REVISION, INPUTS and
SHA256SUMS. It reuses verified unchanged SDK/userland/ports from the baseline;
the kernel and AMD license payload are rebuilt. These sets are not PXE staging.
The subsequent documentation commit does not change probe code. Luna may build
the submitted PR head instead; record its actual revisions and hashes.

```sh
make -j16 image LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 \
  DISPLAY_FLIP_METRICS=1 DISPLAY_TIMING=off DISPLAY_CURSOR_PROBE=1
```

`DISPLAY_CURSOR_PROBE=1` adds `display.cursor.probe=1`. Default zero adds no
probe output. The flag enables generic compose/copy counters and, only after
qualified flip preparation and full revalidation, reads the active route's
cursor inventory. Absent/refused Renoir reports unavailable without cursor
register reads. No global trace logging is needed.

## Inventory and source conclusions

Before AP startup, two read-only samples record all four inherited HUBP cursor
controls, addresses, size, position, hotspot, stereo, fetch scheduling and power;
DPP cursor/clock controls; OPP cursor locks; selected DPP MPC pending/taken/ACK;
and active OTG update, VSTARTUP, VUPDATE, keepout and position. Instance tables
are distinct. Only with the probe is BAR5 page zero additionally mapped read-only
UC for REFCLK/DP DTO; cursor pages use the existing retained mappings. The
existing three-register flip writer allowlist is unchanged.

Numeric ATOM dce_info v4.1–4.3 crystal metadata is copied from the validated VFCT
image before paging, with bounded table reads. No raw BIOS, EDID, serial or MAC
is logged. REFCLK, global hub timer and every DTO control/phase/modulo are read;
Linux's DCHUB reference arithmetic is labelled a candidate, not clock authority.
Native reference-clock metadata supports the 48 MHz DCHUB candidate below;
pixel source/reference provenance and cursor power-state semantics still need
proof before cursor programming. Nothing programs clocks or powers up a cursor block.

The 64 KiB storage candidate is recomputed after the owned spare extent using
the same memory-policy checks as scanout preparation, including reserved boot/EFI
coverage, the 9 MiB VGA prefix, 16 MiB tail guard and physical alias exclusions.
All four cursor enable bits must be disabled in stable samples before reporting
`UMA-policy-candidate=1`; it performs no reservation, mapping or store. This is
storage-policy evidence, **not old-cursor fetch retirement or complete allocation
authority**. Nonzero inherited addresses/update state need review even if disabled.
The captured prior layout predicts offset `0x10f0000`; never assume that result.

Pinned Linux v6.19.10 commit `271f8eab9590b57a2ff0c8c9eee357723c4a85cb`:
[HUBP table](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c),
[definitions](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h),
[masks](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_sh_mask.h),
[DCCG](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/dccg/dcn20/dcn20_dccg.c#L72-L91),
[hub reference](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/hubbub/dcn20/dcn20_hubbub.c#L560-L590)
and [DTO readback](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/dce/dce_clock_source.c#L1192-L1228).
Complete adapted-source MIT notices and provenance are in the bundled
[AMD NOTICE](../../../../arch/x86_64/amd/NOTICE).

The [cursor lock workaround](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/hwss/dcn10/dcn10_hwseq.c#L2173-L2263)
uses pixel/reference timing and a 70 µs programming estimate. Its VUPDATE
candidate derives from blank-end minus VSTARTUP plus one with Linux's exact
positive/negative normalization; VUPDATE_PARAM alone is not that recipe. This
probe prints the candidate, never schedules a write. Our future validation and
transaction duration has not been qualified against its keepout.

**Task 2 blocker:** the [MPC lock helper](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/mpc/dcn10/dcn10_mpc.c#L460-L465)
and cursor setters do not establish a transaction-correlated latch/disable/old
fetch retirement predicate from pending/taken/ACK. No cursor equivalent of the
flip's earliest-in-use address was found. Stable idle bits in this read-only boot
cannot prove image reuse, capture completion or safe disable; no guessed delay
or register readback replaces that proof. Task 2 needs separate assignment and
resolution of this evidence gap before writes, plus supported native clock,
power/address-domain and keepout evidence.

## Counter interpretation

`display-cursor-probe` emits cumulative totals every 120 successfully presented
frames alongside existing `display-flip-metrics` and `renoir-flip` metrics.
`compositions` counts frames receiving pixels into display storage; CPU scanout
copies count staged/flip surface copies. A timeout dual copy counts twice; a
direct-front path after staging allocation refusal has no separate scanout copy
and is unsuitable for this native baseline. Submitted/confirmed flips come from
the existing flip metrics, so enable `DISPLAY_FLIP_METRICS=1` too.

`reports` counts routed reports, including synthesized button changes, not raw
PS/2 packets. `relative-motion` counts nonzero dx/dy reports; `screen-moves` counts
reports actually changing the bounded physical position (locked relative input
changes no screen position). `moved-frames` counts successive visible **rendered**
snapshots with different positions; several moves can coalesce into one frame.
The pending frame's snapshot stays fixed while later input counters advance.

Use boundary differences, not whole-boot averages: frames/reports/moves and
compose/copy totals divided by delta frames or delta moves. Report ratios with
zero moves as unavailable. Compose/copy spans include clock/preemption overhead; wall elapsed also includes
sleeps and logging. Formatting this sample happens after its compose/copy spans,
but opt-in output can still perturb cadence. These are not CPU profiling or
per-event attribution. Idle output/caret cadence
and the normal periodic base recomposition remain active. More frames during
motion alone does not establish that input caused those compositions.

## Local checks

Clean-main full source image, probe image and default image build with the
existing builder; optional images reuse verified unchanged SDK/userland/ports.
Source/lifetime review confirms no cursor writer or allocation and unchanged
flip write fields and guards. Named register offsets/masks were checked against
the pinned headers; the negative VUPDATE normalization was corrected to Linux's
exact arithmetic. No tests, synthetic MMIO, fault injection or benchmark
infrastructure were added.

Manual QEMU 10.2.2, Q35/nested KVM, max CPU, four CPUs, 2 GiB, fresh OVMF variables,
relative PS/2, VirtIO-SCSI CD/RNG, no NIC/disk/USB/audio. Standard VGA 1280x800
checks the absent-Renoir probe path: OFF/unprepared, all 12 mapping slots zero,
and one gated unavailable line. Four routed motion reports changed physical
position four times but only two visible rendered positions, demonstrating
coalescing; normal frames continue composing/copying while the report counts
are unchanged. This is a counter sanity check, not a native cost result.

Default firmware, Bochs 800x600 and VirtIO boots keep probe counters disabled,
zero and silent, with no Renoir mappings. Shell/tab/ls checks pass, and software pointer/caret rendering is visible on
firmware/Bochs. VirtIO retains its separate cursor state; its QMP framebuffer
dump is not a visual check of that hardware cursor. QEMU has no DCN: it
cannot check the native register, clock, cursor-power or memory evidence.

## Native result — 2026-10-10

Owner-run ThinkPad on AC, 1920x1080 GOP, PS/2 touchpad/TrackPoint, Development
terminal prompt. Luna built PR head `d81c731a8f8d60490efb09eb7fe68c0de5ddbe6b`
with the make flags above. PXE **Cursor inventory** used
`display.flip=1 display.flip.metrics=1 display.cursor.probe=1 display.timing=off
 log.udp=1`, LOG_LEVEL=info. Luna's kernel/initrd hashes were not supplied;
the local artifact hashes are not hashes of this native build. The masked
650-line log remains at `/shared/present/batch2/cursor-670.log`, not in Git.

After a ten-second wait, the owner used **motion, idle, motion, idle**, about
30 seconds each, rather than the requested idle-first order. No clicks or space
changes. Host note places the first window at approximately 12:01:17 UTC;
the log has no timestamps. Moved-frame deltas place motion near frames
1080–2880 and 4680–6480. These complete interior windows exclude transitions.
Every window spans 1560 frames in 26.208–26.209 seconds; differences of cumulative
counters, not whole-boot means, give:

| Window, sample endpoints | Screen moves | Changed-position frames | Compose mean/frame | Copy mean/frame | Compositions/move |
| --- | ---: | ---: | ---: | ---: | ---: |
| Motion 1200–2760 | 2109 | 1547 | 1.199 ms | 0.954 ms | 0.740 |
| Idle 3000–4560 | 0 | 0 | 1.228 ms | 0.953 ms | unavailable |
| Motion 4800–6360 | 2091 | 1542 | 1.218 ms | 0.954 ms | 0.746 |
| Idle 6600–8160 | 0 | 0 | 1.188 ms | 0.940 ms | unavailable |

All four windows have **1560 compositions, 1560 CPU scanout copies and 1560
confirmed flips**, at 59.52 frames/s; motion reports equal screen moves.
Motion coalesces into rendered snapshots. Idle still pays 2.128–2.181 ms/frame
of composition plus copy. The periodic presenter recomposes independently of
pointer activity; this is not a measurement of marginal work caused by a report.
The owner's queued whole-frame-skipping task addresses that separate cost before
hardware cursor task 2. Wall spans include preemption and opt-in metrics overhead.

Throughout, backend=flip, timeouts=0, front=`0xf400000000`, with no FAILED line.
The final available sample is **8160**, beyond the owner's 7680 checkpoint:
8160 submitted/confirmed/compositions/copies, 4861 motion reports/screen moves,
3576 moved frames; cumulative compose mean 1.219 ms, copy mean 0.954 ms.
Flip wait mean/max is 12.092/16.733 ms, validation mean/max 362.737/6442.701 µs,
light-read mean/max 7.230/251.111 µs. Existing reported poll/BSP observation costs
are 0.815/1.136 ms per confirmed frame; they remain separate from compose/copy.

### What the inventory establishes

Two samples confirm the mono route **OTG0 ← OPP0 ← MPCC0 ← DPP0/HUBP0**.
Every HUBP cursor has enable=0, address=0, size=0, position/hotspot=0; inherited
control is `0x01000000`, TMZ/snoop/system fields zero. DPP cursor control is
`0x84` on all instances: enable=0, mode=0, pixel inversion and alpha modulation
bits set, update-pending clear. No firmware cursor image is configured in these
snapshots. The selected MPC pending/taken/ACK, DPP pending and OPP lock are zero;
OTG cursor pending/taken are clear. These are idle observations, not a completed
cursor transaction.

Cursor memory power control is `0x20` on all instances: force=0, disable=0,
LS_MODE=2. Power-state field is 1 on HUBP0 and 0 on HUBP1–3. The pinned masks
identify fields, but do not establish their state enums or prove that enabling
cursor fetch requires no power write. DPP clock-enable (bit 4) is set only for
DPP0 (`0xf0000010`; others `0xf0000000`). Keep the raw distinction without
interpreting other status bits as permission to change clocks or power.

ATOM dce_info v4.3 supplies a 48 MHz crystal. REFCLK=0 and global timer=`0x1001`
match Linux's 48 MHz DCHUB reference candidate. Active DTO control=`0x10`,
phase=138700000, modulo=598875000; the phase agrees numerically with the Fedora
138.700 MHz reference, but the source/reference and modulo relationship still
need qualification. OTG totals are 2080×1111, blank 1108..28, VSTARTUP=13;
Linux's VUPDATE candidate is 16..18. Two advancing position samples and idle
keepout bits do not qualify the future cursor-write execution bound.

The storage-policy candidate passed unchanged UMA exclusion checks:
64 KiB at offset `0x10f0000`, CPU `0x8110f0000`, GPU `0xf4010f0000`, immediately
after the owned spare's rounded extent. It was **not reserved, mapped or stored**.
Disabled zero-address images remove an inherited configured image from this
capture; they do not establish future image-slot retirement or latch semantics.

**Task 1 outcome:** the owner now has the inherited configuration, a checked
storage candidate and repeated software control windows. **Task 2 remains
unassigned and blocked on transaction-correlated latch, disable and old-fetch
retirement evidence**, plus the clock/power/keepout qualifications above. No
native cursor write was performed, so this run cannot observe those transitions.
A stable idle pending/taken/ACK value, register readback or guessed delay cannot
justify image reuse, capture publication or software fallback after disable.

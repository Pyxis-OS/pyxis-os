# Renoir presentation timing: Fedora reference

2026-10-09, ThinkPad T14 Gen 1 AMD, Ryzen 5 PRO 4650U; Fedora 44,
Linux 6.19.10-300.fc44.x86_64. Linux reference for
[accepted presentation step 2](../../../wip/presentation-timing.md), not a Pyxis
implementation. **Complete: panel mode, active OTG, idle/animation counters and WC copy reference.** No Pyxis code, dependency pin or display configuration changed.

## Panel and blank interval

Read DRM's existing connector/encoder/CRTC mode using GETRESOURCES,
GETCONNECTOR (nonzero modes capacity, no forced detection), GETENCODER and
GETCRTC on `/dev/dri/card1`. Only eDP-1 was connected and active; HDMI/DP were
disconnected. Connector 101, encoder 100, CRTC 87; object IDs do not identify OTG.

| Timing | Active | Front porch | Sync | Back porch | Total |
| --- | ---: | ---: | ---: | ---: | ---: |
| Horizontal, pixels | 1920 | 48 | 32 | 80 | 2080 |
| Vertical, lines | 1080 | 3 | 5 | 23 | 1111 |

Pixel clock: **138.700 MHz**. Positive H sync, negative V sync; progressive.
Computed refresh = 138700000 / (2080 × 1111) = **60.020425 Hz**;
line time = 2080 / 138700000 = **14.996395 µs**.
Vertical blank = (1111 − 1080) × line time = **464.888248 µs**.
Mode queries before/after copy measurements were identical. These are DRM mode
values and arithmetic, not measured panel phase or a Pyxis timing capability.

## OTG and panel self-refresh

The owner ran the protected collector; this agent parsed both captures. The first
30-second capture supplied register snapshots; a second coordinated capture
compared static desktop, temporary Wayland animation and return to idle.
GPU is AMD 1002:1636, PCI 0000:07:00.0. Sysfs reports MMIO BAR5
**0xfd300000–0xfd37ffff**, 512 KiB. GPU runtime status was active, power control
on, suspended time zero; no power-control request was issued.

AMD's [DCN2.1 register offsets](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h)
and [Renoir base table](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/renoir_ip_offset.h)
give BASE_IDX 2, segment base 0x34c0 DWORDs. Byte offset =
(base + header offset) × 4, with 0x80-DWORD/0x200-byte instance stride.
The [Renoir resource table](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/resource/dcn21/dcn21_resource.c)
constructs OTG0–3; generated OTG4–5 definitions are not probed.

| Register | OTG0 header offset | OTG0 BAR byte offset | OTG1 | OTG2 | OTG3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| CONTROL | 0x1b41 | 0x14004 | 0x14204 | 0x14404 | 0x14604 |
| V_TOTAL | 0x1b2f | 0x13fbc | 0x141bc | 0x143bc | 0x145bc |
| V_BLANK_START_END | 0x1b36 | 0x13fd8 | 0x141d8 | 0x143d8 | 0x145d8 |
| STATUS_POSITION | 0x1b4a | 0x14028 | 0x14228 | 0x14428 | 0x14628 |
| STATUS_FRAME_COUNT | 0x1b4c | 0x14030 | 0x14230 | 0x14430 | 0x14630 |
| STATUS | 0x1b49 | 0x14024 | 0x14224 | 0x14424 | 0x14624 |

Collector: O_RDONLY BAR5, mmap offset 0x13000/length 0x2000 with PROT_READ,
then const volatile aligned 32-bit loads only. **OTG0 drives the panel:** only its
CONTROL master-enable bit is set (0x80011301 versus 0x80000300 on OTG1–3); its
programmed timing matches the sole active eDP stream, and its measured frame rate
matches the DRM mode. This does not equate DRM CRTC index/object ID with OTG0.

| Active OTG register | Read value / decoded observation |
| --- | --- |
| V_TOTAL | 0x00000456: 1110 + 1 = 1111 lines |
| V_BLANK_START_END | 0x001c0454: start 1108, end 28; modulo-1111 width 31 lines |
| STATUS_POSITION | First/last 0x0560042d / 0x00700274; vertical count observed 0–1110 |
| STATUS_FRAME_COUNT | First/last 0x000809fe / 0x00081107: 526846 → 528647 |
| STATUS | Example 0x00020002 outside blank; V_BLANK observed both 0 and 1 |

The 29.99997-second capture had 109478 samples per OTG. OTG0 advanced 1801 frames;
a fit to frame transitions gives 60.02027 Hz (16.66104 ms/frame), consistent with
DRM. Observed V_BLANK sample fraction 2.79965% versus computed 31/1111 = 2.79028%.
OTG1–3 stayed disabled with zero position/frame counters; their static V_BLANK
bits are not panel timing. Loads are bracketed with CLOCK_MONOTONIC_RAW, not
one atomic snapshot. Per-OTG sample spacing had 276.2 µs median / 825.5 µs maximum;
OTG0 three-read brackets had 6.622 µs median / 105.418 µs maximum. Poll quantization
and occasional scheduling delay limit measured phase precision.

[Field masks](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_sh_mask.h):
V_TOTAL low 15 bits + 1; blank start/end low/high 15 bits; vertical position low
15 bits; frame count low 24 bits; V_BLANK status bit 0.

Avoided debugfs interfaces: amdgpu_dm_dtn_log clears OTG underflow;
psr_state/replay_state/residency queries send firmware commands; amdgpu_regs reads
can request runtime resume. The [cached connector getters](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_debugfs.c)
psr_capability, replay_capability and disallow_edp_enter_psr do not issue hardware
commands. Driver support in psr_capability is psr_feature_enabled; this identifies
configuration, not current residency. Stalled counters alone cannot prove PSR.
Source reference is upstream v6.19.10; Fedora's patched source was not inspected.

Observed module settings: dc=-1, dcfeaturemask=2, dcdebugmask=0, sg_display=-1,
runpm=-1, aspm=-1, fw_load_type=-1. No explicit amdgpu kernel command-line option
was present; those settings were read, not changed. Cached eDP getters report
PSR sink/driver support **no**, Replay sink/driver/config support **no**;
disallow_edp_enter_psr=0. Neither feature is enabled on this link; no current-state
firmware query was needed. The counters also advanced in all three controlled conditions:

| Condition | Analyzed duration (s) | Samples | Frame-count advance | Fitted rate (Hz) |
| --- | ---: | ---: | ---: | ---: |
| Idle before | 6.0000 | 21911 | 360 | 60.0206 |
| Animation | 8.9816 | 33049 | 539 | 60.0207 |
| Idle after | 8.8178 | 32302 | 529 | 60.0209 |

The 30-second repeat started a 480×280 moving-rectangle window at 10.200 s and
closed it at 20.182 s (599 frame callbacks). Analyze 2–8 s for initial idle;
exclude 0.5 s after first draw/before closure for animation, and 0.5 s after
closure/before capture end for final idle. Rates fit hardware frame transitions;
endpoint counts include partial frame periods. All phases covered vertical count
0–1110, with **no observed stalls**; longest detected frame interval was 16.935 ms,
consistent with 16.661 ms plus polling jitter. OTG1–3 remained disabled/zero.
The requested 200 µs sleep produced 272–278 µs median sample spacing in these
phases, with maxima 429–601 µs. Polling can miss/quantize a blank edge; this is not
an interrupt timestamp. Mode remained identical afterward. Vertical wrap was
observed; full 24-bit frame-counter rollover was not exercised.

## RAM to write-combined copy

A scratch host C program allocated one private 1920×1080×32-bit DRM dumb buffer,
mapped it, and destroyed it after measurement. Pitch 7680, size **8,294,400 bytes**.
No DRM master, framebuffer registration/attachment or modeset was requested.
AMDGPU [dumb creation](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_gem.c)
sets CPU_GTT_USWC; [GTT](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c)
and [ordinary Renoir VRAM](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c)
use write-combined mapping. Cache type follows that driver path; PAT was not
independently decoded. Ordinary DRM open/allocation may initialize driver VM
state/clear the private object; the OTG collector itself uses no GPU ioctl.

AC online before/after; existing CPU governor performance (unchanged). Pin only
the measuring process to CPU0. GCC 16.0.1 -O2, glibc 2.43; page-aligned WB source,
source fill and destination faults settled before warmup. Three runs per method,
100 warmups then 1000 timed full copies each, nearest-rank percentiles. End each
copy with **MFENCE/LFENCE** before the CLOCK_MONOTONIC_RAW endpoint; timer/fence
costs included, not subtracted. Readback verified each run. Preliminary SFENCE-only
samples were discarded because they did not establish the required timestamp
ordering. No display animation ran during these copy samples.

| Method | Three P50 values (µs) | Three P95 values (µs) | Maximum (µs) |
| --- | --- | --- | ---: |
| glibc memcpy | 806.506 / 807.517 / 807.398 | 831.262 / 860.307 / 856.200 | 973.740 |
| rep movsq | 859.967 / 860.217 / 860.006 | 947.551 / 947.931 / 950.456 | 1002.204 |

Even the faster method's P50 is **1.74×** the computed blank window; its worst
P95 is **1.85×**. rep movsq's worst P95 is **2.04×**. This Linux reference full
copy does not fit the panel's vertical blank, before wake/interrupt/safety
margins. It is a private-buffer copy reference, not Pyxis front-buffer performance,
composition time, scanout completion or proof of visible tearing. It does not
justify blank-timed full-frame copies; the accepted Pyxis observer still needs
its own counter/firmware handoff qualification.

Local source/captures: `/tmp/pyxis-renoir-reference`; no raw dumps or helper code
in this PR. Counter/metadata collector source SHA-256
4c86332c952e550830cc6a1015fb975ad27e31e915208ee6eacdd79b25a91b3e;
corrected copy source
2af32479fe47858e12cd6ba9910bb65890c1ad08c40b15228d2d4957029e4fc8.
Commands: `cc -O2 -Wall -Wextra reference.c -o reference`, `reference --mode`,
`copy-reference --copy 0`; protected collector is `sudo reference --metadata`
and `sudo reference --otg 30 200`; `collect-paired.sh` coordinates the second
capture/animation. No helper MMIO stores, modeset or clock/power configuration
request was issued. The animation and all collector/benchmark jobs exited.
This qualifies the Linux reference only: firmware/GOP timing and actual Pyxis
front-copy cost still require the separately assigned native observer. No Pyxis
code, build, boot, source pin or host package installation was needed.

/* SPDX-License-Identifier: MIT */
/* Copyright © 2023-2025 Intel Corporation. Adapted register subset; see NOTICE. */
#ifndef INTEL_DISPLAY_REGS_H
#define INTEL_DISPLAY_REGS_H

/* Intel display 30.0, Linux v6.19 05f7e89a: i915 display register headers. */
#define GMD_ID_DISPLAY 0x510a0u
#define DFSM 0x51000u
#define DE_CAP 0x41100u
#define FUSE_STATUS 0x42000u
#define POWER_BIOS 0x45400u
#define POWER_DRIVER 0x45404u
#define POWER_KVMR 0x45408u
#define POWER_DEBUG 0x4540cu
#define DC_STATE_EN 0x45504u
#define DC_TARGET_MASK 0x4000000bu
#define DC3CO_TARGET (1u << 30)
#define DC3CO_STATUS (1u << 29)
#define POWER_REQUEST_MASK 0x0002a80au
#define PG0_FUSE (1u << 27)
#define PIPE_COUNT 4
#define PLANE_COUNT 5
#define SCALER_COUNT 2
#define PIPE_STEP 0x1000u
#define TRANS_ENABLE (1u << 31)
#define PIPE_IRQ_IMR(p) (0x44404u + 0x10u * (p))
#define PIPE_IRQ_IIR(p) (0x44408u + 0x10u * (p))
#define PIPE_IRQ_IER(p) (0x4440cu + 0x10u * (p))
#define PIPE_PLANE1_FLIP_DONE (1u << 3)
#define WELL_REQUEST(index) (2u << (2 * (index)))
#define WELL_STATE(index) (1u << (2 * (index)))
#define PIPE_SCANLINE(p) (0x70000u + PIPE_STEP * (p))
#define TRANS_CONF(p) (0x70008u + PIPE_STEP * (p))
#define PIPE_MISC(p) (0x70030u + PIPE_STEP * (p))
#define FRAME_TIMESTAMP(p) (0x70048u + PIPE_STEP * (p))
#define TRANS_BASE(p) (0x60000u + PIPE_STEP * (p))
#define PIPE_SOURCE(p) (TRANS_BASE(p) + 0x1cu)
#define TRANS_DP2(p) (TRANS_BASE(p) + 0xa0u)
#define TRANS_DDI(p) (TRANS_BASE(p) + 0x400u)
#define PSR_CONTROL(p) (TRANS_BASE(p) + 0x800u)
#define PSR_STATUS(p) (TRANS_BASE(p) + 0x840u)
#define PSR2_CONTROL(p) (TRANS_BASE(p) + 0x900u)
#define PSR2_STATUS(p) (TRANS_BASE(p) + 0x940u)
#define PLANE_BASE(p, n) (0x70180u + PIPE_STEP * (p) + 0x100u * (n))
enum plane_register {
  PLANE_CONTROL = 0, PLANE_STRIDE = 8, PLANE_POSITION = 12, PLANE_SIZE = 16,
  PLANE_SURFACE = 28, PLANE_OFFSET = 36, PLANE_LIVE = 44, PLANE_COLOR = 76,
};
#define PLANE_ENABLE (1u << 31)
#define PLANE_STRIDE_MASK 0xfffu
#define LINEAR_STRIDE_BYTES 64u
#define PLANE_DECRYPT (1u << 2)
#define PLANE_SURFACE_MASK 0xfffff000u
#define PLANE_FORMAT_MASK 0x0f800000u
#define PLANE_FORMAT_8888 0x04000000u
#define PLANE_TILING_MASK 0x1c00u
#define PLANE_COMPRESSION_MASK ((1u << 15) | (1u << 4))
#define PLANE_RGBX (1u << 20)
#define PLANE_ASYNC_FLIP (1u << 9)
#define PLANE_ROTATION_MASK 3u
#define PLANE_FLIP_HORIZONTAL (1u << 8)
#define CURSOR_BASE(p) (0x70080u + PIPE_STEP * (p))
#define SCALER_BASE(p, s) (0x68100u + 0x800u * (p) + 0x100u * (s))
enum scaler_register { SCALER_POSITION = 112, SCALER_SIZE = 116, SCALER_CONTROL = 128 };
#define CURSOR_MODE_MASK 0x27u
#define SCALER_ENABLE (1u << 31)
#define SCALER_BINDING_MASK (7u << 25)
#define JOINER_BASE(p) (0x78000u + 0x200u * (p))
#define JOINER_MASK ((1u << 29) | (1u << 28) | (1u << 23) | (1u << 22) | (1u << 21) | (1u << 20))
#define GGTT_BASE 0x800000u
#define GGTT_TABLE_BYTES 0x800000u
#define GGTT_PAGES (GGTT_TABLE_BYTES / PAGE_SIZE)
#define GGTT_ADDRESS_BYTES UINT64_C(0x100000000)
#define GGTT_PTE_PRESENT UINT64_C(1)
#define GGTT_PTE_DM UINT64_C(2)
#define GGTT_PTE_PAT_MASK (UINT64_C(3) << 52)
#define GGTT_PTE_ADDRESS_LIMIT (UINT64_C(1) << 46)

#endif

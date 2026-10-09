/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef AMD_RENOIR_REGISTERS_H
#define AMD_RENOIR_REGISTERS_H

/* Linux v6.19.10 DCN2.1, Renoir segment 2: BAR5 byte offsets; flip writes have a separate allowlist. */
#define RENOIR_PIPES 4
#define RENOIR_MPCCS 6
#define HUBP_STRIDE 0x370u
#define OTG_STRIDE 0x200u
#define ODM_STRIDE 0x40u
#define MPCC_STRIDE 0x6cu
#define MPC_OUT_STRIDE 0x10u
#define SELECTOR_MASK 0xfu
#define SELECTOR_NONE 0xfu
#define FB_ADDRESS_MASK 0xffffffu
#define ADDRESS_HIGH_MASK 0xffffu
#define VIEWPORT_MASK 0x3fffu
#define FB_ADDRESS_SHIFT 24
#define OTG_MASTER_ENABLE 1u
#define HUBP_BLANK 1u
#define HUBP_DISABLE 4u
#define HUBP_REQUEST_STATUS 0xf0002u
#define HUBP_CLOCK_STATUS 0xf00000u
#define HUBP_VTG_SHIFT 4
#define HUBP_CLOCK_ENABLE 1u
#define OTG_CURRENT_MASTER_ENABLE 0x10000u
#define OTG_INTERLACE_ENABLE 1u
#define OTG_TIMING_MASK 0x7fffu
#define FORMAT_ARGB8888 8u
#define MPCC_OPAQUE_GLOBAL_ALPHA 0x20u
#define MPCC_ALPHA_MODE_MASK 0x30u
#define MPCC_GLOBAL_ALPHA_GAIN_MASK 0xffff0000u
#define MPCC_MODE_MASK 3u
#define MPCC_STEREO_ENABLE 1u
#define SCALER_MODE_MASK 7u
#define SCALER_AUTOCAL_MASK 3u
#define SCALER_CURRENT_BANK 0x1000u
#define RECOUT_ORIGIN_MASK 0x1fff1fffu
#define CROSSBAR_MASK 0xff0000u
#define SYSTEM_APERTURE_MASK 0x3fffffffu
#define SYSTEM_APERTURE_SHIFT 18
#define FORMAT_MASK 0x7fu
#define ROTATION_MASK 0x300u
#define MIRROR_MASK 0x400u
#define SWIZZLE_MASK 0x1fu
#define SURFACE_TMZ 1u
#define SURFACE_DCC 2u
#define FLIP_LOCK 1u
#define FLIP_IMMEDIATE 2u
#define FLIP_PENDING 0x100u
#define FLIP_MASTER_LOCK 0x200u
#define FLIP_STEREO 0x13000u
#define FLIP_GSL 0x100u
#define FLIP_TRIPLE 0x400u
#define DSCL_STRIDE 0x5acu
#define DSCL_MODE 0x106b0u
#define DSCL_AUTOCAL 0x106f4u
#define DSCL_RECOUT_START 0x10708u
#define DSCL_RECOUT_SIZE 0x1070cu
#define DSCL_MPC_SIZE 0x10710u
#define OTG_STEREO 0x14050u
#define OTG_MASTER_LOCK 0x1412cu
#define OTG_GSL 0x14130u
#define MPCC_STEREO 0x11cd4u
#define OTG_STEREO_ENABLE 0x01000000u
#define OTG_LOCK_MASK 0x101u
#define OTG_GSL_MASK 0x8000000fu
#define MPCC_TOP_PASSTHROUGH 1u
#define ODM_INPUT_COUNT 1u
#define ODM_SEG0_SHIFT 8
#define ODM_SEG1_SHIFT 12
#define DMCUB_ENABLE 0x10000u
#define DMCUB_WINDOW_ENABLE 0x80000000u
#define DMCUB_WINDOW_ADDRESS 0x1fffffffu
#define DMCUB_OFFSET_LOW_MASK 0xffffff00u

#define HUBP_CONFIG 0xea94u
#define HUBP_ADDRESS_CONFIG 0xea98u
#define HUBP_TILING 0xea9cu
#define HUBP_VIEWPORT_START 0xeaa4u
#define HUBP_VIEWPORT_SIZE 0xeaa8u
#define HUBP_CONTROL 0xeaccu
#define HUBP_CLOCK 0xead0u
#define HUBP_PITCH 0xeb1cu
#define HUBP_VMID 0xeb24u
#define HUBP_PRIMARY_LOW 0xeb28u
#define HUBP_PRIMARY_HIGH 0xeb2cu
#define HUBP_METADATA_LOW 0xeb48u
#define HUBP_METADATA_HIGH 0xeb4cu
#define HUBP_FLIP_INTERRUPT 0xeb80u
#define HUBP_FLIP_INTERRUPT_ENABLE 5u
#define HUBP_FLIP_INTERRUPT_STATUS 0xf0000u
#define HUBP_SURFACE_CONTROL 0xeb68u
#define HUBP_FLIP_CONTROL 0xeb6cu
#define HUBP_FLIP_CONTROL2 0xeb70u
#define HUBP_INUSE_LOW 0xeb84u
#define HUBP_INUSE_HIGH 0xeb88u
#define HUBP_EARLIEST_LOW 0xeb94u
#define HUBP_EARLIEST_HIGH 0xeb98u
#define HUBP_APERTURE_LOW 0xebdcu
#define HUBP_APERTURE_HIGH 0xebe0u
#define HUBP_TLB 0xec14u
#define HUBP_CROSSBAR 0xecb0u
#define OTG_H_TOTAL 0x13fa8u
#define OTG_H_BLANK 0x13facu
#define OTG_V_TOTAL 0x13fbcu
#define OTG_V_BLANK 0x13fd8u
#define OTG_CONTROL 0x14004u
#define OTG_INTERLACE 0x14010u
#define ODM_SOURCE 0x13e2cu
#define ODM_FORMAT 0x13e30u
#define MPC_OUT_MUX 0x12114u
#define MPCC_TOP 0x11cc4u
#define MPCC_BOTTOM 0x11cc8u
#define MPCC_OPP 0x11cccu
#define MPCC_CONTROL 0x11cd0u
#define MPCC_STATUS 0x11cfcu
#define DCN_FB_BASE 0xe54cu
#define DCN_FB_TOP 0xe550u
#define DCN_FB_OFFSET 0xe554u
#define DCN_CONTEXT_CONTROL 0xe864u
#define DCN_CONTEXT_BASE_HIGH 0xe868u
#define DCN_CONTEXT_BASE_LOW 0xe86cu
#define DCN_CONTEXT_START_HIGH 0xe870u
#define DCN_CONTEXT_START_LOW 0xe874u
#define DCN_CONTEXT_END_HIGH 0xe878u
#define DCN_CONTEXT_END_LOW 0xe87cu
#define DMCUB_CONTROL 0x19d80u
#define DMCUB_SEC_CONTROL 0x19ce0u
#define DMCUB_CW_BASE 0x19c3cu
#define DMCUB_CW_TOP 0x19c5cu
#define DMCUB_CW_OFFSET_LOW 0x19c7cu
#define DMCUB_CW_OFFSET_HIGH 0x19c80u
#define DMCUB_REGION4_OFFSET_LOW 0x19c00u
#define DMCUB_REGION4_OFFSET_HIGH 0x19c04u
#define DMCUB_REGION5_OFFSET_LOW 0x19c08u
#define DMCUB_REGION5_OFFSET_HIGH 0x19c0cu
#define DMCUB_REGION4_TOP 0x19c2cu
#define DMCUB_REGION5_TOP 0x19c30u

/* GC/MMHUB/NBIF direct aliases; no indirect index/data transaction. */
#define GC_FB_OFFSET 0xa5acu
#define MMHUB_FB_BASE 0x6a0b0u
#define MMHUB_FB_TOP 0x6a0b4u
#define NBIF_MEMSIZE 0x378cu

#endif

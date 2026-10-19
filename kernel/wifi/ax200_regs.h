/*	$OpenBSD: if_iwxreg.h,v 1.75 2026/05/28 10:51:52 kirill Exp $	*/

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */


/* Selected layouts/constants, under the BSD alternative above. */
#ifndef KERNEL_WIFI_AX200_REGS_H
#define KERNEL_WIFI_AX200_REGS_H

#include <stdint.h>

#define AX_VENDOR 0x8086
#define AX_PRODUCT 0x2723
#define AX_RX_COUNT 512u
#define AX_TX_COUNT 256u
#define AX_BUFFER_BYTES 4096u
#define AX_FW_MAP_ENTRIES 64u
#define AX_DATA_QUEUE 1u
#define AX_DATA_TID 15u
#define AX_DATA_PATH_GROUP 5u
#define AX_DATA_SCHEDULER 0x17u
#define AX_DATA_TX_OPCODE 0x1cu
#define AX_DATA_FLUSH_OPCODE 0x1eu
#define AX_DATA_PAYLOAD_BYTES 2048u
#define AX_DATA_MPDU_BYTES 1532u
#define AX_DATA_TX_RESPONSE_BYTES 48u
#define AX_DATA_TX_STATUS_OFFSET 40u
#define AX_DATA_TX_SSN_OFFSET 44u
#define AX_DATA_TX_QUEUE_OFFSET 36u
#define AX_DATA_TX_ENCRYPT_DISABLE (UINT32_C(1) << 1)
#define AX_DATA_TX_COMMAND_RATE (UINT32_C(1) << 0)
#define AX_COMMAND_BYTES 324u
#define AX_COMMAND_PAYLOAD 4088u
#define AX_REGISTER_BYTES 0x3000u
#define AX_BOOTSTRAP_BYTES 0x1000u

#define AX_HW_CONFIG 0x000
#define AX_COALESCING 0x004
#define AX_INT 0x008
#define AX_INT_MASK 0x00c
#define AX_RESET 0x020
#define AX_GP 0x024
#define AX_HW_REV 0x028
#define AX_GIO 0x03c
#define AX_CONTEXT_BASE 0x040
#define AX_GP1_CLEAR 0x05c
#define AX_MBOX 0x088
#define AX_RF_ID 0x09c
#define AX_SHADOW 0x0a8
#define AX_LTR 0x0d4
#define AX_CHICKEN 0x100
#define AX_HPET 0x240
#define AX_LINK_POWER 0x250
#define AX_MAC_OTP 0x380
#define AX_MAC_STRAP 0x388
#define AX_PRPH_WRITE_ADDRESS 0x444
#define AX_PRPH_READ_ADDRESS 0x448
#define AX_PRPH_WRITE_DATA 0x44c
#define AX_PRPH_READ_DATA 0x450
#define AX_COMMAND_DOORBELL 0x460
#define AX_RX_DOORBELL 0x1c80
#define AX_FH_CAUSE 0x2800
#define AX_FH_MASK 0x2804
#define AX_HW_CAUSE 0x2808
#define AX_HW_MASK 0x280c
#define AX_AUTOMASK 0x2810
#define AX_RX_IVAR 0x2880
#define AX_IVAR 0x2890
#define AX_IVAR_ALIVE 0x10
#define AX_IVAR_TOP_FATAL 0x13

#define AX_READY UINT32_C(0x00400000)
#define AX_PREPARE UINT32_C(0x08000000)
#define AX_PME UINT32_C(0x10000000)
#define AX_HAP_WAKE UINT32_C(0x00080000)
#define AX_OS_ALIVE UINT32_C(0x20)
#define AX_SW_RESET UINT32_C(0x80)
#define AX_MASTER_DISABLED UINT32_C(0x100)
#define AX_STOP_MASTER UINT32_C(0x200)
#define AX_CLOCK_READY UINT32_C(1)
#define AX_INIT_DONE UINT32_C(4)
#define AX_ACCESS_REQUEST UINT32_C(8)
#define AX_GOING_TO_SLEEP UINT32_C(0x10)
#define AX_RFKILL_WAKE UINT32_C(0x04000000)
#define AX_RADIO_ENABLED UINT32_C(0x08000000)
#define AX_LINK_DISABLED UINT32_C(0x80000000)
#define AX_L0S_DISABLED UINT32_C(2)
#define AX_L1_NO_L0S UINT32_C(0x00800000)
#define AX_HPET_THRESHOLD UINT32_C(0xffff0000)
#define AX_SHADOW_ENABLE UINT32_C(0x800fffff)
#define AX_LTR_250_US UINT32_C(0x88fa88fa)
#define AX_GP1_RFKILL UINT32_C(2)
#define AX_GP1_COMMAND_BLOCKED UINT32_C(4)
#define AX_HW_ALIVE UINT32_C(1)
#define AX_HW_RFKILL UINT32_C(0x80)
#define AX_HW_ERRORS UINT32_C(0x22000068)
#define AX_FH_RX UINT32_C(3)
#define AX_FH_ERROR UINT32_C(0x00200000)
#define AX_FH_ENABLED UINT32_C(0x002b0003)
#define AX_HW_ENABLED UINT32_C(0x6e0001ef)
#define AX_IVAR_NONAUTO 0x80u
#define AX_CHICK 0xa05c00
#define AX_CHICK_MSIX (UINT32_C(1) << 25)
#define AX_CPU_RUN 0xa05c44
#define AX_SECURE_CPU1_STATUS 0xa038c0
#define AX_SECURE_CPU2_STATUS 0xa038c4
#define AX_HPM_DEBUG 0xa03440
#define AX_PERSISTENCE (UINT32_C(1) << 12)
#define AX_WRITE_PROTECT 0xa04d00
#define AX_WFPM_ACCESS (UINT32_C(1) << 12)
#define AX_RX_DMA_CONFIG 0xa09820
#define AX_RX_DMA_STATUS 0xa09808
#define AX_RX_DMA_IDLE (UINT32_C(1) << 31)

#define AX_PCIE_FLAGS 0x02
#define AX_PCIE_CONTROL 0x08
#define AX_PCIE_LINK_CONTROL 0x10
#define AX_PCIE_CONTROL2 0x28
#define AX_PCIE_NO_SNOOP (1u << 11)
#define AX_PCIE_FLR (1u << 15)
#define AX_PCIE_LTR (1u << 10)
#define AX_PCIE_EXTENT 0x2a

#define AX_FW_MAGIC UINT32_C(0x0a4c5749)
#define AX_FW_HEADER_BYTES 88u
#define AX_FW_MAX_BYTES (2u * 1024u * 1024u)
#define AX_FW_FLAGS 18u
#define AX_FW_SECTION 19u
#define AX_FW_PHY 23u
#define AX_FW_API 29u
#define AX_FW_CAPABILITY 30u
#define AX_FW_COMMAND_VERSIONS 48u
#define AX_FW_CPU_SEPARATOR UINT32_C(0xffffcccc)
#define AX_FW_PAGING_SEPARATOR UINT32_C(0xaaaabbbb)
#define AX_FW_API_WORDS 4u
#define AX_FW_CAPABILITY_WORDS 5u
#define AX_FW_VERSION_ENTRIES 256u
#define AX_ALIVE_OPCODE 1u
#define AX_ALIVE_VERSION 6u
#define AX_ALIVE_BYTES 144u
#define AX_ALIVE_OK 0xcafeu
#define AX_PACKET_INVALID UINT32_C(0x55550000)
#define AX_PACKET_LENGTH UINT32_C(0x3fff)
#define AX_PACKET_ALIGN 64u
#define AX_UNSOLICITED 0x80u
#define AX_LONG_GROUP 1u
#define AX_COMMAND_FAILED 0x40u
#define AX_FIRST_TB_BYTES 20u

struct ax_context {
  uint16_t mac_id, version, size, reserved_version;
  uint32_t control, reserved_control;
  uint64_t reserved0;
  uint64_t rx_free, rx_used, rx_status;
  uint64_t command_base;
  uint8_t command_size, reserved_command[7];
  uint32_t reserved1[4];
  uint8_t dump[16], early_debug[16], pnvm[16];
  uint32_t reserved2[16];
  uint64_t umac[AX_FW_MAP_ENTRIES];
  uint64_t lmac[AX_FW_MAP_ENTRIES];
  uint64_t paging[AX_FW_MAP_ENTRIES];
  uint32_t reserved3[16];
} __attribute__((packed));

struct ax_tb {
  uint16_t length;
  uint64_t address;
} __attribute__((packed));

struct ax_tfd {
  uint16_t count;
  struct ax_tb tb[25];
  uint32_t reserved;
} __attribute__((packed));

struct ax_command_header {
  uint8_t opcode, group, index, queue;
  uint16_t length;
  uint8_t reserved, version;
} __attribute__((packed));

struct ax_rx_status {
  uint16_t closed, closed_frames, finished, finished_frames;
  uint32_t reserved;
} __attribute__((packed));

struct ax_firmware_version {
  uint8_t opcode, group, command, notification;
};

_Static_assert(sizeof(struct ax_context) == 1792, "AX200 context layout");
_Static_assert(sizeof(struct ax_tfd) == 256, "AX200 long TFD layout");
_Static_assert(sizeof(struct ax_command_header) == 8, "AX200 wide header");
_Static_assert(sizeof(struct ax_rx_status) == 12, "AX200 RX status");

#endif

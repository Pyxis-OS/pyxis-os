/*	$OpenBSD: if_iwx.c,v 1.230 2026/09/19 19:36:42 stsp Exp $	*/

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Native AX200 transport, using the permitted reference under its BSD terms. */
#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/boot/options.h>
#include <kernel/initrd.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/panic.h>
#include <kernel/pci/msix.h>
#include <kernel/task.h>
#include <kernel/wifi/ax200_transport.h>
#include "ax200_regs.h"

#define AX_US UINT64_C(1000)
#define AX_MS UINT64_C(1000000)
#define AX_SECOND UINT64_C(1000000000)
#define AX_POLL_BUFFERS 8u
#define AX_FW_SECTION_BYTES 32768u
#define AX_FW_DMA_BUDGET (6u * 1024u * 1024u)

struct ax_firmware_section {
  const uint8_t *data;
  size_t bytes;
  struct dma_buffer dma;
};

struct ax_controller {
  struct pci_claim claim;
  struct pci_probe_state probe;
  struct pci_mapping registers;
  struct pci_msix msix;
  struct wifi_status_reply status;
  unsigned pcie;
  bool worker, enabled, prepared, boot_attempted, running, failed, alive, rx_started, ltr;
  bool stopping, command_busy, command_done, command_valid;
  uint8_t command_group, command_opcode, command_index, producer;
  uint8_t address[6];
  bool address_valid;
  uint64_t event;
  struct task_wait *wait;
  uint32_t pending_fh, pending_hw, hw_revision, phy;
  uint32_t api[AX_FW_API_WORDS], capabilities[AX_FW_CAPABILITY_WORDS];
  struct ax_firmware_version versions[AX_FW_VERSION_ENTRIES];
  size_t version_count;
  struct ax_firmware_section sections[3][AX_FW_MAP_ENTRIES];
  unsigned section_count[3];
  struct dma_buffer context, rx_free, rx_used, rx_status, rx_buffers;
  struct dma_buffer tx_descriptors, tx_commands, tx_byte_count, ict, command_large;
  unsigned rx_cursor;
  size_t response_bytes, response_capacity;
  bool response_wanted;
  uint8_t response[AX_BUFFER_BYTES];
};

static struct ax_controller controller;

static uint16_t load16(const void *data)
{
  const uint8_t *p = data;
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t load32(const void *data)
{
  const uint8_t *p = data;
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
    (uint32_t)p[3] << 24;
}

static void require_bsp(void)
{
  KASSERT(cpu_current() == cpu_bsp());
}

static void set_detail(const char *detail)
{
  uint64_t flags = cpu_save_interrupts();
  size_t i = 0;
  for (; i + 1 < sizeof(controller.status.detail) && detail[i]; ++i) {
    controller.status.detail[i] = detail[i];
  }
  memset(controller.status.detail + i, 0, sizeof(controller.status.detail) - i);
  cpu_restore_interrupts(flags);
}

static uint32_t read32(unsigned offset)
{
  KASSERT(controller.registers.address && offset <= controller.registers.bytes - 4);
  return *(volatile uint32_t *)(controller.registers.address + offset);
}

static void write32(unsigned offset, uint32_t value)
{
  KASSERT(controller.registers.address && offset <= controller.registers.bytes - 4);
  *(volatile uint32_t *)(controller.registers.address + offset) = value;
}

static void write8(unsigned offset, uint8_t value)
{
  KASSERT(controller.registers.address && offset < controller.registers.bytes);
  *(volatile uint8_t *)(controller.registers.address + offset) = value;
}

static bool update32(unsigned offset, uint32_t clear, uint32_t set)
{
  uint32_t value = read32(offset);
  if (value == UINT32_MAX) {
    return false;
  }
  write32(offset, (value & ~clear) | set);
  return true;
}

static uint64_t limited_deadline(uint64_t outer, uint64_t interval)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t local = UINT64_MAX - now < interval ? UINT64_MAX : now + interval;
  return local < outer ? local : outer;
}

static void delay_ns(uint64_t interval)
{
  uint64_t deadline = limited_deadline(UINT64_MAX, interval);
  if (controller.worker && cpu_interrupts_enabled() && interval >= AX_MS) {
    kernel_task_sleep_until(deadline);
    return;
  }
  do {
    arch_clock_maintain();
    __asm__ volatile("pause");
  } while (!task_deadline_expired(deadline));
}

static bool poll32(unsigned offset, uint32_t mask, uint32_t expected, uint64_t deadline)
{
  unsigned rounds = 0;
  do {
    uint32_t value = read32(offset);
    if (value == UINT32_MAX) {
      return false;
    }
    if ((value & mask) == expected) {
      return true;
    }
    arch_clock_maintain();
    delay_ns(10 * AX_US);
    if (controller.worker && cpu_interrupts_enabled() && ++rounds % 64 == 0) {
      kernel_task_yield_if_runnable();
    }
  } while (!task_deadline_expired(deadline));
  return false;
}

static bool nic_acquire(uint64_t deadline)
{
  if (!update32(AX_GP, 0, AX_ACCESS_REQUEST)) {
    return false;
  }
  delay_ns(2 * AX_US);
  bool ready = poll32(AX_GP, AX_CLOCK_READY | AX_GOING_TO_SLEEP, AX_CLOCK_READY,
      limited_deadline(deadline, 150 * AX_MS));
  if (!ready) {
    update32(AX_GP, AX_ACCESS_REQUEST, 0);
  }
  return ready;
}

static void nic_release(void)
{
  update32(AX_GP, AX_ACCESS_REQUEST, 0);
}

static uint32_t prph_read(uint32_t address)
{
  write32(AX_PRPH_READ_ADDRESS, (address & UINT32_C(0x000fffff)) | UINT32_C(0x03000000));
  dma_full_barrier();
  return read32(AX_PRPH_READ_DATA);
}

static void prph_write(uint32_t address, uint32_t value)
{
  write32(AX_PRPH_WRITE_ADDRESS, (address & UINT32_C(0x000fffff)) | UINT32_C(0x03000000));
  dma_write_barrier();
  write32(AX_PRPH_WRITE_DATA, value);
}

static bool ready_once(uint64_t deadline)
{
  if (!update32(AX_HW_CONFIG, 0, AX_READY) ||
      !poll32(AX_HW_CONFIG, AX_READY, AX_READY, limited_deadline(deadline, 50 * AX_US))) {
    return false;
  }
  return update32(AX_MBOX, 0, AX_OS_ALIVE);
}

static bool prepare_card(uint64_t deadline)
{
  if (ready_once(deadline)) {
    return true;
  }
  if (!update32(AX_LINK_POWER, 0, AX_LINK_DISABLED)) {
    return false;
  }
  delay_ns(AX_MS);
  uint64_t ready_deadline = limited_deadline(deadline, 150 * AX_MS);
  for (unsigned tries = 0; tries < 10 && !task_deadline_expired(deadline); ++tries) {
    if (!update32(AX_HW_CONFIG, 0, AX_PREPARE)) {
      return false;
    }
    do {
      if (ready_once(deadline)) {
        return true;
      }
      delay_ns(200 * AX_US);
    } while (!task_deadline_expired(ready_deadline));
    delay_ns(25 * AX_MS);
  }
  return false;
}

static bool clear_persistence(void)
{
  uint32_t value = prph_read(AX_HPM_DEBUG);
  if (value == UINT32_MAX) {
    return false;
  }
  if (value == UINT32_C(0xa5a5a5a0) || !(value & AX_PERSISTENCE)) {
    return true;
  }
  uint32_t protection = prph_read(AX_WRITE_PROTECT);
  if (protection == UINT32_MAX || (protection & AX_WFPM_ACCESS)) {
    return false;
  }
  prph_write(AX_HPM_DEBUG, value & ~AX_PERSISTENCE);
  return !(prph_read(AX_HPM_DEBUG) & AX_PERSISTENCE);
}

static bool apm_init(uint64_t deadline)
{
  if (!update32(AX_CHICKEN, 0, AX_L1_NO_L0S) ||
      !update32(AX_HPET, 0, AX_HPET_THRESHOLD) ||
      !update32(AX_HW_CONFIG, 0, AX_HAP_WAKE) ||
      !update32(AX_GIO, 0, AX_L0S_DISABLED) ||
      !update32(AX_GP, 0, AX_INIT_DONE)) {
    return false;
  }
  return poll32(AX_GP, AX_CLOCK_READY, AX_CLOCK_READY,
      limited_deadline(deadline, 25 * AX_MS));
}

static bool software_reset(void)
{
  if (!update32(AX_RESET, 0, AX_SW_RESET)) {
    return false;
  }
  delay_ns(5 * AX_MS);
  return read32(AX_RESET) != UINT32_MAX;
}

static bool device_halt(uint64_t deadline, bool rx)
{
  bool idle = !rx;
  if (rx && nic_acquire(deadline)) {
    prph_write(AX_RX_DMA_CONFIG, 0);
    uint64_t idle_deadline = limited_deadline(deadline, 10 * AX_MS);
    do {
      uint32_t value = prph_read(AX_RX_DMA_STATUS);
      if (value != UINT32_MAX && (value & AX_RX_DMA_IDLE)) {
        idle = true;
        break;
      }
      delay_ns(10 * AX_US);
    } while (!task_deadline_expired(idle_deadline));
    nic_release();
  }
  bool configured = update32(AX_GP, AX_ACCESS_REQUEST, 0) &&
    update32(AX_LINK_POWER, 0, AX_LINK_DISABLED) &&
    update32(AX_HW_CONFIG, 0, AX_PREPARE | AX_PME);
  delay_ns(AX_MS);
  configured = update32(AX_LINK_POWER, AX_LINK_DISABLED, 0) && configured;
  delay_ns(5 * AX_MS);
  bool master = update32(AX_RESET, 0, AX_STOP_MASTER) &&
    poll32(AX_RESET, AX_MASTER_DISABLED, AX_MASTER_DISABLED,
        limited_deadline(deadline, 100 * AX_US));
  bool init_clear = update32(AX_GP, AX_INIT_DONE, 0);
  bool reset = software_reset();
  /* The reset delay alone does not establish completion; the master ACK does. */
  return idle && configured && master && init_clear && reset;
}

static bool prepare_coherence(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct pci_claim *claim = &controller.claim;
  struct pci_address address = claim->device->address;
  bool valid = !(pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER);
  uint16_t control = pci_read16(address, controller.pcie + AX_PCIE_CONTROL);
  if (control == UINT16_MAX || (control & AX_PCIE_FLR)) {
    valid = false;
  }
  uint16_t expected = control & ~AX_PCIE_NO_SNOOP;
  if (valid && control != expected) {
    pci_write16(claim, controller.pcie + AX_PCIE_CONTROL, expected);
  }
  valid = valid && pci_read16(address, controller.pcie + AX_PCIE_CONTROL) == expected &&
    !(pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER);
  uint16_t control2 = pci_read16(address, controller.pcie + AX_PCIE_CONTROL2);
  valid = valid && control2 != UINT16_MAX;
  if (valid) {
    controller.ltr = !!(control2 & AX_PCIE_LTR);
  }
  cpu_restore_interrupts(flags);
  return valid;
}

static bool set_bus_master(bool enabled)
{
  uint64_t flags = cpu_save_interrupts();
  struct pci_address address = controller.claim.device->address;
  uint16_t command = pci_read16(address, PCI_COMMAND);
  uint16_t expected = enabled ? command | PCI_COMMAND_MASTER : command & ~PCI_COMMAND_MASTER;
  bool valid = command != UINT16_MAX && (command & PCI_COMMAND_INTX_DISABLE) &&
    (command & PCI_COMMAND_MEMORY);
  if (valid) {
    pci_write16(&controller.claim, PCI_COMMAND, expected);
    valid = pci_read16(address, PCI_COMMAND) == expected;
  }
  cpu_restore_interrupts(flags);
  return valid;
}

static bool allocate_dma(struct dma_buffer *buffer, size_t bytes, bool firmware)
{
  if (dma_buffer_allocate(buffer, bytes) != MM_OK) {
    return false;
  }
  controller.status.dma_bytes += buffer->bytes;
  if (firmware) {
    controller.status.firmware_dma_bytes += buffer->bytes;
  }
  return true;
}

static bool parse_firmware(void)
{
  struct initrd_file file;
  if (initrd_lookup("firmware/ax200-wifi/iwlwifi-cc-a0-77.ucode", &file) != INITRD_OK ||
      file.size < AX_FW_HEADER_BYTES || file.size > AX_FW_MAX_BYTES) {
    return false;
  }
  const uint8_t *data = file.data;
  if (load32(data) || load32(data + 4) != AX_FW_MAGIC || load32(data + 72) != 77) {
    return false;
  }
  bool phy = false, versions = false;
  unsigned group = 0;
  size_t offset = AX_FW_HEADER_BYTES, section_bytes = 0;
  while (offset < file.size) {
    if (file.size - offset < 8) {
      return false;
    }
    uint32_t type = load32(data + offset), bytes = load32(data + offset + 4);
    offset += 8;
    size_t padded = ((size_t)bytes + 3) & ~(size_t)3;
    if (padded < bytes || padded > file.size - offset) {
      return false;
    }
    const uint8_t *payload = data + offset;
    switch (type) {
    case AX_FW_SECTION: {
      if (bytes < 4) {
        return false;
      }
      uint32_t address = load32(payload);
      if (address == AX_FW_CPU_SEPARATOR || address == AX_FW_PAGING_SEPARATOR) {
        if (bytes != 8 || !controller.section_count[group] ||
            (address == AX_FW_CPU_SEPARATOR ? group != 0 : group != 1)) {
          return false;
        }
        ++group;
        break;
      }
      size_t length = bytes - 4;
      unsigned index = controller.section_count[group];
      if (!length || length > AX_FW_SECTION_BYTES || index == AX_FW_MAP_ENTRIES ||
          section_bytes > AX_FW_DMA_BUDGET - length) {
        return false;
      }
      controller.sections[group][index].data = payload + 4;
      controller.sections[group][index].bytes = length;
      ++controller.section_count[group];
      section_bytes += length;
      break;
    }
    case AX_FW_PHY:
      if (bytes != 4 || phy) {
        return false;
      }
      controller.phy = load32(payload);
      phy = true;
      break;
    case AX_FW_API:
    case AX_FW_CAPABILITY: {
      if (bytes != 8) {
        return false;
      }
      uint32_t index = load32(payload);
      unsigned words = type == AX_FW_API ? AX_FW_API_WORDS : AX_FW_CAPABILITY_WORDS;
      uint32_t *bits = type == AX_FW_API ? controller.api : controller.capabilities;
      if (index >= words) {
        return false;
      }
      bits[index] |= load32(payload + 4);
      break;
    }
    case AX_FW_COMMAND_VERSIONS:
      if (versions || !bytes || bytes % sizeof(struct ax_firmware_version) ||
          bytes > sizeof(controller.versions)) {
        return false;
      }
      controller.version_count = bytes / sizeof(struct ax_firmware_version);
      memcpy(controller.versions, payload, bytes);
      /* The pinned table contains separate request/notification entries for
       * an unrelated command. Keep the reference first-match lookup order. */
      versions = true;
      break;
    default:
      /* Optional metadata never authorizes debug DMA or another image family. */
      break;
    }
    offset += padded;
  }
  return group == 2 && controller.section_count[2] && phy && versions &&
    ax200_command_version(0, AX_ALIVE_OPCODE, true) == AX_ALIVE_VERSION &&
    ax200_firmware_api(48);
}

static bool allocate_resources(void)
{
  if (!allocate_dma(&controller.context, sizeof(struct ax_context), false) ||
      !allocate_dma(&controller.rx_free, AX_RX_COUNT * sizeof(uint64_t), false) ||
      !allocate_dma(&controller.rx_used, AX_RX_COUNT * sizeof(uint32_t), false) ||
      !allocate_dma(&controller.rx_status, sizeof(struct ax_rx_status), false) ||
      !allocate_dma(&controller.rx_buffers, AX_RX_COUNT * AX_BUFFER_BYTES, false) ||
      !allocate_dma(&controller.tx_descriptors, AX_TX_COUNT * sizeof(struct ax_tfd), false) ||
      !allocate_dma(&controller.tx_commands, AX_TX_COUNT * AX_COMMAND_BYTES, false) ||
      !allocate_dma(&controller.tx_byte_count, (AX_TX_COUNT + 64) * sizeof(uint16_t), false) ||
      !allocate_dma(&controller.ict, AX_BUFFER_BYTES, false) ||
      !allocate_dma(&controller.command_large, AX_BUFFER_BYTES, false)) {
    return false;
  }
  uint64_t *rx_descriptors = (void *)controller.rx_free.address;
  for (unsigned i = 0; i < AX_RX_COUNT; ++i) {
    rx_descriptors[i] = (controller.rx_buffers.physical + (uint64_t)i * AX_BUFFER_BYTES) | i;
  }
  struct ax_context *context = (void *)controller.context.address;
  for (unsigned group = 0; group < 3; ++group) {
    for (unsigned i = 0; i < controller.section_count[group]; ++i) {
      struct ax_firmware_section *section = &controller.sections[group][i];
      if (!allocate_dma(&section->dma, section->bytes, true)) {
        return false;
      }
      memcpy((void *)section->dma.address, section->data, section->bytes);
      /* Packed maps are little-endian on the native x86 target. */
      if (group == 0) {
        context->lmac[i] = section->dma.physical;
      } else if (group == 1) {
        context->umac[i] = section->dma.physical;
      } else {
        context->paging[i] = section->dma.physical;
      }
    }
  }
  context->mac_id = (uint16_t)controller.hw_revision;
  context->size = sizeof(*context) / 4;
  context->control = UINT32_C(0x990);
  context->rx_free = controller.rx_free.physical;
  context->rx_used = controller.rx_used.physical;
  context->rx_status = controller.rx_status.physical;
  context->command_base = controller.tx_descriptors.physical;
  context->command_size = 5;
  controller.status.rx_entries = AX_RX_COUNT;
  controller.status.rx_buffers = AX_RX_COUNT;
  controller.status.command_entries = AX_TX_COUNT;
  controller.status.command_queues = 1;
  return true;
}

static bool discover_pcie(void)
{
  struct pci_claim *claim = &controller.claim;
  struct pci_address address = claim->device->address;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(address, offset) != PCI_CAP_EXPRESS) {
      continue;
    }
    if (controller.pcie || !pci_capability_fits(claim, offset, AX_PCIE_EXTENT)) {
      return false;
    }
    uint16_t flags = pci_read16(address, offset + AX_PCIE_FLAGS);
    unsigned version = flags & 15, type = (flags >> 4) & 15;
    if ((version != 1 && version != 2) || type != 0 || (flags & ((1u << 8) | (1u << 14)))) {
      return false;
    }
    controller.pcie = offset;
  }
  if (!controller.pcie) {
    return false;
  }
  uint16_t control2 = pci_read16(address, controller.pcie + AX_PCIE_CONTROL2);
  if (control2 == UINT16_MAX) {
    return false;
  }
  controller.ltr = !!(control2 & AX_PCIE_LTR);
  return true;
}

static bool register_overlap(const struct pci_region *region)
{
  if (region->bar != 0) {
    return false;
  }
  uint64_t start = region->offset, end = start + region->length;
  return (start < AX_RX_DOORBELL + 4 && end > 0) ||
    (start < AX_IVAR + 0x2f && end > AX_FH_CAUSE);
}

bool ax200_transport_prepare(const struct boot_info *boot)
{
  require_bsp();
  KASSERT(!cpu_interrupts_enabled() && !controller.claim.device);
  struct pci_device *device = NULL;
  enum pci_selection selection = pci_select_device(AX_VENDOR, AX_PRODUCT, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    set_detail(selection == PCI_SELECTION_ABSENT ? "AX200 absent" : "AX200 inventory unavailable or ambiguous");
    return false;
  }
  controller.status.flags = WIFI_PRESENT;
  controller.status.controller_id = 1;
  controller.status.pci_bus = device->address.bus;
  controller.status.pci_device = device->address.device;
  controller.status.pci_function = device->address.function;
  controller.enabled = boot_options_get()->wifi_ax200;
  if (!controller.enabled) {
    set_detail("AX200 preparation disabled by boot policy");
    return false;
  }
  controller.status.flags |= WIFI_ENABLED;
  if (!parse_firmware()) {
    ax200_transport_disable("Pinned AX200 firmware container unavailable or unsupported");
    return false;
  }
  if (!pci_reserve_device(device, &controller.claim)) {
    ax200_transport_disable("AX200 PCI reservation refused");
    return false;
  }
  if (!pci_begin_d3hot_mmio_probe(&controller.claim, &controller.probe) ||
      pci_map_bootstrap_bar(&controller.claim, 0, 0, AX_BOOTSTRAP_BYTES, boot,
          &controller.registers) != MM_OK) {
    /* No controller writes occurred; only the reversible ordinary probe unwinds. */
    if (pci_restore_mmio_probe(&controller.claim, &controller.probe)) {
      pci_cancel_reservation(&controller.claim);
    }
    ax200_transport_disable("AX200 MMIO handoff refused; retained if power changed");
    return false;
  }
  uint64_t deadline = limited_deadline(UINT64_MAX, AX_SECOND);
  if (!prepare_card(deadline) || !clear_persistence() || !apm_init(deadline) ||
      !device_halt(deadline, true) || !pci_complete_claim(&controller.claim)) {
    ax200_transport_disable("AX200 boot halt or master-disable acknowledgement failed; retained");
    return false;
  }
  struct pci_address address = device->address;
  uint16_t command = pci_read16(address, PCI_COMMAND);
  pci_write16(&controller.claim, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
  if (pci_read16(address, PCI_COMMAND) & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
    ax200_transport_disable("AX200 BAR decode disable failed; retained");
    return false;
  }
  bool sized = pci_size_bars(&controller.claim);
  pci_write16(&controller.claim, PCI_COMMAND, (command & ~(PCI_COMMAND_IO | PCI_COMMAND_MASTER)) |
      PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE);
  uint16_t observed = pci_read16(address, PCI_COMMAND);
  unsigned command_mask = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE;
  if (!sized || controller.claim.bars[0].bytes < AX_REGISTER_BYTES ||
      (observed & command_mask) != (PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE) ||
      !discover_pcie() || !prepare_coherence()) {
    ax200_transport_disable("AX200 BAR or PCIe coherence preparation failed; retained");
    return false;
  }
  pci_unmap(&controller.claim, &controller.registers);
  if (pci_map_bar(&controller.claim, 0, 0, AX_REGISTER_BYTES, boot, &controller.registers) != MM_OK ||
      !pci_msix_discover(&controller.claim, &controller.msix) ||
      register_overlap(&controller.msix.table) || register_overlap(&controller.msix.pba) ||
      pci_msix_map(&controller.msix, boot) != MM_OK ||
      !pci_msix_prepare(&controller.msix, APIC_AX200_VECTOR)) {
    ax200_transport_disable("AX200 exact BAR/MSI-X preparation failed; retained");
    return false;
  }
  controller.hw_revision = read32(AX_HW_REV);
  if (controller.hw_revision == UINT32_MAX || !allocate_resources()) {
    ax200_transport_disable("AX200 firmware/DMA reservation failed; retained");
    return false;
  }
  /* PCI retry-timeout byte only; leave neighboring config fields unchanged. */
  pci_write8(&controller.claim, 0x41, 0);
  controller.prepared = true;
  controller.status.flags |= WIFI_PREPARED;
  set_detail("AX200 resources prepared; firmware not started");
  kinfo("wifi: AX200 DMA=%lu firmware=%lu RX=%u command entries=%u queues=1; prepared\n",
      controller.status.dma_bytes, controller.status.firmware_dma_bytes, AX_RX_COUNT, AX_TX_COUNT);
  return true;
}

static bool configure_interrupts(uint64_t deadline)
{
  static const uint8_t causes[] = {
    0, 1, 3, 5, 0x10, 0x11, 0x12, 0x15, 0x16, 0x17, 0x18,
    0x29, 0x2a, 0x2b, 0x2d, 0x2e,
  };
  if (!nic_acquire(deadline)) {
    return false;
  }
  prph_write(AX_CHICK, AX_CHICK_MSIX);
  uint32_t chick = prph_read(AX_CHICK);
  nic_release();
  if (chick == UINT32_MAX || !(chick & AX_CHICK_MSIX)) {
    return false;
  }
  write32(AX_FH_MASK, UINT32_MAX);
  write32(AX_HW_MASK, UINT32_MAX);
  write8(AX_RX_IVAR, AX_IVAR_NONAUTO);
  write8(AX_RX_IVAR + 1, AX_IVAR_NONAUTO);
  for (size_t i = 0; i < sizeof(causes); ++i) {
    write8(AX_IVAR + causes[i], AX_IVAR_NONAUTO);
  }
  write32(AX_FH_CAUSE, UINT32_MAX);
  write32(AX_HW_CAUSE, UINT32_MAX);
  /* Keep failure/rfkill indications observable while waiting for firmware. */
  write32(AX_FH_MASK, ~AX_FH_ENABLED);
  write32(AX_HW_MASK, ~AX_HW_ENABLED);
  write32(AX_AUTOMASK, 1);
  return !(read32(AX_FH_MASK) & AX_FH_ENABLED) && !(read32(AX_HW_MASK) & AX_HW_ENABLED);
}

static bool configure_nic(void)
{
  uint32_t normalized = (controller.hw_revision & UINT32_C(0xfff0)) |
    ((controller.hw_revision & 3) << 2);
  uint32_t configuration = normalized & 15;
  configuration |= (controller.phy & 3) << 10;
  configuration |= ((controller.phy >> 2) & 3) << 14;
  configuration |= ((controller.phy >> 4) & 3) << 12;
  if (!update32(AX_HW_CONFIG, UINT32_C(0xff0f), configuration) ||
      !update32(AX_SHADOW, 0, AX_SHADOW_ENABLE)) {
    return false;
  }
  write8(AX_COALESCING, 0x40);
  return true;
}

static bool valid_address(const uint8_t *address)
{
  static const uint8_t zero[6] = {0};
  static const uint8_t reserved[6] = {0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00};
  return !(address[0] & 1) && memcmp(address, zero, 6) && memcmp(address, reserved, 6);
}

static void capture_address(uint64_t deadline)
{
  if (!nic_acquire(deadline)) {
    return;
  }
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    unsigned base = attempt ? AX_MAC_OTP : AX_MAC_STRAP;
    uint32_t first = read32(base), second = read32(base + 4);
    uint8_t *address = controller.address;
    address[0] = first >> 24;
    address[1] = first >> 16;
    address[2] = first >> 8;
    address[3] = first;
    address[4] = second >> 8;
    address[5] = second;
    if (first != UINT32_MAX && second != UINT32_MAX && valid_address(address)) {
      controller.address_valid = true;
      break;
    }
  }
  nic_release();
}

void ax200_transport_notify(void)
{
  require_bsp();
  uint64_t flags = cpu_save_interrupts();
  ++controller.event;
  struct task_wait *wait = controller.wait;
  controller.wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
  cpu_restore_interrupts(flags);
}

uint64_t ax200_transport_event(void)
{
  require_bsp();
  uint64_t flags = cpu_save_interrupts();
  uint64_t event = controller.event;
  cpu_restore_interrupts(flags);
  return event;
}

void ax200_transport_wait(uint64_t observed, uint64_t deadline_ns)
{
  require_bsp();
  KASSERT(controller.worker && cpu_interrupts_enabled());
  uint64_t flags = cpu_save_interrupts();
  struct task_wait *wait = task_wait_prepare();
  controller.wait = wait;
  /* Register first, then recheck the durable event under the IRQ exclusion. */
  if (controller.event == observed && !controller.failed && !task_deadline_expired(deadline_ns)) {
    task_wait_sleep_until(wait, deadline_ns);
  }
  controller.wait = NULL;
  cpu_restore_interrupts(flags);
}

bool ax200_transport_interrupt(void)
{
  require_bsp();
  KASSERT(!cpu_interrupts_enabled());
  if (!controller.prepared || !controller.running) {
    return false;
  }
  uint32_t fh = read32(AX_FH_CAUSE), hw = read32(AX_HW_CAUSE);
  if (fh == UINT32_MAX || hw == UINT32_MAX) {
    controller.pending_hw |= AX_HW_ERRORS;
  } else {
    write32(AX_FH_CAUSE, fh);
    write32(AX_HW_CAUSE, hw);
    controller.pending_fh |= fh & AX_FH_ENABLED;
    controller.pending_hw |= hw & AX_HW_ENABLED;
  }
  ++controller.status.interrupts;
  ax200_transport_notify();
  /* AUTOMASK remains held until the worker has serviced the pending causes. */
  return true;
}

void ax200_transport_disable(const char *reason)
{
  require_bsp();
  uint64_t flags = cpu_save_interrupts();
  bool first = !controller.failed;
  controller.failed = true;
  controller.status.flags = (controller.status.flags & ~WIFI_READY) | WIFI_FAILED;
  if (controller.registers.address) {
    write32(AX_INT_MASK, 0);
    if (controller.registers.bytes >= AX_REGISTER_BYTES) {
      write32(AX_FH_MASK, UINT32_MAX);
      write32(AX_HW_MASK, UINT32_MAX);
    }
  }
  if (controller.msix.table.mapping.address) {
    pci_msix_disable(&controller.msix);
  }
  if (first) {
    set_detail(reason);
  }
  ax200_transport_notify();
  cpu_restore_interrupts(flags);
  if (first) {
    kwarn("wifi: %s\n", reason);
  }
}

bool ax200_transport_stop(uint64_t deadline_ns)
{
  require_bsp();
  KASSERT(controller.worker && cpu_interrupts_enabled());
  if (!controller.prepared || controller.stopping) {
    return false;
  }
  controller.stopping = true;
  uint64_t flags = cpu_save_interrupts();
  write32(AX_FH_MASK, UINT32_MAX);
  write32(AX_HW_MASK, UINT32_MAX);
  bool masked = pci_msix_disable(&controller.msix);
  cpu_restore_interrupts(flags);
  bool halted = device_halt(deadline_ns, controller.running);
  bool fenced = set_bus_master(false);
  controller.running = false;
  controller.alive = false;
  controller.status.flags &= ~WIFI_READY;
  controller.stopping = false;
  if (!masked || !halted || !fenced) {
    ax200_transport_disable("AX200 stop acknowledgement uncertain; DMA retained until reboot");
    set_detail("AX200 stop acknowledgement uncertain; DMA retained until reboot");
    return false;
  }
  if (!controller.failed) {
    set_detail("AX200 stopped; DMA retained until reboot");
  }
  return true;
}

static bool terminal_failure(const char *reason)
{
  ax200_transport_disable(reason);
  if (controller.worker && controller.prepared && !controller.stopping) {
    ax200_transport_stop(limited_deadline(UINT64_MAX, 200 * AX_MS));
  }
  return false;
}

static void publish_rx(void)
{
  uint64_t *descriptors = (void *)controller.rx_free.address;
  for (unsigned i = 0; i < AX_RX_COUNT; ++i) {
    descriptors[i] = (controller.rx_buffers.physical + (uint64_t)i * AX_BUFFER_BYTES) | i;
  }
  dma_full_barrier();
  write32(AX_RX_DOORBELL, 8);
  controller.rx_started = true;
}

static unsigned rx_closed(void)
{
  volatile struct ax_rx_status *status = (void *)controller.rx_status.address;
  unsigned closed = status->closed;
  dma_read_barrier();
  return (closed & 0xfff) & (AX_RX_COUNT - 1);
}

bool ax200_transport_drained(void)
{
  require_bsp();
  return !controller.running || (controller.rx_started && rx_closed() == controller.rx_cursor);
}

static bool receive_buffer(const uint8_t *buffer)
{
  for (size_t offset = 0; offset + 8 <= AX_BUFFER_BYTES;) {
    const uint8_t *packet = buffer + offset;
    uint32_t flags = load32(packet);
    uint8_t opcode = packet[4], group = packet[5], index = packet[6], queue = packet[7];
    if (flags == AX_PACKET_INVALID ||
        (!opcode && !group && !index && !(queue & ~AX_UNSOLICITED))) {
      break;
    }
    size_t length = flags & AX_PACKET_LENGTH;
    if (length < 4 || length > AX_BUFFER_BYTES - offset - 4) {
      return terminal_failure("AX200 malformed RX notification; DMA retained");
    }
    size_t bytes = length - 4;
    const uint8_t *payload = packet + 8;
    ++controller.status.received_packets;
    if (!group && opcode == 2) {
      return terminal_failure("AX200 firmware rejected a command; DMA retained");
    }
    if (queue & AX_UNSOLICITED) {
      if (group == 0 && opcode == AX_ALIVE_OPCODE) {
        if (controller.alive || bytes != AX_ALIVE_BYTES || load16(payload) != AX_ALIVE_OK) {
          return terminal_failure("AX200 ALIVE layout/status rejected; DMA retained");
        }
        controller.alive = true;
      }
      ax200_firmware_notify(group, opcode, payload, bytes);
    } else {
      bool expected_group = group == controller.command_group ||
        (!controller.command_group && group == AX_LONG_GROUP);
      if (queue != 0 || !controller.command_busy || controller.command_done ||
          index != controller.command_index || opcode != controller.command_opcode ||
          !expected_group || (group & AX_COMMAND_FAILED)) {
        return terminal_failure("AX200 unexpected command response identity; DMA retained");
      }
      if (bytes > AX_BUFFER_BYTES ||
          (controller.response_wanted && bytes > controller.response_capacity)) {
        return terminal_failure("AX200 command response exceeds reservation; DMA retained");
      }
      if (controller.response_wanted) {
        memcpy(controller.response, payload, bytes);
      }
      controller.response_bytes = bytes;
      controller.command_valid = true;
      controller.command_done = true;
      ax200_transport_notify();
    }
    size_t total = length + 4;
    size_t rounded = (total + AX_PACKET_ALIGN - 1) & ~(size_t)(AX_PACKET_ALIGN - 1);
    if (rounded > AX_BUFFER_BYTES - offset) {
      break;
    }
    offset += rounded;
  }
  return true;
}

bool ax200_transport_poll(void)
{
  require_bsp();
  KASSERT(controller.worker && cpu_interrupts_enabled());
  if (controller.failed || !controller.running) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  uint32_t fh = controller.pending_fh, hw = controller.pending_hw;
  controller.pending_fh = controller.pending_hw = 0;
  cpu_restore_interrupts(flags);
  if ((fh & AX_FH_ERROR) || (hw & AX_HW_ERRORS)) {
    return terminal_failure("AX200 fatal hardware/firmware notification; DMA retained");
  }
  uint32_t gp = read32(AX_GP);
  if (gp == UINT32_MAX) {
    return terminal_failure("AX200 register access lost; DMA retained");
  }
  if (!(gp & AX_RADIO_ENABLED)) {
    controller.status.flags |= WIFI_RFKILL;
  } else {
    controller.status.flags &= ~WIFI_RFKILL;
  }
  if (hw & AX_HW_RFKILL) {
    ax200_transport_notify();
  }
  if ((hw & AX_HW_ALIVE) && !controller.rx_started) {
    publish_rx();
  }
  if (controller.rx_started) {
    unsigned closed = rx_closed();
    unsigned queued = (closed - controller.rx_cursor) & (AX_RX_COUNT - 1);
    if (queued > controller.status.rx_high_water) {
      controller.status.rx_high_water = queued;
    }
    unsigned processed = 0;
    while (controller.rx_cursor != closed && processed < AX_POLL_BUFFERS) {
      const uint8_t *buffer = (const void *)(controller.rx_buffers.address +
          (size_t)controller.rx_cursor * AX_BUFFER_BYTES);
      dma_read_barrier();
      if (!receive_buffer(buffer)) {
        return false;
      }
      controller.rx_cursor = (controller.rx_cursor + 1) & (AX_RX_COUNT - 1);
      ++processed;
    }
    if (processed) {
      dma_full_barrier();
      unsigned consumed = (controller.rx_cursor - 1) & (AX_RX_COUNT - 1);
      write32(AX_RX_DOORBELL, consumed & ~7u);
    }
    if (controller.rx_cursor != rx_closed()) {
      ax200_transport_notify();
      kernel_task_yield_if_runnable();
      return true;
    }
  }
  flags = cpu_save_interrupts();
  if (!controller.failed) {
    write32(AX_AUTOMASK, 1);
  }
  cpu_restore_interrupts(flags);
  return !controller.failed;
}

bool ax200_transport_boot(uint64_t deadline_ns)
{
  require_bsp();
  KASSERT(cpu_interrupts_enabled());
  controller.worker = true;
  if (!controller.prepared || controller.failed || controller.boot_attempted) {
    return false;
  }
  controller.boot_attempted = true;
  if (!prepare_card(deadline_ns) || !clear_persistence() || !software_reset() ||
      !prepare_coherence() || !apm_init(deadline_ns) || !configure_nic() ||
      !configure_interrupts(deadline_ns)) {
    return terminal_failure("AX200 firmware hardware preparation failed; DMA retained");
  }
  write32(AX_GP1_CLEAR, AX_GP1_RFKILL | AX_GP1_COMMAND_BLOCKED);
  write32(AX_INT, UINT32_MAX);
  if (!update32(AX_GP, 0, AX_RFKILL_WAKE)) {
    return terminal_failure("AX200 rfkill observation unavailable; DMA retained");
  }
  uint64_t flags = cpu_save_interrupts();
  controller.running = true;
  bool enabled = pci_msix_enable(&controller.msix);
  bool mastering = enabled && set_bus_master(true);
  cpu_restore_interrupts(flags);
  if (!mastering) {
    return terminal_failure("AX200 interrupt/DMA activation refused; retained");
  }
  dma_full_barrier();
  write32(AX_CONTEXT_BASE, (uint32_t)controller.context.physical);
  write32(AX_CONTEXT_BASE + 4, controller.context.physical >> 32);
  if (!nic_acquire(deadline_ns)) {
    return terminal_failure("AX200 firmware self-load access failed; DMA retained");
  }
  write32(AX_LTR, AX_LTR_250_US);
  prph_write(AX_CPU_RUN, 1);
  nic_release();
  uint64_t alive_deadline = limited_deadline(deadline_ns, AX_SECOND);
  while (!controller.alive) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll()) {
      return false;
    }
    if (controller.alive) {
      break;
    }
    if (task_deadline_expired(alive_deadline)) {
      return terminal_failure("AX200 ALIVE deadline expired; DMA retained");
    }
    ax200_transport_wait(event, alive_deadline);
  }
  capture_address(deadline_ns);
  if (!controller.address_valid) {
    return terminal_failure("AX200 hardware address unavailable; DMA retained");
  }
  set_detail("AX200 firmware ALIVE validated; NVM initialization pending");
  return true;
}

bool ax200_command(uint8_t group, uint8_t opcode, const void *payload, size_t bytes,
    void *reply, size_t capacity, size_t *received, uint64_t deadline_ns)
{
  require_bsp();
  KASSERT(controller.worker && cpu_interrupts_enabled());
  if (received) {
    *received = 0;
  }
  if (controller.failed || !controller.alive || !controller.running || controller.command_busy ||
      bytes > AX_COMMAND_PAYLOAD || (bytes && !payload) || capacity > sizeof(controller.response) ||
      (!reply && capacity) || task_deadline_expired(deadline_ns)) {
    return false;
  }
  controller.command_busy = true;
  controller.command_done = controller.command_valid = false;
  controller.command_group = group;
  controller.command_opcode = opcode;
  controller.command_index = controller.producer;
  controller.response_capacity = capacity;
  controller.response_wanted = reply != NULL;
  controller.response_bytes = 0;
  if (controller.status.command_high_water < 1) {
    controller.status.command_high_water = 1;
  }
  size_t total = sizeof(struct ax_command_header) + bytes;
  uintptr_t address;
  phys_addr_t physical;
  if (total <= AX_COMMAND_BYTES) {
    address = controller.tx_commands.address + (size_t)controller.producer * AX_COMMAND_BYTES;
    physical = controller.tx_commands.physical + (size_t)controller.producer * AX_COMMAND_BYTES;
  } else {
    address = controller.command_large.address;
    physical = controller.command_large.physical;
  }
  memset((void *)address, 0, total);
  struct ax_command_header *header = (void *)address;
  header->opcode = opcode;
  header->group = group ? group : AX_LONG_GROUP;
  header->index = controller.producer;
  header->length = bytes;
  if (bytes) {
    memcpy((uint8_t *)address + sizeof(*header), payload, bytes);
  }
  struct ax_tfd *descriptor = (void *)(controller.tx_descriptors.address +
      (size_t)controller.producer * sizeof(struct ax_tfd));
  memset(descriptor, 0, sizeof(*descriptor));
  descriptor->tb[0].address = physical;
  descriptor->tb[0].length = total < AX_FIRST_TB_BYTES ? total : AX_FIRST_TB_BYTES;
  descriptor->count = 1;
  if (total > AX_FIRST_TB_BYTES) {
    descriptor->tb[1].address = physical + AX_FIRST_TB_BYTES;
    descriptor->tb[1].length = total - AX_FIRST_TB_BYTES;
    descriptor->count = 2;
  }
  controller.producer = (uint8_t)(controller.producer + 1);
  dma_full_barrier();
  write32(AX_COMMAND_DOORBELL, controller.producer);
  uint64_t command_deadline = limited_deadline(deadline_ns, AX_SECOND);
  while (!controller.command_done) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll()) {
      return false;
    }
    if (controller.command_done) {
      break;
    }
    if (task_deadline_expired(command_deadline)) {
      return terminal_failure("AX200 command deadline expired; DMA retained");
    }
    ax200_transport_wait(event, command_deadline);
  }
  dma_full_barrier();
  if (!controller.command_valid) {
    return terminal_failure("AX200 command completion uncertain; DMA retained");
  }
  if (reply && controller.response_bytes) {
    memcpy(reply, controller.response, controller.response_bytes);
  }
  if (received) {
    *received = controller.response_bytes;
  }
  controller.command_busy = false;
  return true;
}

void ax200_transport_status(struct wifi_status_reply *status)
{
  require_bsp();
  KASSERT(!cpu_interrupts_enabled());
  *status = controller.status;
}

bool ax200_transport_rfkill(void)
{
  require_bsp();
  return !!(controller.status.flags & WIFI_RFKILL);
}

bool ax200_transport_address(uint8_t address[6])
{
  require_bsp();
  if (!controller.address_valid) {
    return false;
  }
  memcpy(address, controller.address, sizeof(controller.address));
  return true;
}

bool ax200_transport_ltr_enabled(void)
{
  require_bsp();
  return controller.ltr;
}

bool ax200_firmware_api(unsigned bit)
{
  return bit < AX_FW_API_WORDS * 32 && (controller.api[bit / 32] & (1u << (bit % 32)));
}

bool ax200_firmware_capability(unsigned bit)
{
  return bit < AX_FW_CAPABILITY_WORDS * 32 &&
    (controller.capabilities[bit / 32] & (1u << (bit % 32)));
}

int ax200_command_version(uint8_t group, uint8_t opcode, bool notification)
{
  for (size_t i = 0; i < controller.version_count; ++i) {
    const struct ax_firmware_version *version = &controller.versions[i];
    if (version->group == group && version->opcode == opcode) {
      return notification ? version->notification : version->command;
    }
  }
  return -1;
}

uint32_t ax200_firmware_phy(void)
{
  return controller.phy;
}

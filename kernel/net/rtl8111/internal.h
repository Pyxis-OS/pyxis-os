#ifndef RTL8111_INTERNAL_H
#define RTL8111_INTERNAL_H

#include <kernel/pci/msix.h>
#include <kernel/net/panic_tx.h>
#include "registers.h"
#include "ring.h"
#include "counters.h"

enum rtl_panic_operation {
  RTL_PANIC_START = 1,
  RTL_PANIC_STOP,
  RTL_PANIC_IRQ,
  RTL_PANIC_SERVICE,
  RTL_PANIC_RX_REPOST,
  RTL_PANIC_COUNTERS,
  RTL_PANIC_TX_BASE = RTL_RING_COUNT,
};

struct rtl8111_controller {
  uint32_t controller_id;
  struct pci_claim claim;
  struct pci_probe_state probe;
  struct pci_mapping registers;
  struct pci_msix msix;
  unsigned power_capability, express_capability, xid;
  bool identified, mutated, quiesced, prepared;
  uint8_t mac[6];
  bool identity_known, started, active, link_up, stopping;
  struct rtl_ring rx, tx;
  struct net_panic_gate panic_gate;
  bool panic_ready, panic_duplicate;
  unsigned panic_slot, panic_interrupted_slot;
  struct dma_buffer counters;
  uint64_t tx_deadlines[RTL_RING_COUNT];
  uint64_t reset_deadline, reset_recheck;
  uint16_t pending_interrupts;
  const char *stop_reason;
  bool dma_disabled, interrupts_disabled;
  uint64_t interrupts, received, malformed, transmitted, completed, queue_full;
  struct rtl8111_controller *next;
};

static inline uint8_t rtl_read8(const struct rtl8111_controller *controller, unsigned reg)
{
  return *(volatile uint8_t *)(controller->registers.address + reg);
}

static inline uint16_t rtl_read16(const struct rtl8111_controller *controller, unsigned reg)
{
  return *(volatile uint16_t *)(controller->registers.address + reg);
}

static inline uint32_t rtl_read32(const struct rtl8111_controller *controller, unsigned reg)
{
  return *(volatile uint32_t *)(controller->registers.address + reg);
}

static inline void rtl_write8(struct rtl8111_controller *controller, unsigned reg, uint8_t value)
{
  *(volatile uint8_t *)(controller->registers.address + reg) = value;
}

static inline void rtl_write16(struct rtl8111_controller *controller, unsigned reg, uint16_t value)
{
  *(volatile uint16_t *)(controller->registers.address + reg) = value;
}

static inline void rtl_write32(struct rtl8111_controller *controller, unsigned reg, uint32_t value)
{
  *(volatile uint32_t *)(controller->registers.address + reg) = value;
}

void rtl_delay(uint64_t ns);
uint16_t rtl_mac_read(struct rtl8111_controller *controller, unsigned address);
void rtl_mac_write(struct rtl8111_controller *controller, unsigned address, uint16_t value);
void rtl_mac_modify(struct rtl8111_controller *controller, unsigned address,
                    uint16_t clear, uint16_t set);
bool rtl_phy_prepare(struct rtl8111_controller *controller);
bool rtl_phy_read(struct rtl8111_controller *controller, unsigned address, uint16_t *value);

/* Debugger snapshot: BSP kernel context, IF=0, outside interrupt handlers.
 * Only read the returned payload after success. Storage remains owned until reboot,
 * including when a dump times out; timeout does not revoke device ownership. */
const struct rtl_counters *rtl8111_capture_counters(struct rtl8111_controller *controller);

#endif

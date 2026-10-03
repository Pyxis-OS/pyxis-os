#ifndef RTL8111_INTERNAL_H
#define RTL8111_INTERNAL_H

#include <kernel/pci/msix.h>
#include "registers.h"

struct rtl8111_controller {
  struct pci_claim claim;
  struct pci_probe_state probe;
  struct pci_mapping registers;
  struct pci_msix msix;
  unsigned power_capability, express_capability, xid;
  bool mutated, quiesced, prepared;
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

#endif

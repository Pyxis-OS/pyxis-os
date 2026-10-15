/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/clock.h>
#include <kernel/i2c/designware.h>

/* Register facts checked against DesignWare definitions in Linux db6a1865.
 * This is an independent, synchronous FIFO implementation. */
#define DW_CON 0x00
#define DW_TAR 0x04
#define DW_DATA_CMD 0x10
#define DW_SS_HIGH 0x14
#define DW_SS_LOW 0x18
#define DW_FS_HIGH 0x1c
#define DW_FS_LOW 0x20
#define DW_INTR_MASK 0x30
#define DW_RAW_INTR 0x34
#define DW_RX_THRESHOLD 0x38
#define DW_TX_THRESHOLD 0x3c
#define DW_CLEAR_INTR 0x40
#define DW_ENABLE 0x6c
#define DW_STATUS 0x70
#define DW_TX_LEVEL 0x74
#define DW_RX_LEVEL 0x78
#define DW_SDA_HOLD 0x7c
#define DW_ABORT_SOURCE 0x80
#define DW_DMA_CONTROL 0x88
#define DW_ENABLE_STATUS 0x9c
#define DW_COMPONENT_PARAMETERS 0xf4
#define DW_COMPONENT_VERSION 0xf8
#define DW_COMPONENT_TYPE 0xfc

#define DW_TYPE_VALUE UINT32_C(0x44570140)
#define DW_HOLD_MIN_VERSION UINT32_C(0x3131312a)
#define DW_CON_MASTER (1u << 0)
#define DW_CON_STANDARD (1u << 1)
#define DW_CON_FAST (2u << 1)
#define DW_CON_TEN_BIT (1u << 4)
#define DW_CON_RESTART (1u << 5)
#define DW_CON_SLAVE_DISABLE (1u << 6)
#define DW_COMMAND_READ (1u << 8)
#define DW_COMMAND_STOP (1u << 9)
#define DW_COMMAND_RESTART (1u << 10)
#define DW_TAR_TEN_BIT (1u << 12)
#define DW_ENABLED (1u << 0)
#define DW_ACTIVE (1u << 0)
#define DW_MASTER_ACTIVE (1u << 5)
#define DW_SLAVE_ACTIVE (1u << 6)
#define DW_ACTIVITY (DW_ACTIVE | DW_MASTER_ACTIVE | DW_SLAVE_ACTIVE)
#define DW_INTR_RX_UNDER (1u << 0)
#define DW_INTR_RX_OVER (1u << 1)
#define DW_INTR_TX_OVER (1u << 3)
#define DW_INTR_ABORT (1u << 6)
#define DW_INTR_STOP (1u << 9)
#define DW_INTR_FIFO_ERRORS (DW_INTR_RX_UNDER | DW_INTR_RX_OVER | DW_INTR_TX_OVER)
#define DW_ABORT_ADDRESS ((1u << 0) | (1u << 1) | (1u << 2))
#define DW_ABORT_DATA (1u << 3)
#define DW_ABORT_ARBITRATION (1u << 12)
#define DW_PARAMETER_WIDTH_MASK 3u
#define DW_PARAMETER_WIDTH_32 2u
#define DW_PARAMETER_SPEED_SHIFT 2
#define DW_PARAMETER_SPEED_MASK 3u
#define DW_PARAMETER_FAST 2u
#define DW_PARAMETER_RX_SHIFT 8
#define DW_PARAMETER_TX_SHIFT 16
#define DW_PARAMETER_DEPTH_MASK 0xffu
#define DW_FIFO_MIN_DEPTH 2u
#define DW_COUNTER_MASK 0xffffu
#define DW_HIGH_COUNT_MIN 6u
#define DW_LOW_COUNT_MIN 8u
#define DW_HOLD_MASK 0xffffffu

static uint32_t read_register(const struct dw_i2c_master *master, unsigned offset)
{
  return *(const volatile uint32_t *)(master->base + offset);
}

static void write_register(const struct dw_i2c_master *master, unsigned offset,
    uint32_t value)
{
  *(volatile uint32_t *)(master->base + offset) = value;
}

static bool expired(uint64_t deadline)
{
  arch_clock_maintain();
  return arch_monotonic_ns() >= deadline;
}

static bool identified(const struct dw_i2c_master *master)
{
  return master && master->base && master->tx_depth >= DW_FIFO_MIN_DEPTH &&
      master->rx_depth >= DW_FIFO_MIN_DEPTH;
}

static enum dw_i2c_result abort_result(uint32_t source)
{
  if (source == UINT32_MAX) {
    return DW_I2C_IO;
  }
  if (source & DW_ABORT_ARBITRATION) {
    return DW_I2C_ARBITRATION_LOST;
  }
  if (source & DW_ABORT_ADDRESS) {
    return DW_I2C_ADDRESS_NACK;
  }
  if (source & DW_ABORT_DATA) {
    return DW_I2C_DATA_NACK;
  }
  return DW_I2C_IO;
}

static enum dw_i2c_result observe(struct dw_i2c_master *master, uint32_t *interrupts)
{
  master->last_status = read_register(master, DW_STATUS);
  *interrupts = read_register(master, DW_RAW_INTR);
  if (master->last_status == UINT32_MAX || *interrupts == UINT32_MAX) {
    return DW_I2C_IO;
  }
  if (*interrupts & DW_INTR_ABORT) {
    master->abort_source |= read_register(master, DW_ABORT_SOURCE);
    return abort_result(master->abort_source);
  }
  return (*interrupts & DW_INTR_FIFO_ERRORS) ? DW_I2C_IO : DW_I2C_OK;
}

enum dw_i2c_result dw_i2c_identify(struct dw_i2c_master *master, uintptr_t base)
{
  if (!master || !base || (base & (sizeof(uint32_t) - 1)) ||
      base > UINTPTR_MAX - DW_COMPONENT_TYPE - sizeof(uint32_t)) {
    return DW_I2C_UNSUPPORTED;
  }
  *master = (struct dw_i2c_master){.base = base};
  master->component_type = read_register(master, DW_COMPONENT_TYPE);
  if (master->component_type != DW_TYPE_VALUE) {
    return DW_I2C_UNSUPPORTED;
  }
  master->component_version = read_register(master, DW_COMPONENT_VERSION);
  master->component_parameters = read_register(master, DW_COMPONENT_PARAMETERS);
  uint32_t parameters = master->component_parameters;
  unsigned tx_depth = ((parameters >> DW_PARAMETER_TX_SHIFT) & DW_PARAMETER_DEPTH_MASK) + 1;
  unsigned rx_depth = ((parameters >> DW_PARAMETER_RX_SHIFT) & DW_PARAMETER_DEPTH_MASK) + 1;
  if (parameters == UINT32_MAX ||
      (parameters & DW_PARAMETER_WIDTH_MASK) != DW_PARAMETER_WIDTH_32 ||
      tx_depth < DW_FIFO_MIN_DEPTH || rx_depth < DW_FIFO_MIN_DEPTH ||
      !master->component_version || master->component_version == UINT32_MAX) {
    return DW_I2C_UNSUPPORTED;
  }
  master->tx_depth = tx_depth;
  master->rx_depth = rx_depth;
  return DW_I2C_OK;
}

enum dw_i2c_result dw_i2c_halt(struct dw_i2c_master *master, uint64_t deadline)
{
  if (!identified(master) || master->failed) {
    return DW_I2C_UNSUPPORTED;
  }
  write_register(master, DW_INTR_MASK, 0);
  write_register(master, DW_DMA_CONTROL, 0);
  uint32_t interrupts;
  enum dw_i2c_result observed = observe(master, &interrupts);
  /* Disabling may wait for a bus operation to finish. Never force ABORT on
   * an inherited firmware transaction or attempt GPIO bus recovery. */
  write_register(master, DW_ENABLE, 0);
  for (;;) {
    uint32_t enabled = read_register(master, DW_ENABLE_STATUS);
    uint32_t requested = read_register(master, DW_ENABLE);
    master->last_status = read_register(master, DW_STATUS);
    if (enabled == UINT32_MAX || requested == UINT32_MAX ||
        master->last_status == UINT32_MAX) {
      master->failed = true;
      master->configured = false;
      return DW_I2C_IO;
    }
    if (!((enabled | requested) & DW_ENABLED) && !(master->last_status & DW_ACTIVITY)) {
      break;
    }
    if (expired(deadline)) {
      master->failed = true;
      master->configured = false;
      return DW_I2C_TIMEOUT;
    }
  }
  uint32_t pending = read_register(master, DW_RX_LEVEL);
  if (pending > master->rx_depth) {
    master->failed = true;
    master->configured = false;
    return DW_I2C_IO;
  }
  for (uint32_t i = 0; i < pending; ++i) {
    (void)read_register(master, DW_DATA_CMD);
  }
  enum dw_i2c_result final_observation = observe(master, &interrupts);
  if (observed == DW_I2C_OK) {
    observed = final_observation;
  }
  if (interrupts == UINT32_MAX || master->last_status == UINT32_MAX ||
      read_register(master, DW_RX_LEVEL) || read_register(master, DW_TX_LEVEL) ||
      read_register(master, DW_DMA_CONTROL) || read_register(master, DW_INTR_MASK)) {
    master->failed = true;
    master->configured = false;
    return DW_I2C_IO;
  }
  (void)read_register(master, DW_CLEAR_INTR);
  return observed;
}

enum dw_i2c_result dw_i2c_configure(struct dw_i2c_master *master,
    const struct dw_i2c_timing *timing, uint64_t deadline)
{
  if (!identified(master) || master->failed || !timing ||
      (timing->speed_hz != 100000 && timing->speed_hz != 400000) ||
      timing->high_count < DW_HIGH_COUNT_MIN || timing->high_count > DW_COUNTER_MASK ||
      timing->low_count < DW_LOW_COUNT_MIN || timing->low_count > DW_COUNTER_MASK ||
      (timing->sda_hold & ~DW_HOLD_MASK) ||
      (timing->sda_hold && master->component_version < DW_HOLD_MIN_VERSION) ||
      (timing->speed_hz == 400000 &&
       ((master->component_parameters >> DW_PARAMETER_SPEED_SHIFT) &
        DW_PARAMETER_SPEED_MASK) < DW_PARAMETER_FAST)) {
    return DW_I2C_UNSUPPORTED;
  }
  if (expired(deadline)) {
    return DW_I2C_TIMEOUT;
  }
  uint32_t enable = read_register(master, DW_ENABLE);
  uint32_t enabled = read_register(master, DW_ENABLE_STATUS);
  if (enable == UINT32_MAX || enabled == UINT32_MAX) {
    return DW_I2C_IO;
  }
  if ((enable | enabled) & DW_ENABLED) {
    return DW_I2C_BUSY;
  }
  master->last_status = read_register(master, DW_STATUS);
  if (master->last_status == UINT32_MAX) {
    return DW_I2C_IO;
  }
  if ((master->last_status & DW_ACTIVITY) || read_register(master, DW_TX_LEVEL) ||
      read_register(master, DW_RX_LEVEL)) {
    return DW_I2C_BUSY;
  }
  master->configured = false;
  unsigned high = timing->speed_hz == 100000 ? DW_SS_HIGH : DW_FS_HIGH;
  unsigned low = timing->speed_hz == 100000 ? DW_SS_LOW : DW_FS_LOW;
  uint32_t control = DW_CON_MASTER | DW_CON_RESTART | DW_CON_SLAVE_DISABLE |
      (timing->speed_hz == 100000 ? DW_CON_STANDARD : DW_CON_FAST);
  write_register(master, DW_INTR_MASK, 0);
  write_register(master, DW_DMA_CONTROL, 0);
  write_register(master, high, timing->high_count);
  write_register(master, low, timing->low_count);
  if (master->component_version >= DW_HOLD_MIN_VERSION) {
    write_register(master, DW_SDA_HOLD, timing->sda_hold);
  }
  write_register(master, DW_RX_THRESHOLD, 0);
  write_register(master, DW_TX_THRESHOLD, 0);
  write_register(master, DW_CON, control);
  (void)read_register(master, DW_CLEAR_INTR);
  if (read_register(master, high) != timing->high_count ||
      read_register(master, low) != timing->low_count ||
      read_register(master, DW_CON) != control ||
      (master->component_version >= DW_HOLD_MIN_VERSION &&
       read_register(master, DW_SDA_HOLD) != timing->sda_hold) ||
      read_register(master, DW_DMA_CONTROL) || read_register(master, DW_INTR_MASK)) {
    return DW_I2C_IO;
  }
  if (expired(deadline)) {
    return DW_I2C_TIMEOUT;
  }
  master->configured = true;
  return DW_I2C_OK;
}

static enum dw_i2c_result finish(struct dw_i2c_master *master,
    enum dw_i2c_result result, uint64_t deadline)
{
  uint32_t failure_status = master->last_status;
  enum dw_i2c_result cleanup = dw_i2c_halt(master, deadline);
  if (result != DW_I2C_OK) {
    master->last_status = failure_status;
    return result;
  }
  if (cleanup == DW_I2C_OK && master->abort_source) {
    return abort_result(master->abort_source);
  }
  return cleanup;
}

enum dw_i2c_result dw_i2c_transfer(struct dw_i2c_master *master,
    uint16_t address, bool ten_bit, const uint8_t *tx, size_t tx_size,
    uint8_t *rx, size_t rx_size, uint64_t deadline)
{
  if (!identified(master) || master->failed || !master->configured ||
      address > (ten_bit ? 0x3ff : 0x7f) || (!tx_size && !rx_size) ||
      (tx_size && !tx) || (rx_size && !rx)) {
    return DW_I2C_UNSUPPORTED;
  }
  if (expired(deadline)) {
    return DW_I2C_TIMEOUT;
  }
  master->abort_source = 0;
  uint32_t interrupts;
  enum dw_i2c_result result = observe(master, &interrupts);
  if (result != DW_I2C_OK) {
    return finish(master, result, deadline);
  }
  uint32_t enable = read_register(master, DW_ENABLE);
  uint32_t enabled = read_register(master, DW_ENABLE_STATUS);
  if (enable == UINT32_MAX || enabled == UINT32_MAX) {
    return finish(master, DW_I2C_IO, deadline);
  }
  if ((master->last_status & DW_ACTIVITY) || ((enable | enabled) & DW_ENABLED)) {
    return finish(master, DW_I2C_BUSY, deadline);
  }
  if (read_register(master, DW_TX_LEVEL) || read_register(master, DW_RX_LEVEL)) {
    return finish(master, DW_I2C_IO, deadline);
  }
  uint32_t control = read_register(master, DW_CON) & ~DW_CON_TEN_BIT;
  if (ten_bit) {
    control |= DW_CON_TEN_BIT;
  }
  write_register(master, DW_CON, control);
  write_register(master, DW_TAR, address | (ten_bit ? DW_TAR_TEN_BIT : 0));
  if (read_register(master, DW_CON) != control ||
      read_register(master, DW_TAR) != (address | (ten_bit ? DW_TAR_TEN_BIT : 0)) ||
      read_register(master, DW_DMA_CONTROL) || read_register(master, DW_INTR_MASK)) {
    return finish(master, DW_I2C_IO, deadline);
  }
  (void)read_register(master, DW_CLEAR_INTR);
  write_register(master, DW_ENABLE, DW_ENABLED);
  for (;;) {
    enabled = read_register(master, DW_ENABLE_STATUS);
    if (enabled == UINT32_MAX) {
      return finish(master, DW_I2C_IO, deadline);
    }
    if (enabled & DW_ENABLED) {
      break;
    }
    if (expired(deadline)) {
      return finish(master, DW_I2C_TIMEOUT, deadline);
    }
  }
  size_t written = 0, requested = 0, received = 0;
  for (;;) {
    result = observe(master, &interrupts);
    if (result != DW_I2C_OK) {
      return finish(master, result, deadline);
    }
    uint32_t available = read_register(master, DW_RX_LEVEL);
    if (available > master->rx_depth || available > requested - received) {
      return finish(master, DW_I2C_IO, deadline);
    }
    for (uint32_t i = 0; i < available; ++i) {
      rx[received++] = (uint8_t)read_register(master, DW_DATA_CMD);
    }
    if (interrupts & DW_INTR_STOP) {
      if (written != tx_size || requested != rx_size || received != rx_size ||
          read_register(master, DW_TX_LEVEL)) {
        return finish(master, DW_I2C_IO, deadline);
      }
      if (!(master->last_status & DW_ACTIVITY)) {
        return finish(master, DW_I2C_OK, deadline);
      }
    }
    if (expired(deadline)) {
      return finish(master, DW_I2C_TIMEOUT, deadline);
    }
    uint32_t queued = read_register(master, DW_TX_LEVEL);
    if (queued > master->tx_depth) {
      return finish(master, DW_I2C_IO, deadline);
    }
    unsigned slots = master->tx_depth - queued;
    while (slots && (written < tx_size || requested < rx_size)) {
      uint32_t command;
      if (written < tx_size) {
        command = tx[written++];
        if (written == tx_size && !rx_size) {
          command |= DW_COMMAND_STOP;
        }
      } else {
        /* Every unread command consumes one RX FIFO credit, including reads
         * still queued in TX. Drain before issuing any beyond that bound. */
        if (requested - received >= master->rx_depth) {
          break;
        }
        command = DW_COMMAND_READ;
        if (!requested && tx_size) {
          command |= DW_COMMAND_RESTART;
        }
        if (++requested == rx_size) {
          command |= DW_COMMAND_STOP;
        }
      }
      write_register(master, DW_DATA_CMD, command);
      --slots;
    }
  }
}

const char *dw_i2c_result_name(enum dw_i2c_result result)
{
  switch (result) {
  case DW_I2C_OK: return "ok";
  case DW_I2C_BUSY: return "busy";
  case DW_I2C_ADDRESS_NACK: return "address-nack";
  case DW_I2C_DATA_NACK: return "data-nack";
  case DW_I2C_ARBITRATION_LOST: return "arbitration-lost";
  case DW_I2C_TIMEOUT: return "timeout";
  case DW_I2C_IO: return "io";
  case DW_I2C_UNSUPPORTED: return "unsupported";
  }
  return "unknown";
}

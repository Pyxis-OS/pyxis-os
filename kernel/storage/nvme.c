/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/boot/options.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/dma.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/pci/registers.h>
#include <kernel/pci/msi.h>
#include <kernel/pci/msix.h>
#include <kernel/storage/nvme.h>
#include <kernel/string.h>
#include <kernel/task.h>

/* NVM Express 1.3 / Base 2.0c register and command contracts. No vendor code. */
#define NVME_CAP 0x00u
#define NVME_VS 0x08u
#define NVME_INTMS 0x0cu
#define NVME_INTMC 0x10u
#define NVME_CC 0x14u
#define NVME_CSTS 0x1cu
#define NVME_AQA 0x24u
#define NVME_ASQ 0x28u
#define NVME_ACQ 0x30u
#define NVME_CRTO 0x68u
#define NVME_DOORBELLS 0x1000u
#define NVME_CAP_NVM (UINT64_C(1) << 37)
#define NVME_CAP_IO_COMMAND_SETS (UINT64_C(1) << 43)
#define NVME_CAP_TIMEOUT_MASK (UINT64_C(255) << 24)
#define NVME_CAP_READY_WITH_MEDIA (UINT64_C(1) << 59)
#define NVME_CAP_READY_INDEPENDENT (UINT64_C(1) << 60)
#define NVME_CC_IO_COMMAND_SETS (6u << 4)
#define NVME_CC_READY_INDEPENDENT (1u << 24)
#define NVME_CC_ENABLE 1u
#define NVME_CC_SHUTDOWN (1u << 14)
#define NVME_CC_SHUTDOWN_MASK (3u << 14)
#define NVME_CC_QUEUE_SIZES ((6u << 16) | (4u << 20))
#define NVME_CSTS_READY 1u
#define NVME_CSTS_FATAL 2u
#define NVME_CSTS_SHUTDOWN_MASK 0xcu
#define NVME_CSTS_SHUTDOWN_DONE 0x8u
#define NVME_ADMIN_IDENTIFY 0x06u
#define NVME_ADMIN_GET_LOG 0x02u
#define NVME_ADMIN_FEATURES 0x09u
#define NVME_ADMIN_GET_FEATURES 0x0au
#define NVME_ADMIN_CREATE_CQ 0x05u
#define NVME_ADMIN_CREATE_SQ 0x01u
#define NVME_ADMIN_DELETE_SQ 0x00u
#define NVME_ADMIN_DELETE_CQ 0x04u
#define NVME_NVM_WRITE 0x01u
#define NVME_NVM_READ 0x02u
#define NVME_NVM_FLUSH 0x00u
#define NVME_IDENTIFY_NAMESPACE 0u
#define NVME_IDENTIFY_CONTROLLER 1u
#define NVME_IDENTIFY_ACTIVE_NS 2u
#define NVME_IDENTIFY_NS_DESCRIPTORS 3u
#define NVME_IDENTIFY_IO_COMMAND_SETS 0x1cu
#define NVME_IDENTIFY_CURRENT_CONTROLLER (0xffffu << 16)
#define NVME_FEATURE_QUEUE_COUNT 7u
#define NVME_FEATURE_IO_PROFILE 0x19u
#define NVME_IO_PROFILE_INDEX_MASK 0x1ffu
#define NVME_IO_PROFILE_NVM UINT64_C(1)
#define NVME_NS_DESCRIPTOR_CSI 4u
#define NVME_CSI_NVM 0u
#define NVME_SMART_LOG 2u
#define NVME_LOG_RETAIN_EVENTS (1u << 15)
#define NVME_QUEUE_CONTIGUOUS 1u
#define NVME_CQ_INTERRUPT 2u
#define NVME_INTERRUPT_VECTOR_ZERO 1u
#define NVME_IO_QUEUE 1u
#define NVME_ADMIN_DEPTH 16u
#define NVME_IO_DEPTH 16u
#define NVME_SLOT_LIMIT 8u
#define NVME_TRANSFER_LIMIT 65536u
#define NVME_COMMAND_TIMEOUT_NS UINT64_C(5000000000)
#define NVME_READY_UNIT_NS UINT64_C(500000000)
#define NVME_READY_LIMIT_UNITS 255u
#define NVME_SHUTDOWN_LIMIT_US 5000000u
#define NVME_SHUTDOWN_BUDGET_NS UINT64_C(20000000000)
#define NVME_POLL_NS UINT64_C(1000000)
#define NVME_STATUS_PHASE 1u
#define NVME_STATUS_CODE_MASK 0xffeu
#define NVME_PAGE_BYTES 4096u
#define NVME_ID_MDTS 77u
#define NVME_ID_RTD3E 88u
#define NVME_ID_CONTROLLER_TYPE 111u
#define NVME_CONTROLLER_IO 1u
#define NVME_ID_SQES 512u
#define NVME_ID_CQES 513u
#define NVME_NS_FEATURES 24u
#define NVME_NS_THIN_PROVISIONING 1u
#define NVME_NS_NLBAF 25u
#define NVME_NS_FLBAS 26u
#define NVME_NS_DPS 29u
#define NVME_NS_LBA_FORMATS 128u
#define NVME_NS_LBA_FORMAT_COUNT 64u
#define NVME_NSID_BROADCAST UINT32_MAX
#define NVME_ACTIVE_LIST_ENTRIES (NVME_PAGE_BYTES / 4)
#define NVME_NAMESPACE_EXAMINE_LIMIT 8u

struct nvme_command {
  uint8_t opcode, flags;
  uint16_t cid;
  uint32_t nsid;
  uint64_t reserved, metadata, prp1, prp2;
  uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
struct nvme_completion {
  uint32_t result, reserved;
  uint16_t sq_head, sq_id, cid, status;
};
_Static_assert(sizeof(struct nvme_command) == 64, "NVMe submission entry");
_Static_assert(sizeof(struct nvme_completion) == 16, "NVMe completion entry");

struct nvme_queue {
  struct dma_buffer sq, cq;
  unsigned depth, id, tail, cq_head, sq_head, descriptors;
  uint64_t posted, consumed;
  bool phase;
};
enum nvme_slot_state { NVME_FREE, NVME_QUEUED, NVME_ACTIVE, NVME_DONE };
struct nvme_slot {
  struct dma_buffer data, prps;
  enum nvme_slot_state state;
  enum block_operation operation;
  uint64_t generation, first_block, deadline, sequence;
  uint16_t cid;
  size_t bytes;
  struct block_completion completion;
  struct task_wait *wait;
  bool abandoned, device_owned;
};
struct nvme_controller {
  block_device_id id;
  enum block_preparation preparation;
  struct pci_claim claim;
  struct pci_mapping registers, doorbells;
  /* MSI-X when the function has it, otherwise one MSI message. */
  bool msi_only;
  struct pci_msix msix;
  struct pci_msi msi;
  struct nvme_queue admin, io;
  struct dma_buffer identify, smart;
  struct nvme_slot slots[NVME_SLOT_LIMIT];
  struct block_info info;
  uint64_t cap, reset_timeout, generation, published, completed, written;
  uint64_t shutdown_deadline;
  unsigned stride, namespace, shutdown_us;
  uint16_t next_cid;
  uint32_t cc, version;
  struct task_wait *worker_wait;
  enum call_status shutdown_result;
  bool initialized, prepared, accepting, interrupt_ready, notified, stopped;
  bool shutdown_requested, shutdown_done;
};
static struct nvme_controller *controllers;
static size_t controller_count;
static bool inventory_complete;

static void require_client(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}
static uint32_t read_register(struct nvme_controller *disk, unsigned offset)
{
  return *(const volatile uint32_t *)(disk->registers.address + offset);
}
static uint64_t read_register64(struct nvme_controller *disk, unsigned offset)
{
  return *(const volatile uint64_t *)(disk->registers.address + offset);
}
static void write_register(struct nvme_controller *disk, unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)(disk->registers.address + offset) = value;
}
static void write_register64(struct nvme_controller *disk, unsigned offset, uint64_t value)
{
  *(volatile uint64_t *)(disk->registers.address + offset) = value;
}
static void ring_doorbell(struct nvme_controller *disk, unsigned index, unsigned value)
{
  dma_write_barrier();
  *(volatile uint32_t *)(disk->doorbells.address + index * disk->stride) = value;
}
static void pause_poll(bool boot)
{
  if (boot) {
    arch_clock_maintain();
    __asm__ volatile("pause");
  } else {
    kernel_task_sleep_until(arch_monotonic_ns() + NVME_POLL_NS);
  }
}
static uint64_t bounded_ready_timeout(unsigned units)
{
  if (!units) {
    units = 1;
  } else if (units > NVME_READY_LIMIT_UNITS) {
    units = NVME_READY_LIMIT_UNITS;
  }
  return (uint64_t)units * NVME_READY_UNIT_NS;
}
static uint64_t enable_timeout(struct nvme_controller *disk, uint32_t cc)
{
  if (!(disk->cap & NVME_CAP_READY_WITH_MEDIA)) {
    return disk->reset_timeout;
  }
  uint32_t crto = read_register(disk, NVME_CRTO);
  unsigned units = cc & NVME_CC_READY_INDEPENDENT ? crto >> 16 : crto & 0xffffu;
  return bounded_ready_timeout(units);
}
static bool refresh_capabilities(struct nvme_controller *disk)
{
  uint64_t cap = read_register64(disk, NVME_CAP);
  /* Reset/CRIME may change CAP.TO, but none of the resource-layout fields. */
  if (cap == UINT64_MAX || (cap & ~NVME_CAP_TIMEOUT_MASK) !=
      (disk->cap & ~NVME_CAP_TIMEOUT_MASK)) {
    return false;
  }
  disk->cap = cap;
  disk->reset_timeout = bounded_ready_timeout((cap >> 24) & 255);
  return true;
}
static bool wait_ready(struct nvme_controller *disk, bool ready)
{
  uint32_t cc = read_register(disk, NVME_CC);
  if (cc == UINT32_MAX) {
    return false;
  }
  uint64_t deadline = arch_monotonic_ns() +
      (ready ? enable_timeout(disk, cc) : disk->reset_timeout);
  for (;;) {
    uint32_t status = read_register(disk, NVME_CSTS);
    if (status == UINT32_MAX || (ready && (status & NVME_CSTS_FATAL))) {
      return false;
    }
    if (!!(status & NVME_CSTS_READY) == ready) {
      return true;
    }
    if (arch_monotonic_ns() >= deadline) {
      return false;
    }
    pause_poll(true);
  }
}
static bool halt_controller(struct nvme_controller *disk, bool boot)
{
  uint32_t cc = read_register(disk, NVME_CC);
  if (cc == UINT32_MAX) {
    return false;
  }
  if (cc & NVME_CC_ENABLE) {
    if (boot && !wait_ready(disk, true)) {
      return false;
    }
    write_register(disk, NVME_CC, cc & ~NVME_CC_ENABLE);
  }
  uint64_t deadline = arch_monotonic_ns() + disk->reset_timeout;
  for (;;) {
    uint32_t status = read_register(disk, NVME_CSTS);
    if (status == UINT32_MAX) {
      return false;
    }
    if (!(status & NVME_CSTS_READY)) {
      disk->initialized = false;
      return true;
    }
    if (arch_monotonic_ns() >= deadline) {
      return false;
    }
    pause_poll(boot);
  }
}
static struct nvme_controller *find_device(block_device_id id)
{
  for (size_t i = 0; i < controller_count; ++i) {
    if (id && controllers[i].id == id) {
      return &controllers[i];
    }
  }
  return NULL;
}
size_t nvme_device_count(void)
{
  require_client();
  return controller_count;
}
block_device_id nvme_device_at(size_t index)
{
  require_client();
  return index < controller_count ? controllers[index].id : BLOCK_DEVICE_ID_NONE;
}
void nvme_set_id(size_t index, block_device_id id)
{
  require_client();
  KASSERT(index < controller_count && id && !controllers[index].accepting);
  controllers[index].id = id;
}
bool nvme_inventory_complete(void)
{
  require_client();
  return inventory_complete;
}
enum block_preparation nvme_preparation_result(block_device_id id)
{
  require_client();
  struct nvme_controller *disk = find_device(id);
  return disk ? disk->preparation : BLOCK_DEVICE_INVALID;
}
static enum mm_result allocate_queue(struct nvme_queue *queue, unsigned id, unsigned depth)
{
  queue->id = id;
  queue->depth = depth;
  queue->phase = true;
  enum mm_result result = dma_buffer_allocate(&queue->sq, depth * sizeof(struct nvme_command));
  return result == MM_OK ? dma_buffer_allocate(&queue->cq, depth * sizeof(struct nvme_completion)) : result;
}
static bool queue_post(struct nvme_controller *disk, struct nvme_queue *queue,
    const struct nvme_command *command)
{
  if (queue->descriptors >= queue->depth - 1 || queue->posted == UINT64_MAX) {
    return false;
  }
  struct nvme_command *sq = (void *)queue->sq.address;
  sq[queue->tail] = *command;
  queue->tail = (queue->tail + 1) % queue->depth;
  ++queue->descriptors;
  ++queue->posted;
  ring_doorbell(disk, queue->id * 2, queue->tail);
  return true;
}
/* Peek never recycles buffers. The protocol caller validates CID ownership
 * before acknowledging this entry or returning request DMA to its client. */
static int queue_peek(struct nvme_queue *queue, struct nvme_completion *value)
{
  const volatile struct nvme_completion *cq = (void *)queue->cq.address;
  const volatile struct nvme_completion *entry = &cq[queue->cq_head];
  uint16_t status = entry->status;
  if (!!(status & NVME_STATUS_PHASE) != queue->phase) {
    return 0;
  }
  dma_read_barrier();
  *value = (struct nvme_completion){
    .result = entry->result, .reserved = entry->reserved,
    .sq_head = entry->sq_head, .sq_id = entry->sq_id, .cid = entry->cid, .status = status,
  };
  if (value->sq_id != queue->id || value->sq_head >= queue->depth) {
    return -1;
  }
  unsigned advance = (value->sq_head + queue->depth - queue->sq_head) % queue->depth;
  return advance <= queue->descriptors ? 1 : -1;
}
static bool submission_consumed(const struct nvme_queue *queue,
    const struct nvme_completion *value, uint64_t sequence)
{
  unsigned advance = (value->sq_head + queue->depth - queue->sq_head) % queue->depth;
  return sequence <= queue->consumed + advance;
}
static void queue_ack(struct nvme_controller *disk, struct nvme_queue *queue,
    const struct nvme_completion *value)
{
  unsigned advance = (value->sq_head + queue->depth - queue->sq_head) % queue->depth;
  queue->descriptors -= advance;
  queue->consumed += advance;
  queue->sq_head = value->sq_head;
  queue->cq_head = (queue->cq_head + 1) % queue->depth;
  if (!queue->cq_head) {
    queue->phase = !queue->phase;
  }
  ring_doorbell(disk, queue->id * 2 + 1, queue->cq_head);
}
static bool controller_ready(struct nvme_controller *disk)
{
  return read_register64(disk, NVME_CAP) == disk->cap &&
      read_register(disk, NVME_CC) == disk->cc &&
      (read_register(disk, NVME_CSTS) & (NVME_CSTS_READY | NVME_CSTS_FATAL)) == NVME_CSTS_READY;
}
static bool admin_command(struct nvme_controller *disk, struct nvme_command command,
    bool boot, uint32_t *result)
{
  uint64_t flags = cpu_save_interrupts();
  if (!boot && (disk->stopped || (disk->shutdown_requested &&
      arch_monotonic_ns() >= disk->shutdown_deadline))) {
    cpu_restore_interrupts(flags);
    return false;
  }
  command.cid = ++disk->next_cid;
  bool posted = queue_post(disk, &disk->admin, &command);
  cpu_restore_interrupts(flags);
  if (!posted) {
    return false;
  }
  uint64_t deadline = arch_monotonic_ns() + NVME_COMMAND_TIMEOUT_NS;
  if (!boot && disk->shutdown_requested && deadline > disk->shutdown_deadline) {
    deadline = disk->shutdown_deadline;
  }
  for (;;) {
    struct nvme_completion completion;
    int state = queue_peek(&disk->admin, &completion);
    if (state < 0 || (state && (completion.cid != command.cid ||
        !submission_consumed(&disk->admin, &completion, disk->admin.posted)))) {
      return false;
    }
    if (state) {
      queue_ack(disk, &disk->admin, &completion);
      if (result) {
        *result = completion.result;
      }
      return !(completion.status & NVME_STATUS_CODE_MASK);
    }
    if (!controller_ready(disk) || arch_monotonic_ns() >= deadline || disk->stopped) {
      return false;
    }
    pause_poll(boot);
  }
}
static uint16_t get16(const uint8_t *p)
{
  return p[0] | (uint16_t)p[1] << 8;
}
static uint32_t get32(const uint8_t *p)
{
  return get16(p) | (uint32_t)get16(p + 2) << 16;
}
static uint64_t get64(const uint8_t *p)
{
  return get32(p) | (uint64_t)get32(p + 4) << 32;
}
static bool identify(struct nvme_controller *disk, uint32_t namespace, unsigned kind)
{
  memset((void *)disk->identify.address, 0, NVME_PAGE_BYTES);
  return admin_command(disk, (struct nvme_command){
      .opcode = NVME_ADMIN_IDENTIFY, .nsid = namespace,
      .prp1 = disk->identify.physical, .cdw10 = kind,
    }, true, NULL);
}
static void decimal128(const uint8_t *source, char output[40])
{
  uint8_t bytes[16];
  memcpy(bytes, source, sizeof(bytes));
  char reverse[39];
  unsigned count = 0;
  for (;;) {
    unsigned remainder = 0;
    bool nonzero = false;
    for (unsigned i = sizeof(bytes); i--;) {
      unsigned digit = (remainder << 8) | bytes[i];
      bytes[i] = digit / 10;
      remainder = digit % 10;
      nonzero |= bytes[i] != 0;
    }
    reverse[count++] = '0' + remainder;
    if (!nonzero) {
      break;
    }
  }
  for (unsigned i = 0; i < count; ++i) {
    output[i] = reverse[count - i - 1];
  }
  output[count] = '\0';
}
static bool smart_read(struct nvme_controller *disk, bool boot)
{
  memset((void *)disk->smart.address, 0, 512);
  if (!admin_command(disk, (struct nvme_command){
      .opcode = NVME_ADMIN_GET_LOG, .nsid = UINT32_MAX, .prp1 = disk->smart.physical,
      .cdw10 = (127u << 16) | NVME_LOG_RETAIN_EVENTS | NVME_SMART_LOG,
    }, boot, NULL)) {
    return false;
  }
  const uint8_t *data = (void *)disk->smart.address;
  char read[40], written[40], unsafe[40];
  decimal128(data + 32, read);
  decimal128(data + 48, written);
  decimal128(data + 144, unsafe);
  kinfo("nvme PCI %x:%x.%u: SMART read units=%s written units=%s (512000 bytes/unit), used=%u%% spare=%u%% threshold=%u%% temperature=%u K critical=0x%x unsafe=%s\n",
      disk->claim.device->address.bus, disk->claim.device->address.device,
      disk->claim.device->address.function, read, written, (unsigned)data[5], (unsigned)data[3], (unsigned)data[4],
      (unsigned)get16(data + 1), (unsigned)data[0], unsafe);
  return true;
}
static const char *unsupported_geometry(struct nvme_controller *disk, const char *reason)
{
  disk->preparation = BLOCK_DEVICE_UNSUPPORTED;
  return reason;
}
/* The current I/O command set profile must include NVM. */
static const char *identify_io_profile(struct nvme_controller *disk)
{
  if (!(disk->cc & NVME_CC_IO_COMMAND_SETS)) {
    return NULL;
  }
  if (!identify(disk, 0, NVME_IDENTIFY_CURRENT_CONTROLLER | NVME_IDENTIFY_IO_COMMAND_SETS)) {
    return "I/O command set combinations identify failed";
  }
  uint32_t profile;
  if (!admin_command(disk, (struct nvme_command){
      .opcode = NVME_ADMIN_GET_FEATURES, .cdw10 = NVME_FEATURE_IO_PROFILE,
    }, true, &profile)) {
    return "current I/O command set profile read failed";
  }
  unsigned index = profile & NVME_IO_PROFILE_INDEX_MASK;
  const uint8_t *data = (void *)disk->identify.address;
  for (unsigned i = 0; i <= index; ++i) {
    uint64_t vector = get64(data + i * 8);
    if (!vector || (i == index && !(vector & NVME_IO_PROFILE_NVM))) {
      return unsupported_geometry(disk, "current I/O command set profile excludes NVM");
    }
  }
  return NULL;
}
static const char *identify_controller(struct nvme_controller *disk)
{
  if (!identify(disk, 0, NVME_IDENTIFY_CONTROLLER)) {
    return "controller identify failed";
  }
  const uint8_t *data = (void *)disk->identify.address;
  unsigned type = data[NVME_ID_CONTROLLER_TYPE];
  if (type != NVME_CONTROLLER_IO && (type || disk->version >= 0x10400u)) {
    return unsupported_geometry(disk, "requires an I/O controller");
  }
  unsigned sqes = data[NVME_ID_SQES], cqes = data[NVME_ID_CQES];
  if ((sqes & 15) > 6 || (sqes >> 4) < 6 || (cqes & 15) > 4 || (cqes >> 4) < 4) {
    return unsupported_geometry(disk, "unsupported command entry sizes");
  }
  unsigned mdts = data[NVME_ID_MDTS];
  disk->info.max_transfer = mdts && mdts < 4 ? NVME_PAGE_BYTES << mdts : NVME_TRANSFER_LIMIT;
  disk->shutdown_us = get32(data + NVME_ID_RTD3E);
  if (disk->shutdown_us > NVME_SHUTDOWN_LIMIT_US) {
    return unsupported_geometry(disk, "advertised shutdown time exceeds five-second profile");
  }
  if (disk->shutdown_us < 1000000u) {
    disk->shutdown_us = 1000000u;
  }
  if (disk->cap & NVME_CAP_IO_COMMAND_SETS) {
    return identify_io_profile(disk);
  }
  return NULL;
}

enum nvme_namespace_result {
  NVME_NAMESPACE_ELIGIBLE,
  NVME_NAMESPACE_REFUSED,
  NVME_NAMESPACE_FAILED,
};
/* Command-set-capable controllers must identify the namespace as CSI 0. */
static enum nvme_namespace_result namespace_command_set(struct nvme_controller *disk,
    uint32_t namespace, const char **reason)
{
  if (!identify(disk, namespace, NVME_IDENTIFY_NS_DESCRIPTORS)) {
    *reason = "namespace command set descriptors identify failed";
    return NVME_NAMESPACE_FAILED;
  }
  const uint8_t *data = (void *)disk->identify.address;
  bool nvm = false;
  for (unsigned offset = 0; offset < NVME_PAGE_BYTES;) {
    if (NVME_PAGE_BYTES - offset < 4) {
      *reason = "truncated namespace descriptor";
      return NVME_NAMESPACE_REFUSED;
    }
    unsigned kind = data[offset], length = data[offset + 1];
    if (!length) {
      break;
    }
    if (length > NVME_PAGE_BYTES - offset - 4) {
      *reason = "truncated namespace descriptor payload";
      return NVME_NAMESPACE_REFUSED;
    }
    if (kind == NVME_NS_DESCRIPTOR_CSI) {
      if (nvm || length != 1 || data[offset + 4] != NVME_CSI_NVM) {
        *reason = "namespace requires one NVM command set identifier";
        return NVME_NAMESPACE_REFUSED;
      }
      nvm = true;
    }
    offset += 4 + length;
  }
  if (!nvm) {
    *reason = "namespace lacks NVM command set identifier";
    return NVME_NAMESPACE_REFUSED;
  }
  return NVME_NAMESPACE_ELIGIBLE;
}
/* A refusal concerns this namespace alone; a failed admin command ends the
 * controller's preparation. Geometry is recorded only for an eligible one. */
static enum nvme_namespace_result examine_namespace(struct nvme_controller *disk,
    uint32_t namespace, const char **reason)
{
  if (namespace == NVME_NSID_BROADCAST) {
    *reason = "broadcast namespace ID in the active list";
    return NVME_NAMESPACE_REFUSED;
  }
  if (disk->cap & NVME_CAP_IO_COMMAND_SETS) {
    enum nvme_namespace_result result = namespace_command_set(disk, namespace, reason);
    if (result != NVME_NAMESPACE_ELIGIBLE) {
      return result;
    }
  }
  if (!identify(disk, namespace, NVME_IDENTIFY_NAMESPACE)) {
    *reason = "namespace identify failed";
    return NVME_NAMESPACE_FAILED;
  }
  const uint8_t *data = (void *)disk->identify.address;
  unsigned flbas = data[NVME_NS_FLBAS];
  unsigned format = (flbas & 15) | ((flbas & 0x60) >> 1);
  if (data[NVME_NS_NLBAF] >= NVME_NS_LBA_FORMAT_COUNT || (flbas & 0x80) ||
      format > data[NVME_NS_NLBAF] || (data[NVME_NS_DPS] & 7)) {
    *reason = "unsupported namespace protection or format index";
    return NVME_NAMESPACE_REFUSED;
  }
  const uint8_t *lbaf = data + NVME_NS_LBA_FORMATS + format * 4;
  if (get16(lbaf) || (lbaf[2] != 9 && lbaf[2] != 12)) {
    *reason = "metadata or non-512/4096 logical block format";
    return NVME_NAMESPACE_REFUSED;
  }
  uint32_t block_size = 1u << lbaf[2];
  uint64_t block_count = get64(data);
  uint64_t capacity = get64(data + 8);
  if ((data[NVME_NS_FEATURES] & NVME_NS_THIN_PROVISIONING) ||
      !capacity || capacity != block_count ||
      block_count > UINT64_MAX / block_size ||
      disk->info.max_transfer < block_size) {
    *reason = "invalid or thin-provisioned namespace capacity";
    return NVME_NAMESPACE_REFUSED;
  }
  disk->namespace = namespace;
  disk->info.block_size = block_size;
  disk->info.block_count = block_count;
  return NVME_NAMESPACE_ELIGIBLE;
}
static void report_namespace(const struct nvme_controller *disk, uint32_t namespace,
    const char *reason)
{
  struct pci_address address = disk->claim.device->address;
  kwarn("nvme PCI %x:%x.%u: namespace %u refused: %s\n",
      address.bus, address.device, address.function, namespace, reason);
}
/* The first eligible namespace in active-list order becomes the controller's
 * block device. At most eight are examined; the rest are reported unserved. */
static const char *select_namespace(struct nvme_controller *disk)
{
  if (!identify(disk, 0, NVME_IDENTIFY_ACTIVE_NS)) {
    return "active namespace identify failed";
  }
  const uint8_t *data = (void *)disk->identify.address;
  uint32_t candidates[NVME_NAMESPACE_EXAMINE_LIMIT + 1];
  unsigned active = 0;
  while (active < NVME_ACTIVE_LIST_ENTRIES && get32(data + active * 4)) {
    if (active <= NVME_NAMESPACE_EXAMINE_LIMIT) {
      candidates[active] = get32(data + active * 4);
    }
    ++active;
  }
  for (unsigned i = active; i < NVME_ACTIVE_LIST_ENTRIES; ++i) {
    if (get32(data + i * 4)) {
      return unsupported_geometry(disk, "malformed active namespace list");
    }
  }
  if (!active) {
    return unsupported_geometry(disk, "no active namespace");
  }
  uint32_t last = get32(data + (active - 1) * 4);
  unsigned limit = active < NVME_NAMESPACE_EXAMINE_LIMIT ? active : NVME_NAMESPACE_EXAMINE_LIMIT;
  for (unsigned i = 0; i < limit; ++i) {
    const char *reason = NULL;
    enum nvme_namespace_result result = examine_namespace(disk, candidates[i], &reason);
    if (result == NVME_NAMESPACE_FAILED) {
      return reason;
    }
    if (result == NVME_NAMESPACE_REFUSED) {
      report_namespace(disk, candidates[i], reason);
      continue;
    }
    struct pci_address address = disk->claim.device->address;
    unsigned unserved = active - i - 1;
    if (unserved == 1) {
      kwarn("nvme PCI %x:%x.%u: namespace %u not served: one namespace per controller\n",
          address.bus, address.device, address.function, last);
    } else if (unserved) {
      /* A full list may continue beyond its last entry. */
      kwarn("nvme PCI %x:%x.%u: namespaces %u to %u (%u%s) not served: one namespace per controller\n",
          address.bus, address.device, address.function, candidates[i + 1], last, unserved,
          active == NVME_ACTIVE_LIST_ENTRIES ? " or more" : "");
    }
    return NULL;
  }
  return unsupported_geometry(disk, active > limit ?
      "no eligible namespace among the first eight active" : "no eligible active namespace");
}
static const char *identify_geometry(struct nvme_controller *disk)
{
  const char *failure = identify_controller(disk);
  if (!failure) {
    failure = select_namespace(disk);
  }
  if (failure) {
    return failure;
  }
  disk->info.writable = boot_options_get()->nvme_write;
  disk->info.flush_supported = disk->info.writable;
  disk->info.raw_claim_requires_blank = true;
  return NULL;
}

static void notify_worker(struct nvme_controller *disk)
{
  disk->notified = true;
  struct task_wait *wait = disk->worker_wait;
  disk->worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}
/* The shared vector cannot name its controller, so every active controller's
 * worker is notified. Single MSI is sent only when interrupt status rises or a
 * set status is unmasked (NVMe 1.3 7.5.1). The handler masks vector zero; the
 * worker unmasks it after acknowledging the completions it found. */
void nvme_interrupt(void)
{
  for (size_t i = 0; i < controller_count; ++i) {
    struct nvme_controller *disk = &controllers[i];
    if (disk->interrupt_ready && !disk->stopped) {
      if (disk->msi_only) {
        write_register(disk, NVME_INTMS, NVME_INTERRUPT_VECTOR_ZERO);
      }
      notify_worker(disk);
    }
  }
}
static struct nvme_slot *find_ticket(struct nvme_controller *disk, const struct block_ticket *ticket)
{
  if (!disk || !ticket || ticket->device != disk->id || !ticket->generation ||
      ticket->slot >= disk->info.request_slots) {
    return NULL;
  }
  struct nvme_slot *slot = &disk->slots[ticket->slot];
  return slot->state != NVME_FREE && !slot->abandoned && slot->generation == ticket->generation ? slot : NULL;
}
enum block_result nvme_get_info(block_device_id id, struct block_info *info)
{
  require_client();
  struct nvme_controller *disk = find_device(id);
  if (!disk || !info) {
    return BLOCK_INVALID;
  }
  if (!disk->accepting) {
    return BLOCK_UNAVAILABLE;
  }
  *info = disk->info;
  return BLOCK_OK;
}
enum block_result nvme_submit(block_device_id id, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes,
    struct block_ticket *ticket)
{
  require_client();
  struct nvme_controller *disk = find_device(id);
  if (!disk || !ticket || (operation != BLOCK_READ && operation != BLOCK_WRITE && operation != BLOCK_FLUSH) ||
      (operation == BLOCK_WRITE ? !write_bytes : write_bytes != NULL)) {
    return BLOCK_INVALID;
  }
  if (!disk->accepting || disk->generation == UINT64_MAX) {
    return BLOCK_UNAVAILABLE;
  }
  if (operation != BLOCK_READ && !disk->info.writable) {
    return BLOCK_READ_ONLY;
  }
  if (operation != BLOCK_READ && disk->info.write_failed) {
    return BLOCK_WRITE_FAILED;
  }
  size_t bytes = 0;
  if (operation == BLOCK_FLUSH) {
    if (first_block || block_count) {
      return BLOCK_INVALID;
    }
  } else {
    if (!block_count || block_count > disk->info.max_transfer / disk->info.block_size ||
        first_block >= disk->info.block_count || block_count > disk->info.block_count - first_block) {
      return BLOCK_INVALID;
    }
    bytes = (size_t)block_count * disk->info.block_size;
  }
  unsigned index = 0;
  while (index < disk->info.request_slots && disk->slots[index].state != NVME_FREE) {
    ++index;
  }
  if (index == disk->info.request_slots) {
    return BLOCK_FULL;
  }
  struct nvme_slot *slot = &disk->slots[index];
  if (operation == BLOCK_WRITE) {
    memcpy((void *)slot->data.address, write_bytes, bytes);
  }
  slot->operation = operation;
  slot->generation = ++disk->generation;
  slot->first_block = first_block;
  slot->bytes = bytes;
  slot->completion = (struct block_completion){0};
  slot->abandoned = slot->device_owned = false;
  slot->state = NVME_QUEUED;
  *ticket = (struct block_ticket){.device = id, .slot = index, .generation = slot->generation};
  notify_worker(disk);
  return BLOCK_OK;
}
enum block_result nvme_collect(const struct block_ticket *ticket, void *read_bytes,
    size_t read_capacity, struct block_completion *completion)
{
  require_client();
  struct nvme_controller *disk = ticket ? find_device(ticket->device) : NULL;
  struct nvme_slot *slot = find_ticket(disk, ticket);
  if (!slot || !completion) {
    return BLOCK_INVALID;
  }
  if (slot->wait) {
    return BLOCK_BUSY;
  }
  if (slot->state != NVME_DONE) {
    return BLOCK_PENDING;
  }
  if (slot->operation == BLOCK_READ && slot->completion.result == BLOCK_OK) {
    if (!read_bytes || read_capacity < slot->bytes) {
      return BLOCK_INVALID;
    }
    memcpy(read_bytes, (const void *)slot->data.address, slot->bytes);
  }
  *completion = slot->completion;
  slot->state = NVME_FREE;
  return BLOCK_OK;
}
enum block_result nvme_abandon(const struct block_ticket *ticket)
{
  require_client();
  struct nvme_controller *disk = ticket ? find_device(ticket->device) : NULL;
  struct nvme_slot *slot = find_ticket(disk, ticket);
  if (!slot) {
    return BLOCK_INVALID;
  }
  if (slot->wait) {
    return BLOCK_BUSY;
  }
  if (slot->operation != BLOCK_READ && slot->completion.submitted) {
    disk->info.write_failed = true;
  }
  if (slot->device_owned) {
    slot->abandoned = true;
  } else {
    slot->state = NVME_FREE;
  }
  notify_worker(disk);
  return BLOCK_OK;
}
enum block_result nvme_wait(const struct block_ticket *ticket, uint64_t deadline_ns)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  struct nvme_controller *disk = ticket ? find_device(ticket->device) : NULL;
  for (;;) {
    struct nvme_slot *slot = find_ticket(disk, ticket);
    enum block_result result;
    if (!slot) {
      result = BLOCK_INVALID;
    } else if (slot->wait) {
      result = BLOCK_BUSY;
    } else if (slot->state == NVME_DONE) {
      result = BLOCK_OK;
    } else if (task_deadline_expired(deadline_ns)) {
      result = BLOCK_TIMED_OUT;
    } else {
      struct task_wait *wait = task_wait_prepare();
      slot->wait = wait;
      task_wait_sleep_until(wait, deadline_ns);
      slot = find_ticket(disk, ticket);
      if (slot && slot->wait == wait) {
        slot->wait = NULL;
      }
      continue;
    }
    cpu_restore_interrupts(flags);
    return result;
  }
}
static void finish_slot(struct nvme_slot *slot, enum block_result result)
{
  slot->completion.result = result;
  slot->completion.bytes = result == BLOCK_OK ? slot->bytes : 0;
  slot->state = slot->abandoned ? NVME_FREE : NVME_DONE;
  struct task_wait *wait = slot->wait;
  slot->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}
static bool interrupt_delivery(struct nvme_controller *disk, bool enabled)
{
  if (disk->msi_only) {
    return enabled ? pci_msi_enable(&disk->msi) : pci_msi_disable(&disk->msi);
  }
  return enabled ? pci_msix_enable(&disk->msix) : pci_msix_disable(&disk->msix);
}
static void stop_controller(struct nvme_controller *disk, const char *reason)
{
  uint64_t flags = cpu_save_interrupts();
  disk->accepting = disk->interrupt_ready = false;
  disk->stopped = true;
  bool masked = interrupt_delivery(disk, false);
  uint16_t command = pci_read16(disk->claim.device->address, PCI_COMMAND);
  pci_write16(&disk->claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  for (unsigned i = 0; i < disk->info.request_slots; ++i) {
    struct nvme_slot *slot = &disk->slots[i];
    if (slot->state == NVME_QUEUED || slot->state == NVME_ACTIVE) {
      if (slot->operation != BLOCK_READ && slot->completion.submitted) {
        disk->info.write_failed = true;
      }
      finish_slot(slot, BLOCK_UNAVAILABLE);
    }
  }
  cpu_restore_interrupts(flags);
  bool reset = halt_controller(disk, false);
  if (reset) {
    flags = cpu_save_interrupts();
    for (unsigned i = 0; i < disk->info.request_slots; ++i) {
      disk->slots[i].device_owned = false;
      if (disk->slots[i].abandoned) {
        disk->slots[i].state = NVME_FREE;
      }
    }
    cpu_restore_interrupts(flags);
  }
  kerror("nvme %u: %s; stopped (reset=%u interrupts masked=%u), DMA retained\n",
      disk->id, reason, (unsigned)reset, (unsigned)masked);
}
static const char *complete_io(struct nvme_controller *disk, bool *worked)
{
  for (unsigned n = 0; n < disk->io.depth; ++n) {
    struct nvme_completion value;
    int ready = queue_peek(&disk->io, &value);
    if (ready < 0) {
      return "invalid completion queue metadata";
    }
    if (!ready) {
      return NULL;
    }
    uint64_t flags = cpu_save_interrupts();
    struct nvme_slot *slot = NULL;
    for (unsigned i = 0; i < disk->info.request_slots; ++i) {
      if (disk->slots[i].state == NVME_ACTIVE && disk->slots[i].device_owned && disk->slots[i].cid == value.cid) {
        slot = &disk->slots[i];
        break;
      }
    }
    if (!slot) {
      cpu_restore_interrupts(flags);
      return "unknown or duplicate completion CID";
    }
    if (!submission_consumed(&disk->io, &value, slot->sequence)) {
      cpu_restore_interrupts(flags);
      return "completion precedes submission consumption";
    }
    queue_ack(disk, &disk->io, &value);
    slot->device_owned = false;
    enum block_result result = (value.status & NVME_STATUS_CODE_MASK) ? BLOCK_IO_ERROR : BLOCK_OK;
    if (result != BLOCK_OK && slot->operation != BLOCK_READ) {
      disk->info.write_failed = true;
    }
    ++disk->completed;
    finish_slot(slot, result);
    cpu_restore_interrupts(flags);
    *worked = true;
  }
  return "completion queue overrun";
}
static unsigned active_requests(const struct nvme_controller *disk)
{
  unsigned count = 0;
  for (unsigned i = 0; i < disk->info.request_slots; ++i) {
    count += disk->slots[i].state == NVME_ACTIVE;
  }
  return count;
}
static uint16_t next_cid(struct nvme_controller *disk)
{
  for (;;) {
    uint16_t cid = ++disk->next_cid;
    bool in_use = false;
    for (unsigned i = 0; i < disk->info.request_slots; ++i) {
      in_use |= disk->slots[i].state == NVME_ACTIVE && disk->slots[i].cid == cid;
    }
    if (!in_use) {
      return cid;
    }
  }
}
static const char *publish_requests(struct nvme_controller *disk, bool *worked)
{
  uint64_t flags = cpu_save_interrupts();
  for (unsigned i = 0; i < disk->info.request_slots; ++i) {
    if (disk->slots[i].state == NVME_ACTIVE && disk->slots[i].operation == BLOCK_FLUSH) {
      cpu_restore_interrupts(flags);
      return NULL;
    }
  }
  for (;;) {
    struct nvme_slot *slot = NULL;
    for (unsigned i = 0; i < disk->info.request_slots; ++i) {
      struct nvme_slot *candidate = &disk->slots[i];
      if (candidate->state == NVME_QUEUED && (!slot || candidate->generation < slot->generation)) {
        slot = candidate;
      }
    }
    if (!slot || disk->io.descriptors == disk->io.depth - 1) {
      break;
    }
    if (slot->operation != BLOCK_READ && disk->info.write_failed) {
      finish_slot(slot, BLOCK_WRITE_FAILED);
      *worked = true;
      continue;
    }
    if (slot->operation == BLOCK_FLUSH && active_requests(disk)) {
      break;
    }
    struct nvme_command command = {
      .opcode = slot->operation == BLOCK_READ ? NVME_NVM_READ :
          slot->operation == BLOCK_WRITE ? NVME_NVM_WRITE : NVME_NVM_FLUSH,
      .cid = next_cid(disk), .nsid = disk->namespace,
    };
    if (slot->operation != BLOCK_FLUSH) {
      command.prp1 = slot->data.physical;
      unsigned pages = (slot->bytes + NVME_PAGE_BYTES - 1) / NVME_PAGE_BYTES;
      if (pages == 2) {
        command.prp2 = slot->data.physical + NVME_PAGE_BYTES;
      } else if (pages > 2) {
        uint64_t *prps = (void *)slot->prps.address;
        for (unsigned i = 1; i < pages; ++i) {
          prps[i - 1] = slot->data.physical + i * NVME_PAGE_BYTES;
        }
        command.prp2 = slot->prps.physical;
      }
      command.cdw10 = slot->first_block;
      command.cdw11 = slot->first_block >> 32;
      command.cdw12 = slot->bytes / disk->info.block_size - 1;
    }
    if (disk->shutdown_requested && arch_monotonic_ns() >= disk->shutdown_deadline) {
      cpu_restore_interrupts(flags);
      return "shutdown publication deadline expired";
    }
    slot->cid = command.cid;
    slot->deadline = arch_monotonic_ns() + NVME_COMMAND_TIMEOUT_NS;
    slot->state = NVME_ACTIVE;
    slot->device_owned = true;
    slot->completion.submitted = true;
    if (!queue_post(disk, &disk->io, &command)) {
      slot->state = NVME_QUEUED;
      slot->device_owned = false;
      slot->completion.submitted = false;
      cpu_restore_interrupts(flags);
      return "submission queue accounting changed";
    }
    slot->sequence = disk->io.posted;
    ++disk->published;
    if (slot->operation == BLOCK_WRITE) {
      disk->written = slot->bytes > UINT64_MAX - disk->written ? UINT64_MAX : disk->written + slot->bytes;
    }
    *worked = true;
    if (slot->operation == BLOCK_FLUSH) {
      break;
    }
  }
  cpu_restore_interrupts(flags);
  return NULL;
}
static bool any_queued(struct nvme_controller *disk)
{
  for (unsigned i = 0; i < disk->info.request_slots; ++i) {
    if (disk->slots[i].state == NVME_QUEUED) {
      return true;
    }
  }
  return false;
}
static void shutdown_controller(struct nvme_controller *disk)
{
  bool success = smart_read(disk, false);
  if (success) {
    success = admin_command(disk, (struct nvme_command){.opcode = NVME_ADMIN_DELETE_SQ, .cdw10 = NVME_IO_QUEUE}, false, NULL);
  }
  if (success) {
    success = admin_command(disk, (struct nvme_command){.opcode = NVME_ADMIN_DELETE_CQ, .cdw10 = NVME_IO_QUEUE}, false, NULL);
  }
  if (success) {
    uint64_t write_flags = cpu_save_interrupts();
    success = !disk->stopped && arch_monotonic_ns() < disk->shutdown_deadline;
    if (success) {
      disk->cc |= NVME_CC_SHUTDOWN;
      write_register(disk, NVME_CC, disk->cc);
    }
    cpu_restore_interrupts(write_flags);
    uint64_t deadline = arch_monotonic_ns() + (uint64_t)disk->shutdown_us * 1000;
    if (deadline > disk->shutdown_deadline) {
      deadline = disk->shutdown_deadline;
    }
    while (success) {
      uint32_t status = read_register(disk, NVME_CSTS);
      if ((status & (NVME_CSTS_FATAL | NVME_CSTS_SHUTDOWN_MASK)) == NVME_CSTS_SHUTDOWN_DONE) {
        break;
      }
      if (status == UINT32_MAX || (status & NVME_CSTS_FATAL) || arch_monotonic_ns() >= deadline || disk->stopped) {
        success = false;
        break;
      }
      pause_poll(false);
    }
  }
  uint64_t flags = cpu_save_interrupts();
  disk->accepting = disk->interrupt_ready = false;
  disk->stopped = true;
  bool masked = interrupt_delivery(disk, false);
  disk->shutdown_result = success && masked ? CALL_OK : CALL_UNAVAILABLE;
  disk->shutdown_done = true;
  cpu_restore_interrupts(flags);
  kinfo("nvme %u: normal shutdown %s; published write bytes=%lu; retired until reboot\n",
      disk->id, disk->shutdown_result == CALL_OK ? "confirmed" : "unconfirmed", disk->written);
}
static void wait_for_work(struct nvme_controller *disk)
{
  uint64_t flags = cpu_save_interrupts();
  uint64_t deadline = UINT64_MAX;
  for (unsigned i = 0; i < disk->info.request_slots; ++i) {
    struct nvme_slot *slot = &disk->slots[i];
    if (slot->state == NVME_ACTIVE && slot->deadline < deadline) {
      deadline = slot->deadline;
    }
  }
  if (disk->shutdown_requested && disk->shutdown_deadline < deadline) {
    deadline = disk->shutdown_deadline;
  }
  /* Every completion found so far is acknowledged. Unmasking with status
   * still set sends a new message, which is taken once this wait sleeps. */
  if (disk->msi_only && disk->interrupt_ready) {
    write_register(disk, NVME_INTMC, NVME_INTERRUPT_VECTOR_ZERO);
  }
  if (disk->notified) {
    disk->notified = false;
  } else {
    KASSERT(!disk->worker_wait);
    disk->worker_wait = task_wait_prepare();
    if (deadline == UINT64_MAX) {
      task_wait_sleep(disk->worker_wait);
    } else {
      task_wait_sleep_until(disk->worker_wait, deadline);
    }
    disk->worker_wait = NULL;
    disk->notified = false;
  }
  cpu_restore_interrupts(flags);
}
static void nvme_worker(void *argument)
{
  struct nvme_controller *disk = argument;
  uint64_t flags = cpu_save_interrupts();
  bool activated = controller_ready(disk) && interrupt_delivery(disk, true);
  disk->interrupt_ready = activated;
  cpu_restore_interrupts(flags);
  if (!activated) {
    stop_controller(disk, "interrupt activation failed");
    return;
  }
  kinfo("nvme %u: namespace %u ready, %lu blocks of %u bytes, transfer=%u slots=%u %s\n",
      disk->id, disk->namespace, disk->info.block_count, disk->info.block_size,
      disk->info.max_transfer, disk->info.request_slots, disk->info.writable ? "writable with flush" : "read-only");
  for (;;) {
    if (disk->stopped) {
      return;
    }
    if (disk->shutdown_requested && arch_monotonic_ns() >= disk->shutdown_deadline) {
      stop_controller(disk, "shutdown deadline expired");
      return;
    }
    bool worked = false;
    const char *failure = controller_ready(disk) ? complete_io(disk, &worked) : "controller state changed";
    flags = cpu_save_interrupts();
    for (unsigned i = 0; !failure && i < disk->info.request_slots; ++i) {
      if (disk->slots[i].state == NVME_ACTIVE && task_deadline_expired(disk->slots[i].deadline)) {
        failure = "device request timed out";
      }
    }
    cpu_restore_interrupts(flags);
    if (!failure) {
      failure = publish_requests(disk, &worked);
    }
    if (failure) {
      stop_controller(disk, failure);
      return;
    }
    if (disk->shutdown_requested) {
      if (!active_requests(disk) && !any_queued(disk)) {
        shutdown_controller(disk);
        return;
      }
      if (arch_monotonic_ns() >= disk->shutdown_deadline) {
        stop_controller(disk, "shutdown drain deadline expired");
        return;
      }
    }
    if (worked) {
      kernel_task_sleep_until(arch_monotonic_ns());
    } else {
      wait_for_work(disk);
    }
  }
}
void nvme_start(void)
{
  for (size_t i = 0; i < controller_count; ++i) {
    struct nvme_controller *disk = &controllers[i];
    if (!disk->prepared) {
      continue;
    }
    enum mm_result result = kernel_task_create(nvme_worker, disk);
    if (result != MM_OK) {
      kerror("nvme %u: worker creation failed; controller retained\n", disk->id);
      continue;
    }
    disk->accepting = true;
  }
}
enum call_status nvme_shutdown(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t entry_flags = cpu_save_interrupts();
  KASSERT(entry_flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(entry_flags);
  enum call_status result = CALL_OK;
  uint64_t deadline = arch_monotonic_ns() + NVME_SHUTDOWN_BUDGET_NS;
  uint64_t flags = cpu_save_interrupts();
  for (size_t i = 0; i < controller_count; ++i) {
    struct nvme_controller *disk = &controllers[i];
    if (!disk->shutdown_requested && !disk->stopped && disk->accepting) {
      disk->accepting = false;
      disk->shutdown_requested = true;
      disk->shutdown_deadline = deadline;
      notify_worker(disk);
    }
  }
  cpu_restore_interrupts(flags);
  for (size_t i = 0; i < controller_count; ++i) {
    struct nvme_controller *disk = &controllers[i];
    if (!disk->claim.dma_started) {
      continue;
    }
    if (!disk->shutdown_requested) {
      result = CALL_UNAVAILABLE;
      continue;
    }
    while (!disk->shutdown_done && !disk->stopped && arch_monotonic_ns() < disk->shutdown_deadline) {
      kernel_task_sleep_until(arch_monotonic_ns() + NVME_POLL_NS);
    }
    if (!disk->shutdown_done || disk->shutdown_result != CALL_OK) {
      /* The controller worker alone retires DMA, masks IRQs and completes
       * tickets. A caller deadline never bypasses that cleanup. Admission is
       * already sealed; its deadline prevents further publication. */
      flags = cpu_save_interrupts();
      notify_worker(disk);
      cpu_restore_interrupts(flags);
      result = CALL_UNAVAILABLE;
    }
  }
  return result;
}

static bool region_overlap(const struct pci_region *region, unsigned bar,
    uint64_t offset, uint64_t bytes)
{
  return region->bar == bar && region->offset < offset + bytes &&
      offset < (uint64_t)region->offset + region->length;
}
static bool has_msix(const struct pci_claim *claim)
{
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    if (pci_read8(claim->device->address, claim->capabilities[i]) == PCI_CAP_MSIX) {
      return true;
    }
  }
  return false;
}
static const char *prepare_transport(struct nvme_controller *disk, struct pci_device *device,
    const struct boot_info *boot)
{
  struct pci_probe_state probe = {0};
  if (!pci_reserve_device(device, &disk->claim) || !pci_begin_mmio_probe(&disk->claim, &probe)) {
    return "cannot reserve controller or obtain firmware MMIO access";
  }
  if (pci_map_bootstrap_bar(&disk->claim, 0, 0, PCI_BOOTSTRAP_BAR_BYTES, boot, &disk->registers) != MM_OK) {
    return "cannot map controller register prefix";
  }
  disk->cap = read_register64(disk, NVME_CAP);
  disk->version = read_register(disk, NVME_VS);
  unsigned minimum_page = (disk->cap >> 48) & 15, maximum_page = (disk->cap >> 52) & 15;
  if (disk->cap == UINT64_MAX || disk->version == UINT32_MAX ||
      !(disk->cap & (NVME_CAP_NVM | NVME_CAP_IO_COMMAND_SETS)) || minimum_page ||
      maximum_page < minimum_page || disk->version < 0x10300u) {
    disk->preparation = BLOCK_DEVICE_UNSUPPORTED;
    return "requires NVMe 1.3 or newer, an NVM-capable command set and 4 KiB pages";
  }
  if ((disk->cap & NVME_CAP_READY_INDEPENDENT) && !(disk->cap & NVME_CAP_READY_WITH_MEDIA)) {
    return unsupported_geometry(disk, "invalid controller-ready mode capabilities");
  }
  if (disk->version >= 0x20000u && !(disk->cap & NVME_CAP_READY_WITH_MEDIA)) {
    return unsupported_geometry(disk, "NVMe 2.0 controller lacks ready-with-media capability");
  }
  disk->reset_timeout = bounded_ready_timeout((disk->cap >> 24) & 255);
  disk->stride = 4u << ((disk->cap >> 32) & 15);
  unsigned maximum_depth = (disk->cap & 0xffff) + 1;
  unsigned depth = NVME_IO_DEPTH;
  while (depth > maximum_depth) {
    depth /= 2;
  }
  if (depth < 2) {
    disk->preparation = BLOCK_DEVICE_UNSUPPORTED;
    return "I/O queue cannot hold a request";
  }
  disk->info.request_slots = depth - 1 < NVME_SLOT_LIMIT ? depth - 1 : NVME_SLOT_LIMIT;
  /* Firmware DMA stays enabled until the controller's own halt confirms the
   * ownership handoff. BAR sizing must not run before that confirmation. */
  if (!halt_controller(disk, true) || !refresh_capabilities(disk) ||
      !pci_complete_claim(&disk->claim)) {
    return "firmware controller did not halt";
  }
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  pci_write16(&disk->claim, PCI_COMMAND, command & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));
  if (!pci_size_bars(&disk->claim)) {
    return "BAR sizing failed";
  }
  uint64_t doors = (uint64_t)disk->stride * 4;
  disk->msi_only = !has_msix(&disk->claim);
  if (!disk->claim.bars[0].physical ||
      disk->claim.bars[0].bytes < NVME_DOORBELLS + doors ||
      (!disk->msi_only && !pci_msix_discover(&disk->claim, &disk->msix))) {
    disk->preparation = BLOCK_DEVICE_UNSUPPORTED;
    return "BAR0 or MSI-X layout unsupported";
  }
  if (!disk->msi_only &&
      (region_overlap(&disk->msix.table, 0, 0, NVME_PAGE_BYTES) ||
       region_overlap(&disk->msix.pba, 0, 0, NVME_PAGE_BYTES) ||
       region_overlap(&disk->msix.table, 0, NVME_DOORBELLS, doors) ||
       region_overlap(&disk->msix.pba, 0, NVME_DOORBELLS, doors))) {
    disk->preparation = BLOCK_DEVICE_UNSUPPORTED;
    return "MSI-X regions overlap NVMe registers or doorbells";
  }
  if (pci_map_bar(&disk->claim, 0, NVME_DOORBELLS, doors, boot, &disk->doorbells) != MM_OK ||
      (!disk->msi_only && pci_msix_map(&disk->msix, boot) != MM_OK)) {
    return "cannot map controller doorbells or MSI-X";
  }
  pci_write16(&disk->claim, PCI_COMMAND, (command | PCI_COMMAND_MEMORY) & ~PCI_COMMAND_MASTER);
  if (read_register64(disk, NVME_CAP) != disk->cap ||
      (read_register(disk, NVME_CSTS) & NVME_CSTS_READY)) {
    return "controller changed during resource preparation";
  }
  enum mm_result result = allocate_queue(&disk->admin, 0, NVME_ADMIN_DEPTH);
  if (result == MM_OK) {
    result = allocate_queue(&disk->io, NVME_IO_QUEUE, depth);
  }
  if (result == MM_OK) {
    result = dma_buffer_allocate(&disk->identify, NVME_PAGE_BYTES);
  }
  if (result == MM_OK) {
    result = dma_buffer_allocate(&disk->smart, NVME_PAGE_BYTES);
  }
  for (unsigned i = 0; i < disk->info.request_slots && result == MM_OK; ++i) {
    result = dma_buffer_allocate(&disk->slots[i].data, NVME_TRANSFER_LIMIT);
    if (result == MM_OK) {
      result = dma_buffer_allocate(&disk->slots[i].prps, NVME_PAGE_BYTES);
    }
  }
  if (result != MM_OK) {
    return "DMA allocation failed";
  }
  /* A function without MSI-X or a usable MSI capability is refused; pin
   * interrupts are not routed. */
  if (disk->msi_only ? !pci_msi_prepare(&disk->claim, &disk->msi, APIC_NVME_VECTOR) :
      !pci_msix_prepare(&disk->msix, APIC_NVME_VECTOR)) {
    return disk->msi_only ?
        unsupported_geometry(disk, "MSI layout or single-message programming refused") :
        "MSI-X preparation failed";
  }
  disk->cc = NVME_CC_QUEUE_SIZES;
  if (!(disk->cap & NVME_CAP_NVM)) {
    disk->cc |= NVME_CC_IO_COMMAND_SETS;
  }
  write_register(disk, NVME_CC, disk->cc);
  if (!refresh_capabilities(disk) || read_register(disk, NVME_CC) != disk->cc ||
      (read_register(disk, NVME_CSTS) & NVME_CSTS_READY)) {
    return "controller changed during disabled configuration";
  }
  write_register(disk, NVME_AQA, ((NVME_ADMIN_DEPTH - 1) << 16) | (NVME_ADMIN_DEPTH - 1));
  write_register64(disk, NVME_ASQ, disk->admin.sq.physical);
  write_register64(disk, NVME_ACQ, disk->admin.cq.physical);
  disk->cc |= NVME_CC_ENABLE;
  if (!pci_write16(&disk->claim, PCI_COMMAND, command | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ||
      !(pci_read16(device->address, PCI_COMMAND) & PCI_COMMAND_MASTER)) {
    return "PCI bus mastering did not enable";
  }
  write_register(disk, NVME_CC, disk->cc);
  disk->initialized = true;
  if (!wait_ready(disk, true)) {
    return "controller enable did not become ready";
  }
  const char *failure = identify_geometry(disk);
  if (failure) {
    return failure;
  }
  uint32_t allocation;
  if (!admin_command(disk, (struct nvme_command){
      .opcode = NVME_ADMIN_FEATURES, .cdw10 = NVME_FEATURE_QUEUE_COUNT,
    }, true, &allocation)) {
    return "one I/O queue pair negotiation failed";
  }
  if (!admin_command(disk, (struct nvme_command){
      .opcode = NVME_ADMIN_CREATE_CQ, .prp1 = disk->io.cq.physical,
      .cdw10 = ((depth - 1) << 16) | NVME_IO_QUEUE,
      .cdw11 = NVME_QUEUE_CONTIGUOUS | NVME_CQ_INTERRUPT,
    }, true, NULL) ||
      !admin_command(disk, (struct nvme_command){
      .opcode = NVME_ADMIN_CREATE_SQ, .prp1 = disk->io.sq.physical,
      .cdw10 = ((depth - 1) << 16) | NVME_IO_QUEUE,
      .cdw11 = (NVME_IO_QUEUE << 16) | NVME_QUEUE_CONTIGUOUS,
    }, true, NULL)) {
    return "I/O queue creation failed";
  }
  if (!smart_read(disk, true)) {
    return "SMART log read failed";
  }
  return NULL;
}
static bool nvme_candidate(const struct pci_device *device)
{
  return device->base_class == 1 && device->subclass == 8 && device->interface == 2;
}
void nvme_prepare(const struct boot_info *boot)
{
  require_client();
  KASSERT(!controllers && !controller_count);
  size_t count = 0;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    count += nvme_candidate(pci_device_at(i));
  }
  inventory_complete = pci_inventory_state() == PCI_INVENTORY_COMPLETE;
  if (count > UINT32_MAX || count > SIZE_MAX / sizeof(*controllers)) {
    inventory_complete = false;
    return;
  }
  if (count) {
    controllers = kmalloc(count * sizeof(*controllers));
    if (!controllers) {
      inventory_complete = false;
      kerror("nvme: cannot retain controller inventory\n");
      return;
    }
    memset(controllers, 0, count * sizeof(*controllers));
  }
  controller_count = count;
  size_t index = 0;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    struct pci_device *device = (struct pci_device *)pci_device_at(i);
    if (!nvme_candidate(device)) {
      continue;
    }
    struct nvme_controller *disk = &controllers[index];
    disk->id = (block_device_id)++index;
    disk->preparation = BLOCK_DEVICE_SETUP_FAILED;
    const char *failure = prepare_transport(disk, device, boot);
    if (failure) {
      bool reset = false, masked = false;
      if (disk->claim.dma_started) {
        masked = disk->msi_only ? disk->msi.claim && pci_msi_disable(&disk->msi) :
            disk->msix.table.mapping.address && pci_msix_disable(&disk->msix);
        reset = halt_controller(disk, true);
      }
      disk->stopped = true;
      kerror("nvme PCI %x:%x.%u: %s; %s (reset=%u IRQ masked=%u), ownership retained\n",
          device->address.bus, device->address.device, device->address.function, failure,
          disk->preparation == BLOCK_DEVICE_UNSUPPORTED ? "unsupported" : "setup failed",
          (unsigned)reset, (unsigned)masked);
      continue;
    }
    disk->prepared = true;
    disk->preparation = BLOCK_DEVICE_READY;
    kinfo("nvme PCI %x:%x.%u: namespace %u prepared, queues admin=%u I/O=%u, %s; NVMe data writes %s\n",
        device->address.bus, device->address.device, device->address.function,
        disk->namespace, disk->admin.depth, disk->io.depth, disk->msi_only ? "MSI" : "MSI-X",
        disk->info.writable ? "enabled" : "disabled");
  }
}

#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/block.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/virtio/blk.h>
#include <kernel-config.h>
#include "../usb/block.h"
#include "../usb/core.h"
#include "block_registry.h"

struct block_device {
  enum block_preparation preparation;
  struct usb_block_device *usb;
};

static struct block_device *devices;
static size_t device_count, device_capacity, native_count;
static bool retained;

static void assert_client_context(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

static struct block_device *find_device(block_device_id id)
{
  return id && id <= device_count ? &devices[id - 1] : NULL;
}

void block_prepare(void)
{
  assert_client_context();
  size_t virtio_count = virtio_blk_device_count();
  size_t usb_count = 0;
#ifdef CONFIG_XHCI
  usb_count = usb_storage_capacity();
#endif
  if (usb_count > SIZE_MAX - virtio_count || virtio_count + usb_count > UINT32_MAX ||
      virtio_count + usb_count > SIZE_MAX / sizeof(*devices)) {
    return;
  }
  device_capacity = virtio_count + usb_count;
  if (device_capacity) {
    devices = kmalloc(device_capacity * sizeof(*devices));
    if (!devices) {
      device_capacity = 0;
      return;
    }
  }
  retained = true;
  for (size_t i = 0; i < virtio_count; ++i) {
    block_device_id id = (block_device_id)++device_count;
    devices[i] = (struct block_device){
      .preparation = virtio_blk_preparation_result(virtio_blk_device_at(i)),
    };
    virtio_blk_set_id(i, id);
  }
  native_count = device_count;
}

size_t block_registry_capacity(void)
{
  assert_client_context();
  return device_capacity;
}

void block_register_usb(struct usb_block_device *usb)
{
  assert_client_context();
  if (!usb || device_count == device_capacity) {
    retained = false;
    return;
  }
  devices[device_count] = (struct block_device){
    .usb = usb, .preparation = usb_block_preparation(usb),
  };
  usb_block_set_id(usb, (block_device_id)++device_count);
  enum block_preparation preparation = devices[device_count - 1].preparation;
  klog("block: USB device %u %s\n", (unsigned)device_count,
       preparation == BLOCK_DEVICE_READY ? "read-only ready" :
       preparation == BLOCK_DEVICE_UNSUPPORTED ? "unsupported" : "setup failed");
}

size_t block_device_count(void)
{
  assert_client_context();
  return device_count;
}

block_device_id block_device_at(size_t index)
{
  assert_client_context();
  return index < device_count ? (block_device_id)index + 1 : BLOCK_DEVICE_ID_NONE;
}

bool block_discovery_finished(void)
{
  assert_client_context();
#ifdef CONFIG_XHCI
  struct system_info_usb info;
  usb_inventory_read(&info);
  return info.state != SYSTEM_INFO_USB_INITIALIZING;
#else
  return true;
#endif
}

bool block_inventory_complete(void)
{
  assert_client_context();
  if (!retained || !virtio_blk_inventory_complete()) {
    return false;
  }
#ifdef CONFIG_XHCI
  struct system_info_usb info;
  usb_inventory_read(&info);
  return info.state == SYSTEM_INFO_USB_COMPLETE;
#else
  return true;
#endif
}

enum block_preparation block_preparation_result(block_device_id id)
{
  assert_client_context();
  struct block_device *device = find_device(id);
  return device ? device->preparation : BLOCK_DEVICE_INVALID;
}

/* Native filesystem and installer authority retain their qualified backend
 * domain while USB block/GPT access is being brought up inside the kernel. */
size_t block_native_device_count(void)
{
  assert_client_context();
  return native_count;
}

block_device_id block_native_device_at(size_t index)
{
  assert_client_context();
  return index < native_count ? (block_device_id)index + 1 : BLOCK_DEVICE_ID_NONE;
}

bool block_native_inventory_complete(void)
{
  assert_client_context();
  return native_count == virtio_blk_device_count() && virtio_blk_inventory_complete();
}

bool block_native_device(block_device_id id)
{
  assert_client_context();
  return id && id <= native_count;
}

enum block_result block_get_info(block_device_id id, struct block_info *info)
{
  assert_client_context();
  struct block_device *device = find_device(id);
  if (!device) {
    return BLOCK_INVALID;
  }
  return device->usb ? usb_block_get_info(device->usb, info) : virtio_blk_get_info(id, info);
}

enum block_result block_submit(block_device_id id, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes,
    struct block_ticket *ticket)
{
  assert_client_context();
  struct block_device *device = find_device(id);
  if (!device) {
    return BLOCK_INVALID;
  }
  return device->usb ? usb_block_submit(device->usb, operation, first_block, block_count,
      write_bytes, ticket) : virtio_blk_submit(id, operation, first_block, block_count,
      write_bytes, ticket);
}

enum block_result block_collect(const struct block_ticket *ticket, void *read_bytes,
    size_t read_capacity, struct block_completion *completion)
{
  assert_client_context();
  struct block_device *device = ticket ? find_device(ticket->device) : NULL;
  if (!device) {
    return BLOCK_INVALID;
  }
  return device->usb ? usb_block_collect(device->usb, ticket, read_bytes, read_capacity,
      completion) : virtio_blk_collect(ticket, read_bytes, read_capacity, completion);
}

enum block_result block_abandon(const struct block_ticket *ticket)
{
  assert_client_context();
  struct block_device *device = ticket ? find_device(ticket->device) : NULL;
  if (!device) {
    return BLOCK_INVALID;
  }
  return device->usb ? usb_block_abandon(device->usb, ticket) : virtio_blk_abandon(ticket);
}

enum block_result block_wait(const struct block_ticket *ticket, uint64_t deadline_ns)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  struct block_device *device = ticket ? find_device(ticket->device) : NULL;
  struct usb_block_device *usb = device ? device->usb : NULL;
  cpu_restore_interrupts(flags);
  if (!device) {
    return BLOCK_INVALID;
  }
  return usb ? usb_block_wait(usb, ticket, deadline_ns) : virtio_blk_wait(ticket, deadline_ns);
}

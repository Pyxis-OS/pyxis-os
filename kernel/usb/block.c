#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "block.h"
#include "bot.h"
#include "host.h"
#include "settings.h"

#define USB_BLOCK_SLOTS 2u

enum usb_block_slot_state { USB_BLOCK_FREE, USB_BLOCK_QUEUED, USB_BLOCK_ACTIVE, USB_BLOCK_DONE };

struct usb_block_slot {
  enum usb_block_slot_state state;
  uint64_t generation, first_block;
  uint32_t block_count;
  size_t bytes;
  void *data;
  struct block_completion completion;
  struct task_wait *wait;
  bool abandoned;
};

struct usb_block_device {
  struct usb_block_pool *pool;
  struct usb_bot *bot;
  block_device_id id;
  enum block_preparation preparation;
  uint64_t generation;
  struct usb_block_slot slots[USB_BLOCK_SLOTS];
  bool failed;
};

struct usb_block_pool {
  struct usb_host_controller *host;
  struct usb_block_device *devices;
  size_t capacity, count;
  void *buffers[USB_STORAGE_DEVICE_BUDGET][USB_BLOCK_SLOTS];
  unsigned buffers_used;
  bool failed;
};

static void assert_client_context(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

struct usb_block_pool *usb_block_prepare(struct usb_host_controller *host, size_t capacity)
{
  assert_client_context();
  if (!host || !capacity || capacity > SIZE_MAX / sizeof(struct usb_block_device)) {
    return NULL;
  }
  struct usb_block_pool *pool = kmalloc(sizeof(*pool));
  if (!pool) {
    return NULL;
  }
  *pool = (struct usb_block_pool){.host = host, .capacity = capacity};
  pool->devices = kmalloc(capacity * sizeof(*pool->devices));
  if (!pool->devices) {
    kfree(pool);
    return NULL;
  }
  memset(pool->devices, 0, capacity * sizeof(*pool->devices));
  for (unsigned i = 0; i < USB_STORAGE_DEVICE_BUDGET; ++i) {
    for (unsigned j = 0; j < USB_BLOCK_SLOTS; ++j) {
      pool->buffers[i][j] = kmalloc(USB_BULK_BYTES);
    }
  }
  return pool;
}

void usb_block_release_prepared(struct usb_block_pool *pool)
{
  assert_client_context();
  if (!pool) {
    return;
  }
  for (unsigned i = 0; i < USB_STORAGE_DEVICE_BUDGET; ++i) {
    for (unsigned j = 0; j < USB_BLOCK_SLOTS; ++j) {
      kfree(pool->buffers[i][j]);
    }
  }
  kfree(pool->devices);
  kfree(pool);
}

struct usb_block_device *usb_block_bind(struct usb_block_pool *pool, struct usb_bot *bot)
{
  assert_client_context();
  if (!pool || !bot || bot->state == USB_BOT_UNBOUND || pool->count == pool->capacity) {
    return NULL;
  }
  struct usb_block_device *device = &pool->devices[pool->count++];
  *device = (struct usb_block_device){
    .pool = pool, .bot = bot, .failed = pool->failed,
    .preparation = bot->state == USB_BOT_UNSUPPORTED ? BLOCK_DEVICE_UNSUPPORTED : BLOCK_DEVICE_SETUP_FAILED,
  };
  if (bot->state == USB_BOT_READY && !pool->failed && pool->buffers_used < USB_STORAGE_DEVICE_BUDGET) {
    unsigned index = pool->buffers_used++;
    if (pool->buffers[index][0] && pool->buffers[index][1]) {
      for (unsigned i = 0; i < USB_BLOCK_SLOTS; ++i) {
        device->slots[i].data = pool->buffers[index][i];
      }
      device->preparation = BLOCK_DEVICE_READY;
    }
  }
  return device;
}

enum block_preparation usb_block_preparation(const struct usb_block_device *device)
{
  assert_client_context();
  return device ? device->preparation : BLOCK_DEVICE_INVALID;
}

void usb_block_set_id(struct usb_block_device *device, block_device_id id)
{
  assert_client_context();
  KASSERT(device && !device->id && id);
  device->id = id;
}

static bool accepting(const struct usb_block_device *device)
{
  return device && device->id && device->preparation == BLOCK_DEVICE_READY &&
    !device->failed && !device->pool->failed;
}

enum block_result usb_block_get_info(struct usb_block_device *device, struct block_info *info)
{
  assert_client_context();
  if (!device || !info) {
    return BLOCK_INVALID;
  }
  if (!accepting(device)) {
    return BLOCK_UNAVAILABLE;
  }
  *info = (struct block_info){
    .block_count = device->bot->blocks, .block_size = device->bot->block_bytes,
    .max_transfer = USB_BULK_BYTES, .request_slots = USB_BLOCK_SLOTS,
  };
  return BLOCK_OK;
}

enum block_result usb_block_submit(struct usb_block_device *device, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes, struct block_ticket *ticket)
{
  assert_client_context();
  if (!device || !ticket || (operation != BLOCK_READ && operation != BLOCK_WRITE && operation != BLOCK_FLUSH) ||
      (operation == BLOCK_WRITE ? !write_bytes : write_bytes != NULL)) {
    return BLOCK_INVALID;
  }
  if (!accepting(device) || device->generation == UINT64_MAX) {
    return BLOCK_UNAVAILABLE;
  }
  if (operation != BLOCK_READ) {
    return BLOCK_READ_ONLY;
  }
  if (!block_count || block_count > USB_BULK_BYTES / device->bot->block_bytes ||
      first_block >= device->bot->blocks || block_count > device->bot->blocks - first_block) {
    return BLOCK_INVALID;
  }
  unsigned index = 0;
  while (index < USB_BLOCK_SLOTS && device->slots[index].state != USB_BLOCK_FREE) {
    ++index;
  }
  if (index == USB_BLOCK_SLOTS) {
    return BLOCK_FULL;
  }
  struct usb_block_slot *slot = &device->slots[index];
  KASSERT(!slot->wait);
  void *data = slot->data;
  *slot = (struct usb_block_slot){
    .state = USB_BLOCK_QUEUED, .generation = ++device->generation,
    .first_block = first_block, .block_count = block_count,
    .bytes = (size_t)block_count * device->bot->block_bytes, .data = data,
  };
  *ticket = (struct block_ticket){.device = device->id, .slot = index, .generation = slot->generation};
  usb_host_notify(device->pool->host);
  return BLOCK_OK;
}

static struct usb_block_slot *find_ticket(struct usb_block_device *device, const struct block_ticket *ticket)
{
  if (!device || !ticket || ticket->device != device->id || ticket->slot >= USB_BLOCK_SLOTS) {
    return NULL;
  }
  struct usb_block_slot *slot = &device->slots[ticket->slot];
  return slot->state != USB_BLOCK_FREE && !slot->abandoned && slot->generation == ticket->generation ? slot : NULL;
}

enum block_result usb_block_collect(struct usb_block_device *device, const struct block_ticket *ticket,
    void *read_bytes, size_t read_capacity, struct block_completion *completion)
{
  assert_client_context();
  struct usb_block_slot *slot = find_ticket(device, ticket);
  if (!slot || !completion) {
    return BLOCK_INVALID;
  }
  if (slot->wait) {
    return BLOCK_BUSY;
  }
  if (slot->state != USB_BLOCK_DONE) {
    return BLOCK_PENDING;
  }
  if (slot->completion.result == BLOCK_OK) {
    if (!read_bytes || read_capacity < slot->bytes) {
      return BLOCK_INVALID;
    }
    memcpy(read_bytes, slot->data, slot->bytes);
  }
  *completion = slot->completion;
  slot->state = USB_BLOCK_FREE;
  return BLOCK_OK;
}

enum block_result usb_block_abandon(struct usb_block_device *device, const struct block_ticket *ticket)
{
  assert_client_context();
  struct usb_block_slot *slot = find_ticket(device, ticket);
  if (!slot) {
    return BLOCK_INVALID;
  }
  if (slot->wait) {
    return BLOCK_BUSY;
  }
  slot->abandoned = true;
  if (slot->state != USB_BLOCK_ACTIVE) {
    slot->state = USB_BLOCK_FREE;
  }
  return BLOCK_OK;
}

enum block_result usb_block_wait(struct usb_block_device *device, const struct block_ticket *ticket,
    uint64_t deadline_ns)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  for (;;) {
    struct usb_block_slot *slot = find_ticket(device, ticket);
    enum block_result result;
    if (!slot) {
      result = BLOCK_INVALID;
    } else if (slot->wait) {
      result = BLOCK_BUSY;
    } else if (slot->state == USB_BLOCK_DONE) {
      result = BLOCK_OK;
    } else if (task_deadline_expired(deadline_ns)) {
      result = BLOCK_TIMED_OUT;
    } else {
      struct task_wait *wait = task_wait_prepare();
      slot->wait = wait;
      task_wait_sleep_until(wait, deadline_ns);
      slot = find_ticket(device, ticket);
      if (slot && slot->wait == wait) {
        slot->wait = NULL;
      }
      continue;
    }
    cpu_restore_interrupts(flags);
    return result;
  }
}

static void finish_slot(struct usb_block_slot *slot, enum block_result result)
{
  slot->completion.result = result;
  slot->completion.bytes = result == BLOCK_OK ? slot->bytes : 0;
  slot->state = slot->abandoned ? USB_BLOCK_FREE : USB_BLOCK_DONE;
  struct task_wait *wait = slot->wait;
  slot->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void fail_device(struct usb_block_device *device)
{
  device->failed = true;
  for (unsigned i = 0; i < USB_BLOCK_SLOTS; ++i) {
    if (device->slots[i].state == USB_BLOCK_QUEUED) {
      finish_slot(&device->slots[i], BLOCK_UNAVAILABLE);
    }
  }
}

void usb_block_fail(struct usb_block_pool *pool)
{
  assert_client_context();
  if (!pool) {
    return;
  }
  pool->failed = true;
  for (size_t i = 0; i < pool->count; ++i) {
    fail_device(&pool->devices[i]);
  }
}

void usb_block_process(struct usb_block_pool *pool)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
  if (!pool) {
    return;
  }
  for (size_t i = 0; i < pool->count; ++i) {
    struct usb_block_device *device = &pool->devices[i];
    for (unsigned request = 0; request < USB_BLOCK_SLOTS; ++request) {
      flags = cpu_save_interrupts();
      struct usb_block_slot *slot = NULL;
      for (unsigned j = 0; accepting(device) && j < USB_BLOCK_SLOTS; ++j) {
        struct usb_block_slot *candidate = &device->slots[j];
        if (candidate->state == USB_BLOCK_QUEUED && (!slot || candidate->generation < slot->generation)) {
          slot = candidate;
        }
      }
      if (!slot) {
        cpu_restore_interrupts(flags);
        break;
      }
      slot->state = USB_BLOCK_ACTIVE;
      cpu_restore_interrupts(flags);
      enum usb_bot_read_result outcome = usb_bot_read(device->bot, slot->first_block, slot->block_count,
          slot->data, USB_BULK_BYTES, task_deadline_after_ms(USB_BOT_TIMEOUT_MS), &slot->completion.submitted);
      flags = cpu_save_interrupts();
      enum block_result result = outcome == USB_BOT_READ_TIMED_OUT ? BLOCK_TIMED_OUT :
        outcome != USB_BOT_READ_OK ? BLOCK_IO_ERROR : pool->failed ? BLOCK_UNAVAILABLE : BLOCK_OK;
      finish_slot(slot, result);
      if (outcome == USB_BOT_READ_FAILED || outcome == USB_BOT_READ_TIMED_OUT || pool->failed) {
        fail_device(device);
      }
      cpu_restore_interrupts(flags);
    }
  }
}

#include <arch/acpi.h>
#include <arch/apic.h>
#include <arch/io_apic.h>
#include <arch/layout.h>
#include <arch/ps2.h>
#include <kernel/acpi.h>
#include <kernel/log.h>
#include <kernel/panic.h>

#define IO_APIC_SELECT 0x00
#define IO_APIC_WINDOW 0x10
#define IO_APIC_VERSION 0x01
#define IO_APIC_MAX_ENTRY_SHIFT 16
#define IO_APIC_MAX_ENTRY_MASK 0xff
#define IO_APIC_REDIRECTION_BASE 0x10
#define IO_APIC_REDIRECTION_REGISTERS 2
#define IO_APIC_DESTINATION_SHIFT 24
#define IO_APIC_DELIVERY_PENDING (1u << 12)
#define IO_APIC_ACTIVE_LOW (1u << 13)
#define IO_APIC_LEVEL_TRIGGERED (1u << 15)
#define IO_APIC_MASKED (1u << 16)

/* At most one controller per supported input; shared controllers use one slot. */
static struct io_apic_controller {
  uint64_t physical;
  uintptr_t address;
  unsigned max_entry;
} controllers[IO_APIC_SLOT_COUNT];
static size_t controller_count;

enum input_index {
  KEYBOARD_INPUT,
  MOUSE_INPUT,
  SCI_INPUT,
  INPUT_COUNT,
};

static struct input_route {
  struct isa_irq_route firmware;
  struct io_apic_controller *controller;
  uint32_t reg, entry;
  uint8_t vector;
  bool initialized, enabled;
} inputs[INPUT_COUNT];
static unsigned sci_irq;
static bool sci_masked;

static uint32_t io_apic_read(const struct io_apic_controller *controller, uint32_t reg)
{
  *(volatile uint32_t *)(controller->address + IO_APIC_SELECT) = reg;
  return *(volatile uint32_t *)(controller->address + IO_APIC_WINDOW);
}

static void io_apic_write(const struct io_apic_controller *controller,
                          uint32_t reg, uint32_t value)
{
  *(volatile uint32_t *)(controller->address + IO_APIC_SELECT) = reg;
  *(volatile uint32_t *)(controller->address + IO_APIC_WINDOW) = value;
}

static void prepare_input(struct input_route *input)
{
  uint64_t physical = input->firmware.io_apic_physical;
  if (!physical) {
    return;
  }
  for (size_t i = 0; i < controller_count; ++i) {
    if (controllers[i].physical == physical) {
      input->controller = &controllers[i];
      return;
    }
  }
  KASSERT(controller_count < IO_APIC_SLOT_COUNT);
  struct io_apic_controller *controller = &controllers[controller_count];
  *controller = (struct io_apic_controller){
    .physical = physical,
    .address = IO_APIC_BASE + controller_count * ARCH_PAGE_SIZE,
  };
  ++controller_count;
  input->controller = controller;
}

void io_apic_prepare(const struct boot_info *boot)
{
  acpi_ps2_routes(boot, &inputs[KEYBOARD_INPUT].firmware, &inputs[MOUSE_INPUT].firmware);
  acpi_sci_route(boot, &sci_irq, &inputs[SCI_INPUT].firmware);
  for (size_t i = 0; i < INPUT_COUNT; ++i) {
    prepare_input(&inputs[i]);
  }
}

size_t io_apic_controller_count(void)
{
  return controller_count;
}

uint64_t io_apic_physical_address(size_t index)
{
  KASSERT(index < controller_count);
  return controllers[index].physical;
}

static bool same_pin(const struct input_route *a, const struct input_route *b)
{
  return a->controller == b->controller && a->reg == b->reg;
}

static void update_pin(const struct input_route *input)
{
  bool enabled = false;
  for (size_t i = 0; i < INPUT_COUNT; ++i) {
    if (inputs[i].initialized && same_pin(input, &inputs[i])) {
      enabled |= inputs[i].enabled;
    }
  }
  const struct input_route *sci = &inputs[SCI_INPUT];
  if (sci->initialized && same_pin(input, sci) && sci_masked) {
    enabled = false;
  }
  io_apic_write(input->controller, input->reg,
                input->entry | (enabled ? 0 : IO_APIC_MASKED));
}

static void initialize_input(unsigned index, uint8_t vector, uint32_t destination)
{
  struct input_route *input = &inputs[index];
  if (!input->controller) {
    return;
  }
  const struct isa_irq_route *route = &input->firmware;
  unsigned pin = route->gsi - route->gsi_base;
  if (route->gsi < route->gsi_base || pin > input->controller->max_entry) {
    kerror("I/O APIC: input %u GSI %u outside controller inputs; unavailable\n", index, route->gsi);
    return;
  }
  input->reg = IO_APIC_REDIRECTION_BASE + pin * IO_APIC_REDIRECTION_REGISTERS;
  input->entry = vector |
    (route->active_low ? IO_APIC_ACTIVE_LOW : 0) |
    (route->level_triggered ? IO_APIC_LEVEL_TRIGGERED : 0);
  input->vector = vector;
  for (unsigned i = 0; i < index; ++i) {
    const struct input_route *other = &inputs[i];
    if (!other->initialized || !same_pin(input, other)) {
      continue;
    }
    if (route->active_low != other->firmware.active_low ||
        route->level_triggered != other->firmware.level_triggered) {
      kerror("I/O APIC: GSI %u has conflicting electrical descriptions; input %u unavailable\n",
             route->gsi, index);
      return;
    }
    input->vector = other->vector;
    input->entry = other->entry;
  }
  io_apic_write(input->controller, input->reg + 1, destination << IO_APIC_DESTINATION_SHIFT);
  io_apic_write(input->controller, input->reg, input->entry | IO_APIC_MASKED);
  input->initialized = true;
}

bool io_apic_init(void)
{
  uint32_t destination = apic_id();
  if (controller_count && destination > UINT8_MAX) {
    panic("I/O APIC target ID %u exceeds 255; interrupt remapping is required", destination);
  }
  for (size_t i = 0; i < controller_count; ++i) {
    struct io_apic_controller *controller = &controllers[i];
    controller->max_entry = (io_apic_read(controller, IO_APIC_VERSION) >>
        IO_APIC_MAX_ENTRY_SHIFT) & IO_APIC_MAX_ENTRY_MASK;
    if (controller->max_entry >
        (UINT8_MAX - IO_APIC_REDIRECTION_BASE) / IO_APIC_REDIRECTION_REGISTERS) {
      panic("I/O APIC redirection register range unsupported");
    }
    for (unsigned pin = 0; pin <= controller->max_entry; ++pin) {
      io_apic_write(controller, IO_APIC_REDIRECTION_BASE + pin * IO_APIC_REDIRECTION_REGISTERS,
                    IO_APIC_MASKED);
    }
  }
  initialize_input(KEYBOARD_INPUT, APIC_KEYBOARD_VECTOR, destination);
  initialize_input(MOUSE_INPUT, APIC_MOUSE_VECTOR, destination);
  initialize_input(SCI_INPUT, APIC_ACPI_VECTOR, destination);

  if (inputs[KEYBOARD_INPUT].initialized) {
    kinfo("keyboard: IRQ 1 -> GSI %u -> BSP APIC %u\n", inputs[KEYBOARD_INPUT].firmware.gsi,
          destination);
  } else {
    kinfo("keyboard: no ACPI route or 8042 controller; input unavailable\n");
  }
  if (inputs[MOUSE_INPUT].initialized) {
    kinfo("mouse: IRQ 12 -> GSI %u -> BSP APIC %u\n", inputs[MOUSE_INPUT].firmware.gsi, destination);
  } else {
    kinfo("mouse: no usable ACPI IRQ 12 route; mouse unavailable\n");
  }
  const struct input_route *sci = &inputs[SCI_INPUT];
  if (sci->initialized) {
    kinfo("ACPI: SCI IRQ %u -> GSI %u, I/O APIC input %u -> BSP APIC %u, %s %s\n",
          sci_irq, sci->firmware.gsi, sci->firmware.gsi - sci->firmware.gsi_base, destination,
          sci->firmware.level_triggered ? "level" : "edge",
          sci->firmware.active_low ? "active-low" : "active-high");
  } else {
    kerror("ACPI: no usable SCI route; ACPI events unavailable\n");
  }
  return inputs[KEYBOARD_INPUT].initialized;
}

void io_apic_keyboard_enable(void)
{
  struct input_route *input = &inputs[KEYBOARD_INPUT];
  KASSERT(input->initialized);
  input->enabled = true;
  update_pin(input);
}

bool io_apic_mouse_available(void)
{
  return inputs[MOUSE_INPUT].initialized;
}

void io_apic_mouse_enable(void)
{
  struct input_route *input = &inputs[MOUSE_INPUT];
  KASSERT(input->initialized);
  input->enabled = true;
  update_pin(input);
}

bool io_apic_sci_available(unsigned *irq)
{
  *irq = inputs[SCI_INPUT].initialized ? sci_irq : 0;
  return inputs[SCI_INPUT].initialized;
}

void io_apic_sci_mask(void)
{
  const struct input_route *input = &inputs[SCI_INPUT];
  KASSERT(input->initialized);
  sci_masked = true;
  update_pin(input);
}

void io_apic_sci_unmask(void)
{
  struct input_route *input = &inputs[SCI_INPUT];
  KASSERT(input->initialized);
  sci_masked = false;
  input->enabled = true;
  update_pin(input);
}

bool io_apic_sci_requested(void)
{
  const struct input_route *input = &inputs[SCI_INPUT];
  if (!input->initialized || !input->enabled) {
    return false;
  }
  uint32_t entry = io_apic_read(input->controller, input->reg);
  return (entry & IO_APIC_DELIVERY_PENDING) || apic_vector_requested(input->vector);
}

void io_apic_sci_disable(void)
{
  struct input_route *input = &inputs[SCI_INPUT];
  KASSERT(input->initialized);
  sci_masked = false;
  input->enabled = false;
  update_pin(input);
}

void io_apic_interrupt(uint8_t vector)
{
  static uint64_t shared_ps2_generation;
  bool ps2 = (inputs[KEYBOARD_INPUT].enabled && inputs[KEYBOARD_INPUT].vector == vector) ||
    (inputs[MOUSE_INPUT].enabled && inputs[MOUSE_INPUT].vector == vector);
  if (ps2) {
    ps2_interrupt();
  }
  if (inputs[SCI_INPUT].enabled && inputs[SCI_INPUT].vector == vector) {
    bool claimed = false;
    if (ps2) {
      /* A polling pump may already have drained this pending PS/2 delivery. */
      uint64_t generation = ps2_input_generation();
      claimed = generation != shared_ps2_generation;
      shared_ps2_generation = generation;
    }
    acpi_interrupt(claimed);
  }
}

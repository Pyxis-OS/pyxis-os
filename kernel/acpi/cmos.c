#include <arch/cmos.h>
#include <kernel/log.h>
#include <uacpi/acpi.h>
#include <uacpi/namespace.h>
#include <uacpi/opregion.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>
#include <uacpi/utilities.h>
#include "host.h"

#define CMOS_REGISTER_COUNT 128u

static uacpi_status cmos_region(uacpi_region_op op, uacpi_handle op_data)
{
  acpi_require_worker();
  switch (op) {
  case UACPI_REGION_OP_ATTACH: {
    uacpi_region_attach_data *data = op_data;
    uint64_t base = data->generic_info.base;
    uint64_t length = data->generic_info.length;
    if (!length || base >= CMOS_REGISTER_COUNT || length > CMOS_REGISTER_COUNT - base) {
      return UACPI_STATUS_INVALID_ARGUMENT;
    }
    data->out_region_context = UACPI_NULL;
    return UACPI_STATUS_OK;
  }
  case UACPI_REGION_OP_DETACH:
    return UACPI_STATUS_OK;
  case UACPI_REGION_OP_READ:
  case UACPI_REGION_OP_WRITE: {
    uacpi_region_rw_data *data = op_data;
    /* uACPI has already added the region base to this absolute byte offset. */
    return arch_cmos_transfer(op == UACPI_REGION_OP_WRITE, data->offset,
        data->byte_width, &data->value) ? UACPI_STATUS_OK : UACPI_STATUS_INVALID_ARGUMENT;
  }
  default:
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
}

void acpi_cmos_start(void)
{
  acpi_require_worker();
  struct acpi_fadt *fadt;
  if (uacpi_table_fadt(&fadt) != UACPI_STATUS_OK ||
      (fadt->iapc_boot_arch & ACPI_IA_PC_NO_CMOS_RTC)) {
    return;
  }
  uacpi_status status = uacpi_install_address_space_handler(uacpi_namespace_root(),
      UACPI_ADDRESS_SPACE_SYSTEM_CMOS, cmos_region, UACPI_NULL);
  if (status != UACPI_STATUS_OK) {
    kerror("ACPI: SystemCMOS handler unavailable: %s\n", uacpi_status_to_string(status));
  }
}

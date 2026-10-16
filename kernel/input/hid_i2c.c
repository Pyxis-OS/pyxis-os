/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_i2c.h>
#include <kernel/log.h>
#include <kernel/task.h>

#define HID_I2C_DESCRIPTOR_BYTES 30
#define HID_I2C_VERSION_1_00 0x0100
#define HID_I2C_READ_TIMEOUT_MS 100

static uint16_t read16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

bool hid_i2c_descriptor_read(struct dw_i2c_master *master,
    const struct acpi_hid_i2c_endpoint *endpoint, struct hid_i2c_descriptor *descriptor)
{
  uint8_t address[] = {endpoint->descriptor_register, endpoint->descriptor_register >> 8};
  uint8_t bytes[HID_I2C_DESCRIPTOR_BYTES];
  enum dw_i2c_result result = dw_i2c_transfer(master, endpoint->address,
      endpoint->addressing_mode != 0, address, sizeof(address), bytes, sizeof(bytes),
      task_deadline_after_ms(HID_I2C_READ_TIMEOUT_MS));
  if (result != DW_I2C_OK) {
    kinfo("HID-I2C: descriptor read refused: %s address=0x%x register=0x%x "
        "abort=0x%x status=0x%x\n", dw_i2c_result_name(result), endpoint->address,
        endpoint->descriptor_register, master->abort_source, master->last_status);
    return false;
  }
  uint16_t length = read16(bytes), version = read16(bytes + 2);
  struct hid_i2c_descriptor captured = {
    .report_bytes = read16(bytes + 4), .report_register = read16(bytes + 6),
    .input_register = read16(bytes + 8), .input_bytes = read16(bytes + 10),
    .output_register = read16(bytes + 12), .output_bytes = read16(bytes + 14),
    .command_register = read16(bytes + 16), .data_register = read16(bytes + 18),
    .vendor = read16(bytes + 20), .product = read16(bytes + 22), .version = read16(bytes + 24),
  };
  if (length != sizeof(bytes) || version != HID_I2C_VERSION_1_00 ||
      !captured.report_bytes || captured.input_bytes < 2 ||
      (captured.output_bytes && captured.output_bytes < 2) ||
      bytes[26] || bytes[27] || bytes[28] || bytes[29]) {
    kinfo("HID-I2C: malformed descriptor length=%u version=0x%x report=%u input=%u output=%u "
        "reserved=0x%02x%02x%02x%02x\n", length, version, captured.report_bytes, captured.input_bytes,
        captured.output_bytes, bytes[26], bytes[27], bytes[28], bytes[29]);
    return false;
  }
  /* HID-over-I2C 1.00 requires these five register IDs to be distinct. Zero
   * is not itself invalid; register indices are chosen by the device vendor. */
  const uint16_t registers[] = {
    captured.report_register, captured.input_register, captured.output_register,
    captured.command_register, captured.data_register,
  };
  for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (registers[i] == registers[j]) {
        kinfo("HID-I2C: descriptor register IDs overlap: report=0x%x input=0x%x output=0x%x "
            "command=0x%x data=0x%x\n", captured.report_register, captured.input_register,
            captured.output_register, captured.command_register, captured.data_register);
        return false;
      }
    }
  }
  *descriptor = captured;
  kinfo("HID-I2C: descriptor v1.00 vendor=%04x product=%04x version=%04x report=%u@0x%x "
      "input=%u@0x%x output=%u@0x%x command=0x%x data=0x%x; no input enabled\n",
      captured.vendor, captured.product, captured.version, captured.report_bytes,
      captured.report_register, captured.input_bytes, captured.input_register,
      captured.output_bytes, captured.output_register, captured.command_register,
      captured.data_register);
  return true;
}

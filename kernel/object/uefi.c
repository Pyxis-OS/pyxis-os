#include <abi/uefi.h>
#include <arch/uefi.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/uefi.h>
#include <kernel/user_memory.h>

#define UEFI_BOOT_ORDER_CAPACITY 256u
#define UEFI_BOOT_OPTION_BYTES 512u
#define UEFI_BOOT_ID_PROBES 128u
#define EFI_VARIABLE_ATTRIBUTES UINT32_C(7) /* NV | boot services | runtime */
#define EFI_LOAD_OPTION_ACTIVE UINT32_C(1)
#define EFI_DEVICE_PATH_MEDIA 4u
#define EFI_DEVICE_PATH_HD 1u
#define EFI_DEVICE_PATH_FILE 4u
#define EFI_DEVICE_PATH_END 0x7fu
#define EFI_DEVICE_PATH_END_ENTIRE 0xffu
#define EFI_DEVICE_PATH_HD_BYTES 42u
#define EFI_DEVICE_PATH_END_BYTES 4u
#define EFI_GPT_PARTITION 2u
#define EFI_GPT_SIGNATURE 2u

static const struct efi_guid global_guid = {
  0x8be4df61u, 0x93cau, 0x11d2u,
  {0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c},
};
static const uint16_t boot_order_name[] = {
  'B', 'o', 'o', 't', 'O', 'r', 'd', 'e', 'r', 0,
};
static const char boot_description[] = "Pyxis OS";
static const char boot_file_path[] = "\\EFI\\pyxis\\BOOTX64.EFI";

static void put16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = value;
  bytes[1] = value >> 8;
}

static void put32(uint8_t *bytes, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (i * 8);
}

static void put64(uint8_t *bytes, uint64_t value)
{
  for (unsigned i = 0; i < 8; ++i) bytes[i] = value >> (i * 8);
}

static uint16_t get16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void boot_name(uint16_t number, uint16_t name[9])
{
  static const char hex[] = "0123456789ABCDEF";
  name[0] = 'B'; name[1] = 'o'; name[2] = 'o'; name[3] = 't';
  for (unsigned i = 0; i < 4; ++i) {
    name[4 + i] = hex[(number >> ((3 - i) * 4)) & 15];
  }
  name[8] = 0;
}

static size_t boot_option(const struct uefi_boot_register_request *request,
    uint8_t bytes[UEFI_BOOT_OPTION_BYTES])
{
  const size_t description_chars = sizeof(boot_description);
  const size_t path_chars = sizeof(boot_file_path);
  const size_t file_bytes = 4 + path_chars * 2;
  const size_t path_bytes = EFI_DEVICE_PATH_HD_BYTES + file_bytes +
      EFI_DEVICE_PATH_END_BYTES;
  const size_t total = 6 + description_chars * 2 + path_bytes;
  _Static_assert(6 + sizeof(boot_description) * 2 + EFI_DEVICE_PATH_HD_BYTES +
      4 + sizeof(boot_file_path) * 2 + EFI_DEVICE_PATH_END_BYTES <=
      UEFI_BOOT_OPTION_BYTES, "Pyxis EFI option fits");
  memset(bytes, 0, total);
  put32(bytes, EFI_LOAD_OPTION_ACTIVE);
  put16(bytes + 4, path_bytes);
  size_t offset = 6;
  for (size_t i = 0; i < description_chars; ++i) {
    put16(bytes + offset, (uint8_t)boot_description[i]);
    offset += 2;
  }

  bytes[offset] = EFI_DEVICE_PATH_MEDIA;
  bytes[offset + 1] = EFI_DEVICE_PATH_HD;
  put16(bytes + offset + 2, EFI_DEVICE_PATH_HD_BYTES);
  put32(bytes + offset + 4, request->partition_number);
  put64(bytes + offset + 8, request->first_lba);
  put64(bytes + offset + 16, request->block_count);
  memcpy(bytes + offset + 24, request->partition_guid, 16);
  bytes[offset + 40] = EFI_GPT_PARTITION;
  bytes[offset + 41] = EFI_GPT_SIGNATURE;
  offset += EFI_DEVICE_PATH_HD_BYTES;

  bytes[offset] = EFI_DEVICE_PATH_MEDIA;
  bytes[offset + 1] = EFI_DEVICE_PATH_FILE;
  put16(bytes + offset + 2, file_bytes);
  offset += 4;
  for (size_t i = 0; i < path_chars; ++i) {
    put16(bytes + offset, (uint8_t)boot_file_path[i]);
    offset += 2;
  }
  bytes[offset] = EFI_DEVICE_PATH_END;
  bytes[offset + 1] = EFI_DEVICE_PATH_END_ENTIRE;
  put16(bytes + offset + 2, EFI_DEVICE_PATH_END_BYTES);
  return total;
}

static bool pyxis_description(const uint8_t *bytes, size_t size)
{
  if (size < 6 + sizeof(boot_description) * 2) return false;
  for (size_t i = 0; i < sizeof(boot_description); ++i) {
    if (get16(bytes + 6 + i * 2) != (uint8_t)boot_description[i]) return false;
  }
  return true;
}

static enum call_status read_option(uint16_t number, uint8_t *bytes,
    size_t capacity, size_t *size, bool *present)
{
  uint16_t name[9];
  boot_name(number, name);
  uint32_t attributes = 0;
  *size = capacity;
  uint64_t status = arch_uefi_get_variable(name, &global_guid,
      &attributes, size, bytes);
  *present = status != EFI_STATUS_NOT_FOUND;
  if (status == EFI_STATUS_NOT_FOUND) return CALL_OK;
  if (status == EFI_STATUS_BUFFER_TOO_SMALL) return CALL_LIMIT;
  if (status) return CALL_IO;
  return attributes == EFI_VARIABLE_ATTRIBUTES ? CALL_OK : CALL_BAD_REQUEST;
}

static enum call_status read_order(uint16_t order[UEFI_BOOT_ORDER_CAPACITY + 1],
    size_t *count)
{
  size_t bytes = UEFI_BOOT_ORDER_CAPACITY * sizeof(*order);
  uint32_t attributes = 0;
  uint64_t status = arch_uefi_get_variable(boot_order_name, &global_guid,
      &attributes, &bytes, order);
  if (status == EFI_STATUS_NOT_FOUND) {
    *count = 0;
    return CALL_OK;
  }
  if (status == EFI_STATUS_BUFFER_TOO_SMALL) return CALL_LIMIT;
  if (status) return CALL_IO;
  if (attributes != EFI_VARIABLE_ATTRIBUTES || (bytes & 1)) return CALL_BAD_REQUEST;
  *count = bytes / sizeof(*order);
  for (size_t i = 0; i < *count; ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (order[i] == order[j]) return CALL_BAD_REQUEST;
    }
  }
  return CALL_OK;
}

static bool variable_equals(const uint16_t *name, const void *expected,
    size_t expected_size)
{
  uint8_t bytes[UEFI_BOOT_OPTION_BYTES + 2];
  if (expected_size > sizeof(bytes)) return false;
  size_t size = sizeof(bytes);
  uint32_t attributes = 0;
  uint64_t status = arch_uefi_get_variable(name, &global_guid,
      &attributes, &size, bytes);
  return !status && attributes == EFI_VARIABLE_ATTRIBUTES &&
      size == expected_size && !memcmp(bytes, expected, size);
}

static enum call_status boot_register(const struct uefi_boot_register_request *request)
{
  uint8_t desired[UEFI_BOOT_OPTION_BYTES], actual[UEFI_BOOT_OPTION_BYTES];
  size_t desired_size = boot_option(request, desired);
  uint16_t order[UEFI_BOOT_ORDER_CAPACITY + 1];
  size_t count;
  enum call_status result = read_order(order, &count);
  if (result != CALL_OK) return result;

  uint16_t selected = 0;
  bool found = false;
  for (size_t i = 0; i < count; ++i) {
    size_t size;
    bool present;
    result = read_option(order[i], actual, sizeof(actual), &size, &present);
    if (result != CALL_OK) return result;
    if (!present || !pyxis_description(actual, size)) continue;
    if (size != desired_size || memcmp(actual, desired, size) || found) {
      return CALL_DENIED;
    }
    selected = order[i];
    found = true;
  }

  if (!found) {
    if (count == UEFI_BOOT_ORDER_CAPACITY) return CALL_LIMIT;
    uint16_t seed = UINT16_C(0xa000);
    bool picked = false;
    for (size_t i = 0; i < sizeof(request->partition_guid); ++i) {
      seed = (seed << 3) ^ (seed >> 13) ^ request->partition_guid[i];
    }
    for (unsigned probe = 0; probe < UEFI_BOOT_ID_PROBES; ++probe) {
      uint16_t candidate = seed + probe;
      size_t size;
      bool present;
      result = read_option(candidate, actual, sizeof(actual), &size, &present);
      if (result != CALL_OK) return result;
      if (present && pyxis_description(actual, size) &&
          (size != desired_size || memcmp(actual, desired, size))) {
        return CALL_DENIED;
      }
      if (!present || (size == desired_size && !memcmp(actual, desired, size))) {
        selected = candidate;
        found = present;
        picked = true;
        break;
      }
    }
    if (!picked) return CALL_LIMIT;
  }

  uint16_t name[9];
  boot_name(selected, name);
  if (!found) {
    uint64_t status = arch_uefi_set_variable(name, &global_guid,
        EFI_VARIABLE_ATTRIBUTES, desired_size, desired);
    if (status || !variable_equals(name, desired, desired_size)) {
      return CALL_OUTCOME_UNKNOWN;
    }
  }

  size_t at = count;
  for (size_t i = 0; i < count; ++i) {
    if (order[i] == selected) { at = i; break; }
  }
  if ((request->flags & UEFI_BOOT_FIRST) && at != 0) {
    for (size_t i = at; i > 0; --i) order[i] = order[i - 1];
    order[0] = selected;
    if (at == count) ++count;
  } else if (at == count) {
    order[count++] = selected;
  } else {
    return CALL_OK;
  }
  uint64_t status = arch_uefi_set_variable(boot_order_name, &global_guid,
      EFI_VARIABLE_ATTRIBUTES, count * sizeof(*order), order);
  if (status || !variable_equals(boot_order_name, order, count * sizeof(*order))) {
    return CALL_OUTCOME_UNKNOWN;
  }
  return CALL_OK;
}

static void destroy_uefi(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *uefi_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) object_init(object, OBJECT_UEFI, destroy_uefi);
  return object;
}

struct syscall_result uefi_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, size_t reply_capacity)
{
  if (operation != UEFI_BOOT_REGISTER) return (struct syscall_result){CALL_BAD_OPERATION, 0};
  if (!(rights & UEFI_RIGHT_BOOT_REGISTER)) return (struct syscall_result){CALL_DENIED, 0};
  if (request_size != sizeof(struct uefi_boot_register_request) || reply_capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct uefi_boot_register_request request;
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  bool guid_zero = true;
  for (size_t i = 0; i < sizeof(request.partition_guid); ++i) {
    guid_zero &= request.partition_guid[i] == 0;
  }
  if (!request.partition_number || !request.first_lba || !request.block_count ||
      request.block_count > UINT64_MAX - request.first_lba || guid_zero ||
      (request.flags & ~UEFI_BOOT_FIRST)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t interrupt_flags;
  enum arch_uefi_gate_result gate = arch_uefi_begin(&interrupt_flags);
  if (gate != ARCH_UEFI_READY) {
    return (struct syscall_result){gate == ARCH_UEFI_BUSY ? CALL_BUSY : CALL_UNAVAILABLE, 0};
  }
  enum call_status result = boot_register(&request);
  arch_uefi_end(interrupt_flags);
  return (struct syscall_result){result, 0};
}

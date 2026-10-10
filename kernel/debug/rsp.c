#include "rsp.h"
#include <arch/debug.h>
#include <kernel/debug.h>
#include <kernel/memory.h>

#define RSP_PAYLOAD_BYTES 1008
#define RSP_PACKET_BYTES 0x3f0
#define RSP_RAM_BYTES (RSP_PACKET_BYTES / 2)
#define RSP_MONITOR_VALUES 8
#define RSP_CORE_REGISTERS 40
#define RSP_INVALID_OPCODE_VECTOR 6
#define RSP_SIGNAL_TRAP 5
#define RSP_SIGNAL_ABORT 6
#define RSP_SIGNAL_SEGMENTATION 11
#define RSP_SIGNAL_ILLEGAL_INSTRUCTION 4

struct rsp_register {
  const char *name, *type;
  unsigned bits;
};

static const struct rsp_register registers[] DEBUG_RODATA = {
  {"rax", "int64", 64}, {"rbx", "int64", 64}, {"rcx", "int64", 64},
  {"rdx", "int64", 64}, {"rsi", "int64", 64}, {"rdi", "int64", 64},
  {"rbp", "data_ptr", 64}, {"rsp", "data_ptr", 64},
  {"r8", "int64", 64}, {"r9", "int64", 64}, {"r10", "int64", 64},
  {"r11", "int64", 64}, {"r12", "int64", 64}, {"r13", "int64", 64},
  {"r14", "int64", 64}, {"r15", "int64", 64}, {"rip", "code_ptr", 64},
  {"eflags", "int32", 32}, {"cs", "int32", 32}, {"ss", "int32", 32},
  {"ds", "int32", 32}, {"es", "int32", 32}, {"fs", "int32", 32},
  {"gs", "int32", 32},
  {"st0", "i387_ext", 80}, {"st1", "i387_ext", 80},
  {"st2", "i387_ext", 80}, {"st3", "i387_ext", 80},
  {"st4", "i387_ext", 80}, {"st5", "i387_ext", 80},
  {"st6", "i387_ext", 80}, {"st7", "i387_ext", 80},
  {"fctrl", "int32", 32}, {"fstat", "int32", 32}, {"ftag", "int32", 32},
  {"fiseg", "int32", 32}, {"fioff", "int32", 32}, {"foseg", "int32", 32},
  {"fooff", "int32", 32}, {"fop", "int32", 32},
  {"fs_base", "int64", 64}, {"gs_base", "int64", 64},
  {"cr3", "int64", 64}, {"kernel_gs_base", "int64", 64},
};
#define RSP_REGISTER_COUNT (sizeof(registers) / sizeof(*registers))

struct rsp_buffer { uint8_t *data; size_t length, capacity; };
static size_t selected_cpu DEBUG_DATA;
static uint64_t cached_command DEBUG_DATA;
static bool cached_valid DEBUG_DATA;
static size_t cached_request_length DEBUG_DATA;
static size_t cached_reply_length DEBUG_DATA;
static uint8_t cached_request[DEBUG_RSP_BYTES] DEBUG_DATA;
static uint8_t cached_reply[DEBUG_RSP_BYTES] DEBUG_DATA;
static uint8_t payload[DEBUG_RSP_BYTES] DEBUG_DATA;
static uint8_t response[RSP_PAYLOAD_BYTES] DEBUG_DATA;
static uint8_t memory[RSP_RAM_BYTES] DEBUG_DATA;
static uint8_t target_xml[RSP_REGISTER_COUNT * 96 + 256] DEBUG_DATA;
static size_t target_xml_length DEBUG_DATA;

static DEBUG_CODE bool append(struct rsp_buffer *out, const void *data, size_t bytes)
{
  if (bytes > out->capacity - out->length) {
    return false;
  }
  memcpy(out->data + out->length, data, bytes);
  out->length += bytes;
  return true;
}

static DEBUG_CODE bool character(struct rsp_buffer *out, uint8_t value)
{
  return append(out, &value, 1);
}

static DEBUG_CODE bool literal(struct rsp_buffer *out, const char *text)
{
  size_t bytes = 0;
  while (text[bytes]) {
    ++bytes;
  }
  return append(out, text, bytes);
}

static DEBUG_CODE uint8_t hex_digit(unsigned value)
{
  return value < 10 ? '0' + value : 'a' + value - 10;
}

static DEBUG_CODE int hex_value(uint8_t value)
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
}

static DEBUG_CODE bool number(struct rsp_buffer *out, uint64_t value, unsigned base)
{
  uint8_t digits[20];
  size_t count = 0;
  do {
    digits[count++] = hex_digit(value % base);
    value /= base;
  } while (value);
  while (count) {
    if (!character(out, digits[--count])) {
      return false;
    }
  }
  return true;
}

static DEBUG_CODE bool hex_bytes(struct rsp_buffer *out, const uint8_t *bytes, size_t count)
{
  if (count > (out->capacity - out->length) / 2) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    character(out, hex_digit(bytes[i] >> 4));
    character(out, hex_digit(bytes[i] & 15));
  }
  return true;
}

static DEBUG_CODE bool equals(const uint8_t *data, size_t bytes, const char *text)
{
  size_t i = 0;
  while (text[i]) {
    if (i == bytes || data[i] != (uint8_t)text[i]) {
      return false;
    }
    ++i;
  }
  return i == bytes;
}

static DEBUG_CODE bool prefix(const uint8_t *data, size_t bytes, const char *text)
{
  size_t i = 0;
  while (text[i]) {
    if (i == bytes || data[i] != (uint8_t)text[i]) {
      return false;
    }
    ++i;
  }
  return true;
}

static DEBUG_CODE bool parse_number(const uint8_t *data, size_t bytes, unsigned base,
                                    uint64_t *value)
{
  if (base == 16 && bytes >= 2 && data[0] == '0' && data[1] == 'x') {
    data += 2;
    bytes -= 2;
  }
  if (!bytes) {
    return false;
  }
  uint64_t result = 0;
  for (size_t i = 0; i < bytes; ++i) {
    int digit = hex_value(data[i]);
    if (digit < 0 || (unsigned)digit >= base || result > (UINT64_MAX - digit) / base) {
      return false;
    }
    result = result * base + digit;
  }
  *value = result;
  return true;
}

static DEBUG_CODE bool pair(const uint8_t *data, size_t bytes, uint8_t separator,
                            uint64_t *first, uint64_t *second)
{
  size_t position = 0;
  while (position < bytes && data[position] != separator) {
    ++position;
  }
  return position < bytes && parse_number(data, position, 16, first) &&
    parse_number(data + position + 1, bytes - position - 1, 16, second);
}

static DEBUG_CODE bool packet(const uint8_t *body, size_t bytes, bool ack,
                              uint8_t *out, size_t capacity, size_t *length)
{
  size_t encoded = 0;
  for (size_t i = 0; i < bytes; ++i) {
    encoded += body[i] == '$' || body[i] == '#' || body[i] == '}' || body[i] == '*' ? 2 : 1;
  }
  if (encoded + 4 + ack > capacity) {
    return false;
  }
  size_t position = 0;
  if (ack) {
    out[position++] = '+';
  }
  out[position++] = '$';
  uint8_t checksum = 0;
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t value = body[i];
    if (value == '$' || value == '#' || value == '}' || value == '*') {
      out[position++] = '}';
      checksum += '}';
      value ^= 0x20;
    }
    out[position++] = value;
    checksum += value;
  }
  out[position++] = '#';
  out[position++] = hex_digit(checksum >> 4);
  out[position++] = hex_digit(checksum & 15);
  *length = position;
  return true;
}

static DEBUG_CODE bool stop_reason(struct rsp_buffer *out)
{
  unsigned signal = arch_debug_stop.reason == DEBUG_STOP_PANIC ? RSP_SIGNAL_ABORT : RSP_SIGNAL_TRAP;
  if (arch_debug_stop.reason == DEBUG_STOP_FAULT) {
    uint64_t vector = arch_debug_stop.cpus[arch_debug_stop.origin_cpu].frame.vector;
    signal = vector == EXCEPTION_PAGE_FAULT ? RSP_SIGNAL_SEGMENTATION :
      vector == RSP_INVALID_OPCODE_VECTOR ? RSP_SIGNAL_ILLEGAL_INSTRUCTION : RSP_SIGNAL_ABORT;
  }
  return character(out, 'T') && character(out, hex_digit(signal >> 4)) &&
    character(out, hex_digit(signal & 15)) && literal(out, "thread:") &&
    number(out, arch_debug_stop.origin_cpu + 1, 16) && character(out, ';');
}

DEBUG_CODE bool debug_rsp_stop_reply(uint8_t *out, size_t capacity, size_t *length)
{
  struct rsp_buffer body = {response, 0, sizeof(response)};
  *length = 0;
  return stop_reason(&body) && packet(body.data, body.length, false, out, capacity, length);
}

static DEBUG_CODE void prepare_xml(void)
{
  struct rsp_buffer xml = {target_xml, 0, sizeof(target_xml)};
  bool ok = literal(&xml, "<?xml version=\"1.0\"?><!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
      "<target><architecture>i386:x86-64</architecture><feature name=\"org.gnu.gdb.i386.core\">");
  for (size_t i = 0; ok && i < RSP_REGISTER_COUNT; ++i) {
    if (i == RSP_CORE_REGISTERS) {
      ok = literal(&xml, "</feature><feature name=\"org.gnu.gdb.i386.segments\">");
    } else if (i == RSP_CORE_REGISTERS + 2) {
      ok = literal(&xml, "</feature><feature name=\"org.pyxis.kernel\">");
    }
    ok = ok && literal(&xml, "<reg name=\"") && literal(&xml, registers[i].name) &&
      literal(&xml, "\" bitsize=\"") && number(&xml, registers[i].bits, 10) &&
      literal(&xml, "\" regnum=\"") && number(&xml, i, 10) &&
      literal(&xml, "\" type=\"") && literal(&xml, registers[i].type) && literal(&xml, "\"/>");
  }
  ok = ok && literal(&xml, "</feature></target>");
  target_xml_length = ok ? xml.length : 0;
}

DEBUG_CODE void debug_rsp_reset(void)
{
  cached_valid = false;
  selected_cpu = arch_debug_stop.origin_cpu;
  prepare_xml();
}

static DEBUG_CODE uint64_t register_value(const struct debug_registers *state, size_t index)
{
  const struct exception_frame *f = &state->frame;
  switch (index) {
  case 0: return f->rax;
  case 1: return f->rbx;
  case 2: return f->rcx;
  case 3: return f->rdx;
  case 4: return f->rsi;
  case 5: return f->rdi;
  case 6: return f->rbp;
  case 7: return f->rsp;
  case 8: return f->r8;
  case 9: return f->r9;
  case 10: return f->r10;
  case 11: return f->r11;
  case 12: return f->r12;
  case 13: return f->r13;
  case 14: return f->r14;
  case 15: return f->r15;
  case 16: return f->rip;
  case 17: return f->rflags;
  case 18: return f->cs;
  case 19: return f->ss;
  case 20: return state->ds;
  case 21: return state->es;
  case 22: return state->fs;
  case 23: return state->gs;
  case 40: return state->fs_base;
  case 41: return state->gs_base;
  case 42: return state->cr3;
  case 43: return state->kernel_gs_base;
  default: return 0;
  }
}

static DEBUG_CODE bool export_register(struct rsp_buffer *out,
    const struct debug_registers *state, size_t index)
{
  size_t bytes = registers[index].bits / 8;
  if (bytes > (out->capacity - out->length) / 2) {
    return false;
  }
  if (index >= 24 && index < RSP_CORE_REGISTERS) {
    for (size_t i = 0; i < bytes * 2; ++i) {
      character(out, 'x');
    }
    return true;
  }
  uint64_t value = register_value(state, index);
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t byte = value >> (i * 8);
    character(out, hex_digit(byte >> 4));
    character(out, hex_digit(byte & 15));
  }
  return true;
}

struct monitor_token { const uint8_t *data; size_t bytes; };

static DEBUG_CODE bool token_number(const struct monitor_token *token, unsigned base,
                                    uint64_t *value)
{
  return parse_number(token->data, token->bytes, base, value);
}

static DEBUG_CODE bool pci_address_token(const struct monitor_token *token,
    unsigned *segment, struct pci_address *address)
{
  const uint8_t separators[] = {':', ':', '.'};
  uint64_t values[4];
  size_t position = 0;
  for (unsigned i = 0; i < 4; ++i) {
    size_t start = position;
    while (position < token->bytes && (i == 3 || token->data[position] != separators[i])) {
      ++position;
    }
    if ((i != 3 && position == token->bytes) ||
        !parse_number(token->data + start, position - start, 16, &values[i])) {
      return false;
    }
    ++position;
  }
  if (values[0] > UINT32_MAX || values[1] >= PCI_BUS_COUNT ||
      values[2] >= PCI_DEVICE_COUNT || values[3] >= PCI_FUNCTION_COUNT) {
    return false;
  }
  *segment = values[0];
  *address = (struct pci_address){values[1], values[2], values[3]};
  return true;
}

static DEBUG_CODE bool monitor(struct rsp_buffer *out, const uint8_t *data, size_t bytes)
{
  uint8_t text[160];
  struct monitor_token tokens[5];
  if (!bytes || bytes % 2 || bytes / 2 > sizeof(text)) {
    return literal(out, "E01");
  }
  size_t length = bytes / 2;
  for (size_t i = 0; i < length; ++i) {
    int high = hex_value(data[i * 2]), low = hex_value(data[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return literal(out, "E01");
    }
    text[i] = (high << 4) | low;
  }
  size_t position = 0, count = 0;
  while (position < length) {
    if (text[position] == ' ') {
      ++position;
      continue;
    }
    if (count == sizeof(tokens) / sizeof(*tokens)) {
      return literal(out, "E01");
    }
    size_t start = position;
    while (position < length && text[position] != ' ') {
      ++position;
    }
    tokens[count++] = (struct monitor_token){text + start, position - start};
  }
  if (count < 4 || !equals(tokens[1].data, tokens[1].bytes, "read")) {
    return literal(out, "E01");
  }
  bool pci = equals(tokens[0].data, tokens[0].bytes, "pci");
  bool phys = equals(tokens[0].data, tokens[0].bytes, "phys");
  bool mmio = equals(tokens[0].data, tokens[0].bytes, "mmio");
  uint64_t width, values = 1, address = 0, offset = 0;
  unsigned segment = 0;
  struct pci_address function = {0};
  if ((!pci && !phys && !mmio) || (pci && count != 5) ||
      !token_number(&tokens[pci ? 4 : 3], 10, &width) ||
      (width != 8 && width != 16 && width != 32 && width != 64) ||
      (pci ? !pci_address_token(&tokens[2], &segment, &function) ||
             !token_number(&tokens[3], 16, &offset) || offset > UINT32_MAX :
             !token_number(&tokens[2], 16, &address) ||
             (count == 5 && !token_number(&tokens[4], 10, &values))) ||
      !values || values > RSP_MONITOR_VALUES) {
    return literal(out, "E01");
  }
  /* Reserve the complete hex-ASCII result before any side-effecting bus read. */
  size_t result_bytes = values * (width / 4 + 3) * 2;
  if (result_bytes > out->capacity - out->length) {
    return false;
  }
  enum debug_inspect_status status;
  if (pci) {
    status = arch_debug_read_pci(segment, function, offset, width, memory, sizeof(memory));
  } else if (phys) {
    status = arch_debug_read_phys(address, width, values, memory, sizeof(memory));
  } else {
    status = arch_debug_read_mmio(address, width, values, memory, sizeof(memory));
  }
  if (status != DEBUG_INSPECT_OK) {
    return literal(out, "E14");
  }
  size_t unit = width / 8;
  for (size_t i = 0; i < values; ++i) {
    uint8_t line[19];
    size_t size = 0;
    line[size++] = '0';
    line[size++] = 'x';
    for (size_t j = unit; j; --j) {
      uint8_t byte = memory[i * unit + j - 1];
      line[size++] = hex_digit(byte >> 4);
      line[size++] = hex_digit(byte & 15);
    }
    line[size++] = '\n';
    hex_bytes(out, line, size);
  }
  return true;
}

static DEBUG_CODE bool dispatch(struct rsp_buffer *out, const uint8_t *data, size_t bytes,
                                bool *resume)
{
  if (equals(data, bytes, "qSupported") || prefix(data, bytes, "qSupported:")) {
    return literal(out, "PacketSize=") && number(out, RSP_PACKET_BYTES, 16) &&
      literal(out, ";qXfer:features:read+");
  }
  if (prefix(data, bytes, "qXfer:features:read:target.xml:")) {
    uint64_t offset, count;
    if (!out->capacity) {
      return false;
    }
    size_t start = sizeof("qXfer:features:read:target.xml:") - 1;
    if (!pair(data + start, bytes - start, ',', &offset, &count) || !count) {
      return literal(out, "E01");
    }
    if (!target_xml_length) {
      prepare_xml();
    }
    if (offset >= target_xml_length) {
      return literal(out, "l");
    }
    size_t available = target_xml_length - offset;
    if (count > out->capacity - 1) {
      count = out->capacity - 1;
    }
    if (count > available) {
      count = available;
    }
    return character(out, count == available ? 'l' : 'm') && append(out, target_xml + offset, count);
  }
  if (equals(data, bytes, "?")) {
    return stop_reason(out);
  }
  if (equals(data, bytes, "g") || (bytes && data[0] == 'p')) {
    struct debug_registers state;
    uint64_t index = 0;
    if (arch_debug_read_registers(selected_cpu, &state) != DEBUG_INSPECT_OK ||
        (data[0] == 'p' && (!parse_number(data + 1, bytes - 1, 16, &index) ||
                          index >= RSP_REGISTER_COUNT))) {
      return literal(out, "E01");
    }
    if (data[0] == 'p') {
      return export_register(out, &state, index);
    }
    for (size_t i = 0; i < RSP_REGISTER_COUNT; ++i) {
      if (!export_register(out, &state, i)) {
        return false;
      }
    }
    return true;
  }
  if (equals(data, bytes, "qfThreadInfo")) {
    if (!character(out, 'm')) {
      return false;
    }
    for (size_t i = 0; i < arch_debug_stop.cpu_count; ++i) {
      if ((i && !character(out, ',')) || !number(out, i + 1, 16)) {
        return false;
      }
    }
    return true;
  }
  if (equals(data, bytes, "qsThreadInfo")) {
    return literal(out, "l");
  }
  if (equals(data, bytes, "qC")) {
    return literal(out, "QC") && number(out, selected_cpu + 1, 16);
  }
  if (prefix(data, bytes, "qThreadExtraInfo,")) {
    uint64_t thread;
    size_t start = sizeof("qThreadExtraInfo,") - 1;
    if (!parse_number(data + start, bytes - start, 16, &thread) ||
        !thread || thread > arch_debug_stop.cpu_count) {
      return literal(out, "E01");
    }
    uint8_t text[80];
    struct rsp_buffer label = {text, 0, sizeof(text)};
    return literal(&label, "CPU ") && number(&label, thread - 1, 10) &&
      literal(&label, " APIC ") && number(&label, arch_debug_stop.cpus[thread - 1].lapic_id, 10) &&
      literal(&label, arch_debug_stop.terminal ? " terminal" : " checkpoint") &&
      hex_bytes(out, label.data, label.length);
  }
  if (bytes > 1 && (data[0] == 'T' || (data[0] == 'H' && (data[1] == 'g' || data[1] == 'c')))) {
    size_t start = data[0] == 'T' ? 1 : 2;
    if (data[0] == 'H' && equals(data + start, bytes - start, "-1")) {
      return literal(out, "OK");
    }
    uint64_t thread;
    if (!parse_number(data + start, bytes - start, 16, &thread) ||
        thread > arch_debug_stop.cpu_count || (data[0] == 'T' && !thread)) {
      return literal(out, "E01");
    }
    if (data[0] == 'H' && data[1] == 'g' && thread) {
      selected_cpu = thread - 1;
    }
    return literal(out, "OK");
  }
  if (equals(data, bytes, "qAttached") || prefix(data, bytes, "qAttached:")) {
    return literal(out, "1");
  }
  if (prefix(data, bytes, "qSymbol:")) {
    return literal(out, "OK");
  }
  if (bytes && data[0] == 'm') {
    uint64_t address, length;
    struct debug_registers state;
    if (!pair(data + 1, bytes - 1, ',', &address, &length) || !length ||
        length > RSP_RAM_BYTES || length > (out->capacity - out->length) / 2 ||
        arch_debug_read_registers(selected_cpu, &state) != DEBUG_INSPECT_OK) {
      return literal(out, "E01");
    }
    if (arch_debug_read_ram(state.cr3, address, memory, length) != DEBUG_INSPECT_OK) {
      return literal(out, "E14");
    }
    return hex_bytes(out, memory, length);
  }
  if (prefix(data, bytes, "qRcmd,")) {
    return monitor(out, data + 6, bytes - 6);
  }
  if (equals(data, bytes, "vCont?")) {
    return literal(out, "vCont;c");
  }
  if (equals(data, bytes, "c") || equals(data, bytes, "vCont;c")) {
    if (arch_debug_stop.terminal) {
      return literal(out, "E01");
    }
    *resume = true;
    return true;
  }
  if (bytes && (data[0] == 'G' || data[0] == 'P' || data[0] == 'M' || data[0] == 'X' ||
      data[0] == 'Z' || data[0] == 'z' || data[0] == 's' || data[0] == 'S' ||
      data[0] == 'D' || data[0] == 'k' || data[0] == 'c' || data[0] == 'C' ||
      prefix(data, bytes, "vCont;") || prefix(data, bytes, "vKill"))) {
    return literal(out, "E01");
  }
  return true;
}

DEBUG_CODE bool debug_rsp_valid(const uint8_t *request, size_t length)
{
  if (length == 1) {
    return request[0] == '+' || request[0] == '-' || request[0] == 3;
  }
  if (length < 4 || length > DEBUG_RSP_BYTES || request[0] != '$' ||
      request[length - 3] != '#') {
    return false;
  }
  int high = hex_value(request[length - 2]), low = hex_value(request[length - 1]);
  if (high < 0 || low < 0) {
    return false;
  }
  uint8_t checksum = 0;
  for (size_t i = 1; i < length - 3; ++i) {
    checksum += request[i];
  }
  if (checksum != (uint8_t)((high << 4) | low)) {
    return false;
  }
  for (size_t i = 1; i < length - 3; ++i) {
    if (request[i] == '}') {
      if (++i == length - 3) {
        return false;
      }
    } else if (request[i] == '$' || request[i] == '#') {
      return false;
    }
  }
  return true;
}

DEBUG_CODE bool debug_rsp_handle(uint64_t command, const uint8_t *request, size_t length,
    uint8_t *output, size_t capacity, size_t *output_length, bool *continue_requested)
{
  *output_length = 0;
  *continue_requested = false;
  if (!capacity || !debug_rsp_valid(request, length)) {
    return false;
  }
  if (length == 1 && request[0] == 3) {
    return packet((const uint8_t *)"E01", 3, true, output, capacity, output_length);
  }
  if (length == 1 && request[0] == '+') {
    return true;
  }
  if (length == 1 && request[0] == '-') {
    if (cached_valid && command == cached_command && cached_reply_length <= capacity) {
      memcpy(output, cached_reply, cached_reply_length);
      *output_length = cached_reply_length;
      return true;
    }
    return false;
  }
  if (cached_valid && command == cached_command) {
    if (length != cached_request_length || memcmp(request, cached_request, length)) {
      return false;
    }
    if (cached_reply_length > capacity) {
      return false;
    }
    memcpy(output, cached_reply, cached_reply_length);
    *output_length = cached_reply_length;
    return true;
  }
  if (!command || (cached_valid && command < cached_command)) {
    return false;
  }
  size_t bytes = 0;
  for (size_t i = 1; i < length - 3; ++i) {
    uint8_t value = request[i];
    if (value == '}') {
      if (++i == length - 3) {
        return false;
      }
      value = request[i] ^ 0x20;
    } else if (value == '$' || value == '#') {
      return false;
    }
    payload[bytes++] = value;
  }
  if (capacity < 5) {
    return false;
  }
  size_t response_capacity = capacity - 5;
  if (response_capacity > RSP_PACKET_BYTES) {
    response_capacity = RSP_PACKET_BYTES;
  }
  struct rsp_buffer body = {response, 0, response_capacity};
  bool resume = false;
  if (!dispatch(&body, payload, bytes, &resume)) {
    return false;
  }
  if (resume) {
    output[0] = '+';
    *output_length = 1;
  } else if (!packet(body.data, body.length, true, output, capacity, output_length)) {
    return false;
  }
  memcpy(cached_request, request, length);
  cached_request_length = length;
  memcpy(cached_reply, output, *output_length);
  cached_reply_length = *output_length;
  cached_command = command;
  cached_valid = true;
  *continue_requested = resume;
  return true;
}

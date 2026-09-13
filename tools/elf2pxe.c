#include <elf.h>
#include <endian.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pxe/p1f.h>

static void fail(const char *message)
{
  fprintf(stderr, "elf2pxe: %s\n", message);
  exit(EXIT_FAILURE);
}

static unsigned char *read_file(const char *path, size_t *size)
{
  FILE *file = fopen(path, "rb");
  if (!file) {
    perror(path);
    exit(EXIT_FAILURE);
  }

  if (fseek(file, 0, SEEK_END)) {
    fail("cannot seek input");
  }
  long length = ftell(file);
  if (length < 0 || (uintmax_t)length > SIZE_MAX || fseek(file, 0, SEEK_SET)) {
    fail("cannot determine input size");
  }

  *size = (size_t)length;
  unsigned char *bytes = malloc(*size ? *size : 1);
  if (!bytes) {
    fail("out of memory");
  }
  if (fread(bytes, 1, *size, file) != *size || fclose(file)) {
    fail("cannot read input");
  }
  return bytes;
}

static bool within_file(uint64_t offset, uint64_t length, size_t size)
{
  return offset <= size && length <= size - offset;
}

static Elf64_Phdr read_program_header(const unsigned char *bytes, size_t offset)
{
  Elf64_Phdr header;
  memcpy(&header, bytes + offset, sizeof(header));
  header.p_type = le32toh(header.p_type);
  header.p_flags = le32toh(header.p_flags);
  header.p_offset = le64toh(header.p_offset);
  header.p_vaddr = le64toh(header.p_vaddr);
  header.p_filesz = le64toh(header.p_filesz);
  header.p_memsz = le64toh(header.p_memsz);
  header.p_align = le64toh(header.p_align);
  return header;
}

static uint64_t segment_flags(uint32_t flags)
{
  if ((flags & ~(PF_R | PF_W | PF_X)) || !(flags & PF_R) ||
      (flags & (PF_W | PF_X)) == (PF_W | PF_X)) {
    fail("segments must be readable and cannot be both writable and executable");
  }

  uint64_t result = P1F_READ;
  if (flags & PF_W) {
    result |= P1F_WRITE;
  }
  if (flags & PF_X) {
    result |= P1F_EXECUTE;
  }
  return result;
}

static void write_u64(FILE *file, uint64_t value)
{
  uint64_t encoded = htole64(value);
  if (fwrite(&encoded, sizeof(encoded), 1, file) != 1) {
    fail("cannot write output");
  }
}

static void convert_p1f(const unsigned char *bytes, size_t size, const char *output)
{
  Elf64_Ehdr elf;
  if (size < sizeof(elf)) {
    fail("truncated ELF header");
  }
  memcpy(&elf, bytes, sizeof(elf));
  if (memcmp(elf.e_ident, ELFMAG, SELFMAG) ||
      elf.e_ident[EI_CLASS] != ELFCLASS64 || elf.e_ident[EI_DATA] != ELFDATA2LSB ||
      elf.e_ident[EI_VERSION] != EV_CURRENT || le32toh(elf.e_version) != EV_CURRENT ||
      le16toh(elf.e_machine) != EM_X86_64 || le16toh(elf.e_type) != ET_EXEC ||
      le16toh(elf.e_ehsize) != sizeof(elf)) {
    fail("expected a fixed-address, little-endian x86_64 ELF executable");
  }

  uint64_t table = le64toh(elf.e_phoff);
  size_t count = le16toh(elf.e_phnum);
  uint64_t entry = le64toh(elf.e_entry);
  if (!count || count == PN_XNUM || le16toh(elf.e_phentsize) != sizeof(Elf64_Phdr) ||
      !within_file(table, count * sizeof(Elf64_Phdr), size)) {
    fail("invalid or unsupported ELF program header table");
  }

  Elf64_Phdr *segments = calloc(count, sizeof(*segments));
  if (!segments) {
    fail("out of memory");
  }
  size_t segment_count = 0;
  uint64_t previous_end = P1F_PAGE_SIZE;
  bool executable_entry = false;

  for (size_t i = 0; i < count; ++i) {
    Elf64_Phdr segment = read_program_header(bytes, table + i * sizeof(segment));
    if (segment.p_type == PT_INTERP || segment.p_type == PT_DYNAMIC ||
        segment.p_type == PT_TLS) {
      fail("interpreters, dynamic linking and TLS are unsupported");
    }
    if (segment.p_type != PT_LOAD) {
      continue;
    }
    if (segment.p_filesz > segment.p_memsz ||
        !within_file(segment.p_offset, segment.p_filesz, size)) {
      fail("invalid ELF segment file extent");
    }
    if (!segment.p_memsz) {
      continue;
    }

    if ((segment.p_vaddr & (P1F_PAGE_SIZE - 1)) ||
        segment.p_vaddr < previous_end || segment.p_vaddr >= P1F_USER_LIMIT ||
        segment.p_memsz > P1F_USER_LIMIT - segment.p_vaddr ||
        (segment.p_offset & (P1F_PAGE_SIZE - 1)) ||
        (segment.p_align > 1 &&
         ((segment.p_align & (segment.p_align - 1)) ||
          (segment.p_vaddr % segment.p_align != segment.p_offset % segment.p_align)))) {
      fail("segments must be page-aligned, ordered, nonoverlapping user ranges");
    }

    uint64_t flags = segment_flags(segment.p_flags);
    uint64_t end = segment.p_vaddr + segment.p_memsz;
    previous_end = (end + P1F_PAGE_SIZE - 1) & ~(P1F_PAGE_SIZE - 1);
    if ((flags & P1F_EXECUTE) && entry >= segment.p_vaddr && entry < end) {
      executable_entry = true;
    }
    segments[segment_count++] = segment;
  }

  if (!segment_count || !executable_entry) {
    fail("entry address is not inside an executable segment");
  }

  FILE *file = fopen(output, "wb");
  if (!file) {
    perror(output);
    exit(EXIT_FAILURE);
  }
  write_u64(file, P1F_MAGIC);
  write_u64(file, entry);
  write_u64(file, segment_count);

  for (size_t i = 0; i < segment_count; ++i) {
    const Elf64_Phdr *segment = &segments[i];
    write_u64(file, segment->p_vaddr);
    write_u64(file, segment->p_filesz);
    write_u64(file, segment->p_memsz);
    write_u64(file, segment_flags(segment->p_flags));
  }
  for (size_t i = 0; i < segment_count; ++i) {
    const Elf64_Phdr *segment = &segments[i];
    if (fwrite(bytes + segment->p_offset, 1, segment->p_filesz, file) != segment->p_filesz) {
      fail("cannot write segment payload");
    }
  }
  if (fclose(file)) {
    fail("cannot finish output");
  }
  free(segments);
}

int main(int argc, char **argv)
{
  const char *format = NULL;
  const char *input = NULL;
  const char *output = NULL;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--format") && i + 1 < argc && !format) {
      format = argv[++i];
    } else if (!strcmp(argv[i], "-o") && i + 1 < argc && !output) {
      output = argv[++i];
    } else if (argv[i][0] != '-' && !input) {
      input = argv[i];
    } else {
      fail("usage: elf2pxe --format p1f -o OUTPUT INPUT");
    }
  }
  if (!format || !input || !output) {
    fail("usage: elf2pxe --format p1f -o OUTPUT INPUT");
  }
  if (strcmp(format, "p1f")) {
    fail("unsupported output format (expected p1f)");
  }
  if (!strcmp(input, output)) {
    fail("input and output paths must differ");
  }

  size_t size;
  unsigned char *bytes = read_file(input, &size);
  convert_p1f(bytes, size, output);
  free(bytes);
  return EXIT_SUCCESS;
}

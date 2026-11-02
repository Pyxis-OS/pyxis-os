#ifndef PXE_P1D_H
#define PXE_P1D_H

#include <pxe/p1f.h>
#include <stdint.h>

#define P1D_MAGIC UINT64_C(0x4d4e594431455850) /* "PXE1DYNM" on disk. */
#define P2D_MAGIC UINT64_C(0x4d4e594432455850) /* "PXE2DYNM" on disk. */

/* Header flags. A library's addresses are image offsets from 0; the loader
 * chooses its base. An executable keeps fixed addresses, as in P1F. */
#define P1D_LIBRARY (UINT64_C(1) << 0)

/* Segment flag beside P1F's R/W/X. A RELRO segment is readable and writable,
 * never executable; it is mapped writable for relocation, then sealed
 * read-only for the life of the process. */
#define P1D_RELRO (UINT64_C(1) << 3)

/* Dynamic images: this header, segment_count struct p1f_segment descriptors,
 * their file bytes in descriptor order and, for P2D, the struct p2_symbols
 * section (p1f.h). Segments follow the P1F rules; page zero is allowed for a
 * library's own offsets. entry is 0 for a library and lies in an executable
 * segment otherwise. The kernel checks this container and that the dynamic
 * section lies inside one read-only segment; it never reads the tables. */
struct p1d_header {
  uint64_t magic;
  uint64_t entry;
  uint64_t segment_count;
  uint64_t flags;
  uint64_t dynamic_address;
  uint64_t dynamic_size;
};

/* The dynamic section, at dynamic_address in a read-only segment. Every
 * address is an image address; an absent table has address 0 and count 0.
 * The loader validates every range against the mapped segments.
 *
 * strings: NUL-terminated UTF-8; name, needed[] and library_dirs[] are
 *   offsets into it. name is the library's file name (libraries only).
 * library_dirs: an executable's declared directories, relative to its own
 *   directory; "." is that directory.
 * symbols: Elf64_Sym records, undefined imports first, with the GNU hash
 *   table over the defined exports.
 * relr: relative relocations in the RELR bitmap encoding.
 * rela: Elf64_Rela records of the x86-64 types R_X86_64_RELATIVE, _64,
 *   _GLOB_DAT, _JUMP_SLOT, _DTPMOD64, _DTPOFF64 and _TPOFF64; nothing else.
 * init_array, fini_array: counts of 8-byte function addresses.
 * tls_*: the static TLS template; tls_memory_size 0 if none.
 * eh_frame_hdr: the unwinder's index, or 0. */
struct p1d_dynamic {
  uint64_t strings;
  uint64_t strings_size;
  uint64_t name;
  uint64_t needed;
  uint64_t needed_count;
  uint64_t library_dirs;
  uint64_t library_dirs_count;
  uint64_t symbols;
  uint64_t symbol_count;
  uint64_t gnu_hash;
  uint64_t relr;
  uint64_t relr_size;
  uint64_t rela;
  uint64_t rela_count;
  uint64_t init_array;
  uint64_t init_count;
  uint64_t fini_array;
  uint64_t fini_count;
  uint64_t tls_template;
  uint64_t tls_file_size;
  uint64_t tls_memory_size;
  uint64_t tls_alignment;
  uint64_t eh_frame_hdr;
};

_Static_assert(sizeof(struct p1d_header) == 48, "P1D header layout");
_Static_assert(sizeof(struct p1d_dynamic) == 184, "P1D dynamic layout");

#endif

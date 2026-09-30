#ifndef ABI_SYSTEM_INFO_H
#define ABI_SYSTEM_INFO_H

#include <abi/message.h>

#define SYSTEM_INFO_RIGHT_READ (UINT64_C(1) << 0)
#define SYSTEM_INFO_IDENTITY UINT64_C(1)
#define SYSTEM_INFO_CPU UINT64_C(2)
#define SYSTEM_INFO_MEMORY UINT64_C(3)

/* Header-only synchronous queries on explicitly delegated system_info authority.
 * All require READ. Strings are NUL-terminated and unused bytes are zero.
 * Empty build_revision or brand means that field is unavailable; other fields
 * remain valid. No kernel pointers, physical map or ambient query is exposed. */
struct system_info_identity {
  char os_name[32];
  char kernel_name[32];
  char architecture[16];
  char build_revision[48]; /* Running kernel source commit, not SDK/userland. */
};

struct system_info_cpu {
  uint64_t online_count; /* Online logical CPUs, not cores or caller affinity. */
  char brand[56]; /* Guest-visible BSP sample; not a heterogeneous inventory. */
};

/* Coherent allocator capacity, excluding permanent reservations, not installed
 * RAM, available memory or RSS. total_bytes = allocated_bytes + free_bytes.
 * Label this observation Memory (allocator). Separate queries are not atomic. */
struct system_info_memory {
  uint64_t total_bytes;
  uint64_t allocated_bytes;
  uint64_t free_bytes;
};

_Static_assert(sizeof(struct system_info_identity) == 128, "system identity layout");
_Static_assert(sizeof(struct system_info_cpu) == 64, "system CPU layout");
_Static_assert(sizeof(struct system_info_memory) == 24, "system memory layout");

#endif

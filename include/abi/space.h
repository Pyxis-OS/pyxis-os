#ifndef ABI_SPACE_H
#define ABI_SPACE_H

#include <abi/message.h>

#define SPACE_RIGHT_SET_TITLE (UINT64_C(1) << 0)
#define SPACE_RIGHT_SET_AFFINITY (UINT64_C(1) << 1)
#define SPACE_RIGHTS (SPACE_RIGHT_SET_TITLE | SPACE_RIGHT_SET_AFFINITY)
#define SPACE_SET_TITLE UINT64_C(1)
#define SPACE_SET_AFFINITY UINT64_C(2)
#define SPACE_TITLE_MAX 63

/* SET_TITLE requires SET_TITLE authority and the object's own space. Titles
 * contain 1..63 printable ASCII bytes (0x20..0x7e), without a terminating NUL
 * in length. No reply. Failure leaves the old title intact. Titles are labels,
 * not identities or authority, and survive the last handle and process exit.
 * The tab clips text to its fixed width; duplicate titles are allowed. */
struct space_title_request {
  struct message_header header;
  uint64_t title;
  uint64_t length;
};

_Static_assert(sizeof(struct space_title_request) == 32, "space title request layout");

/* SET_AFFINITY requires SET_AFFINITY authority and the object's own space. It
 * narrows the CPUs the space's tasks may use, within the boot ceiling, until
 * the space's first launch request; afterwards it fails with ENDPOINT_CLOSED.
 * CPUS addresses ceil(CPU_COUNT / 64) little-endian bit words: bit N of word
 * N / 64 selects boot CPU index N. CPU_COUNT is 1..boot CPU count, and bits at
 * or beyond it must be clear. An empty set or an index beyond the boot is
 * BAD_REQUEST, and a CPU outside the ceiling is DENIED. Any CPU may be named,
 * including the BSP. Any failure changes nothing. On
 * success the call returns on an allowed CPU. Repeated requests replace the
 * set. No reply. */
struct space_affinity_request {
  struct message_header header;
  uint64_t cpus;
  uint64_t cpu_count;
};

_Static_assert(sizeof(struct space_affinity_request) == 32, "space affinity request layout");

#define SPACE_FACTORY_RIGHT_CREATE (UINT64_C(1) << 0)
#define SPACE_FACTORY_RIGHTS SPACE_FACTORY_RIGHT_CREATE
#define SPACE_FACTORY_CREATE UINT64_C(1)
#define SPACE_NAME_MAX 31
#define SPACE_REASON_MAX 96

/* CREATE requires CREATE authority. It appends a space after every existing
 * one; spaces are never destroyed. NAME is 1..31 bytes of a-z, 0-9 and '-',
 * unique among spaces, and identifies the space in logs; it grants nothing.
 * TITLE follows SET_TITLE. Lengths exclude any NUL.
 *
 * Exactly one of REASON or LAUNCH is present. With REASON (1..96 printable
 * ASCII bytes), the space gets no CPUs, its tab shows the reason and the call
 * replies nothing; CPUS must be absent (zero CPU_COUNT).
 *
 * With LAUNCH, the address of a launch_request, CPUS uses SET_AFFINITY's
 * encoding and becomes the space's ceiling: nonempty, every index a boot CPU.
 * The request's streams must be NONE, and its resources must not use the names
 * the kernel adds: input, output, keyboard, pointer, display and space. The
 * kernel gives the child the new space's console as input (READ|INTERRUPT),
 * output (WRITE) and all three streams, its keyboard and pointer (INPUT), its
 * display (DRAW) and the space itself (SET_TITLE|SET_AFFINITY). Everything else
 * comes from the request's grants, as for LAUNCHER_LAUNCH; no execution group.
 * Success replies with one WAIT process-control handle.
 *
 * A malformed request, a duplicate name or a launch request rejected before
 * loading creates nothing. A failure while preparing the child after the
 * space exists leaves it without CPUs and shows the failure on its tab. */
struct space_create_request {
  struct message_header header;
  uint64_t name, name_length;
  uint64_t title, title_length;
  uint64_t cpus, cpu_count;
  uint64_t reason, reason_length;
  uint64_t launch;
};

_Static_assert(sizeof(struct space_create_request) == 88, "space create request layout");

#endif

#ifndef KERNEL_BULK_H
#define KERNEL_BULK_H

#include <abi/file.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>

struct process;
struct bulk_gate;

/* Data capacity alone is charged, independently of completion metadata. */
#define BULK_PROCESS_BYTES_LIMIT (16u * 1024u * 1024u)

struct bulk_buffer {
  struct kernel_object storage;
  struct bulk_gate *gate;
  uint8_t *data;
  size_t capacity;
};

/* Any CPU, IF=0, outside interrupt/fault entry and subsystem locks. The caller
 * keeps process admission/storage live throughout creation. Lazy gate creation
 * is thread-safe. Success returns one owned reference; failure clears *result.
 * Reserve before delivery/I/O: budget refusal is LIMIT, allocation failure is
 * NO_MEMORY. Capacity above FILE_READ_BULK_MAX_BYTES is BAD_REQUEST. Zero is
 * valid and allocates no data. Data is uninitialized: each operation privately
 * tracks its initialized count, serializes access and never exposes the tail. */
enum call_status bulk_buffer_create(struct process *process, size_t capacity,
    struct bulk_buffer **result);

/* Caller already owns live storage. Retain refuses reference-count overflow.
 * Release requires IF=0 and consumes ownership; final destruction is deferred
 * to BSP object retirement. Charge survives until actual data reclamation. */
bool bulk_buffer_retain(struct bulk_buffer *buffer);
void bulk_buffer_release(struct bulk_buffer *buffer);

/* BSP, IF=0, after process activity/admission drains. Detaches the process's
 * gate reference. Remaining buffers own only the gate, never process storage. */
void bulk_process_exit(struct process *process);

#endif

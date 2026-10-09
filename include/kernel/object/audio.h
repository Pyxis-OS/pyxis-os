#ifndef KERNEL_OBJECT_AUDIO_H
#define KERNEL_OBJECT_AUDIO_H

#include <abi/audio.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>
#include <stdbool.h>

struct process;
struct space;

struct audio_volume {
  unsigned percent, pending_percent;
  bool muted, pending_muted, pending;
  int32_t gain, target_gain, ramp_step, ramp_direction;
  unsigned ramp_remaining, ramp_remainder, ramp_error;
};

struct audio_object {
  struct kernel_object object;
  struct space *space; /* Initialized spaces retain the object and outlive it. */
  atomic_bool locked;
  struct process *owner; /* Snapshot only; cleared before process reclamation. */
  uint64_t generation;
  size_t free_frames;
  uint64_t starvations, discontinuities;
  bool failed, starved;
  bool cleanup_pending;
  struct execution_group *cleanup_group;
  /* The sole audio worker owns storage and queue mutation. Exit only invalidates
   * the snapshot and leaves allocation-free cleanup for that worker. */
  int16_t *queue;
  size_t head, count;
  /* Worker-confirmed target is covered by locked; only the worker advances
   * gain. BSP bar input coalesces pending targets without borrowing PCM. */
  struct audio_volume volume;
  struct audio_object *volume_next;
};

struct audio_request {
  struct bsp_request request;
  struct process *process; /* Parked caller, cleared before final completion. */
  struct audio_object *audio; /* Kept alive by the caller's capability. */
  uint64_t operation, generation;
  size_t length;
  int16_t pcm[AUDIO_WRITE_MAX / sizeof(int16_t)];
  union {
    struct audio_acquire_reply acquire;
    struct audio_status_reply status;
    uint64_t written;
  } reply;
  enum call_status result;
};

/* BSP, IF=0. The space owns the initial object reference. */
struct audio_object *audio_create(struct space *space);
void audio_process_exit(struct process *process);
/* Any CPU, IF=0; used by syscall and wait authority checks. */
bool audio_owned(struct audio_object *audio, struct process *process);
/* Owning readiness worker; snapshot access does not borrow queue storage. */
uint64_t audio_ready(struct audio_object *audio, struct process *process);
/* Current user task, IF=0. WRITE copies into the shared typed request before
 * handoff, retaining no caller buffer. */
struct syscall_result audio_call(struct audio_object *audio, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif

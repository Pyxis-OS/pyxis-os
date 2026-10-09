#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/audio.h>
#include <kernel/mm/heap.h>
#include <kernel/object/audio.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>

/* Only the audio worker changes these slots. Process exit invalidates the
 * object snapshot, leaving storage in its slot until worker cleanup. */
static struct audio_object *sessions[AUDIO_SESSION_MAX];

static void lock_audio(struct audio_object *audio)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  while (atomic_exchange_explicit(&audio->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_audio(struct audio_object *audio)
{
  atomic_store_explicit(&audio->locked, false, memory_order_release);
}

static void require_worker(void)
{
  audio_require_worker();
}

static void destroy_audio(struct kernel_object *object)
{
  struct audio_object *audio = (struct audio_object *)object;
  KASSERT(!audio->owner && !audio->queue && !audio->cleanup_pending &&
      !audio->cleanup_group);
  kfree(audio);
}

struct audio_object *audio_create(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  struct audio_object *audio = kmalloc(sizeof(*audio));
  if (!audio) {
    return NULL;
  }
  *audio = (struct audio_object){.space = space};
  atomic_init(&audio->locked, false);
  object_init(&audio->object, OBJECT_AUDIO, destroy_audio);
  return audio;
}

bool audio_owned(struct audio_object *audio, struct process *process)
{
  lock_audio(audio);
  bool owned = audio->owner == process;
  unlock_audio(audio);
  return owned;
}

uint64_t audio_ready(struct audio_object *audio, struct process *process)
{
  uint64_t flags = cpu_save_interrupts();
  lock_audio(audio);
  uint64_t ready = audio->owner != process || audio->failed ? WAIT_ERROR :
      audio->free_frames >= AUDIO_WRITE_MAX / AUDIO_FRAME_BYTES ? WAIT_WRITABLE : 0;
  unlock_audio(audio);
  cpu_restore_interrupts(flags);
  return ready;
}

void audio_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  struct audio_object *audio = process->space->audio;
  lock_audio(audio);
  if (audio->owner != process) {
    unlock_audio(audio);
    return;
  }
  audio->owner = NULL;
  if (audio->generation != UINT64_MAX) {
    ++audio->generation;
  }
  audio->free_frames = 0;
  audio->cleanup_pending = true;
  unlock_audio(audio);
  /* The reaper normally supplies the cleanup context, including after it has
   * detached published execution-group membership from the process. */
  struct execution_group *group = process->execution_group;
  if (group) {
    execution_group_cleanup_begin(group);
  } else {
    group = object_cleanup_defer();
  }
  audio->cleanup_group = group;
  readiness_notify();
  audio_worker_notify();
}

static void release_storage(struct audio_object *audio)
{
  /* BSP/IF=0, no object lock. No other worker borrows queue storage. */
  kfree(audio->queue);
  audio->queue = NULL;
  audio->head = audio->count = 0;
  for (size_t i = 0; i < AUDIO_SESSION_MAX; ++i) {
    if (sessions[i] == audio) {
      sessions[i] = NULL;
      return;
    }
  }
  KASSERT(false);
}

void audio_sessions_cleanup(void)
{
  require_worker();
  uint64_t flags = cpu_save_interrupts();
  for (size_t i = 0; i < AUDIO_SESSION_MAX; ++i) {
    struct audio_object *audio = sessions[i];
    if (audio) {
      lock_audio(audio);
      bool pending = audio->cleanup_pending;
      struct execution_group *group = audio->cleanup_group;
      if (pending) {
        KASSERT(!audio->owner);
        audio->cleanup_pending = false;
        audio->cleanup_group = NULL;
      }
      unlock_audio(audio);
      if (pending) {
        release_storage(audio);
        if (group) {
          execution_group_cleanup_end(group);
        }
      }
    }
  }
  cpu_restore_interrupts(flags);
}

static size_t free_session_slot(void)
{
  size_t slot = 0;
  while (slot < AUDIO_SESSION_MAX && sessions[slot]) {
    ++slot;
  }
  return slot;
}

static enum call_status acquire_session(struct audio_request *request)
{
  struct audio_object *audio = request->audio;
  uint64_t flags;
  bool busy, exhausted;
  for (;;) {
    flags = cpu_save_interrupts();
    lock_audio(audio);
    bool cleanup = audio->cleanup_pending;
    busy = audio->owner != NULL;
    exhausted = audio->generation == UINT64_MAX;
    unlock_audio(audio);
    cpu_restore_interrupts(flags);
    if (!cleanup) {
      break;
    }
    /* A retired owner may have invalidated after the initial cleanup sweep. */
    audio_sessions_cleanup();
  }
  if (busy) {
    return CALL_BUSY;
  }
  if (!audio_engine_available()) {
    return CALL_UNAVAILABLE;
  }
  size_t slot = free_session_slot();
  if (slot == AUDIO_SESSION_MAX) {
    /* A different owner may have exited while this request was preempted. */
    audio_sessions_cleanup();
    slot = free_session_slot();
  }
  if (exhausted || slot == AUDIO_SESSION_MAX) {
    return CALL_LIMIT;
  }
  flags = cpu_save_interrupts();
  int16_t *queue = kmalloc(AUDIO_QUEUE_FRAMES * AUDIO_FRAME_BYTES);
  cpu_restore_interrupts(flags);
  if (!queue) {
    return CALL_NO_MEMORY;
  }
  /* The caller remains parked, and this sole worker is the only acquirer.
   * An unrelated exit during allocation can invalidate only another slot. */
  flags = cpu_save_interrupts();
  if (task_wait_stop_requested(request->request.wait)) {
    kfree(queue);
    cpu_restore_interrupts(flags);
    return CALL_ENDPOINT_CLOSED;
  }
  lock_audio(audio);
  KASSERT(!audio->owner && !audio->queue && !audio->cleanup_pending);
  audio->queue = queue;
  audio->head = audio->count = 0;
  audio->owner = request->process;
  ++audio->generation;
  audio->free_frames = AUDIO_QUEUE_FRAMES;
  audio->starvations = audio->discontinuities = 0;
  audio->failed = audio->starved = false;
  sessions[slot] = audio;
  request->reply.acquire = (struct audio_acquire_reply){
    .generation = audio->generation, .rate = AUDIO_RATE,
    .channels = AUDIO_CHANNELS, .format = AUDIO_FORMAT_S16LE,
    .capacity_frames = AUDIO_QUEUE_FRAMES,
  };
  unlock_audio(audio);
  readiness_notify();
  cpu_restore_interrupts(flags);
  return CALL_OK;
}

void audio_session_request_execute(struct audio_request *request)
{
  require_worker();
  audio_sessions_cleanup();
  struct audio_object *audio = request->audio;
  enum call_status status = CALL_OK;
  uint64_t flags = cpu_save_interrupts();
  bool stopped = task_wait_stop_requested(request->request.wait);
  cpu_restore_interrupts(flags);
  if (request->process->space != audio->space) {
    status = CALL_DENIED;
  } else if (stopped) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (request->operation == AUDIO_ACQUIRE) {
    status = acquire_session(request);
  } else {
    flags = cpu_save_interrupts();
    lock_audio(audio);
    bool release = false;
    if (task_wait_stop_requested(request->request.wait)) {
      status = CALL_ENDPOINT_CLOSED;
    } else if (audio->owner != request->process || audio->generation != request->generation) {
      status = CALL_DENIED;
    } else if (request->operation == AUDIO_STATUS) {
      request->reply.status = (struct audio_status_reply){
        .generation = audio->generation, .capacity_frames = AUDIO_QUEUE_FRAMES,
        .free_frames = audio->free_frames, .starvations = audio->starvations,
        .discontinuities = audio->discontinuities,
        .state = audio->failed ? AUDIO_STATE_FAILED : AUDIO_STATE_READY,
      };
    } else if (request->operation == AUDIO_RELEASE) {
      audio->owner = NULL;
      if (audio->generation != UINT64_MAX) {
        ++audio->generation;
      }
      audio->free_frames = 0;
      release = true;
    } else if (audio->failed) {
      status = CALL_UNAVAILABLE;
    } else if (request->length / AUDIO_FRAME_BYTES > audio->free_frames) {
      status = CALL_WOULD_BLOCK;
    } else {
      KASSERT(request->operation == AUDIO_WRITE);
      size_t frames = request->length / AUDIO_FRAME_BYTES;
      for (size_t i = 0; i < frames; ++i) {
        size_t tail = (audio->head + audio->count + i) % AUDIO_QUEUE_FRAMES;
        audio->queue[tail * AUDIO_CHANNELS] = request->pcm[i * AUDIO_CHANNELS];
        audio->queue[tail * AUDIO_CHANNELS + 1] = request->pcm[i * AUDIO_CHANNELS + 1];
      }
      audio->count += frames;
      audio->free_frames -= frames;
      request->reply.written = request->length;
      if (frames) {
        audio->starved = false;
      }
    }
    unlock_audio(audio);
    if (release) {
      release_storage(audio);
      readiness_notify();
    }
    cpu_restore_interrupts(flags);
  }
  request->result = status;
  request->process = NULL;
  request->audio = NULL;
}

bool audio_sessions_pending(void)
{
  require_worker();
  uint64_t flags = cpu_save_interrupts();
  bool pending = false;
  for (size_t i = 0; i < AUDIO_SESSION_MAX; ++i) {
    struct audio_object *audio = sessions[i];
    if (audio) {
      lock_audio(audio);
      pending |= audio->owner && !audio->failed && audio->count;
      unlock_audio(audio);
    }
  }
  cpu_restore_interrupts(flags);
  return pending;
}

size_t audio_sessions_mix(int16_t *output, size_t frames)
{
  KASSERT(arch_cpu_index() == 0 && frames <= AUDIO_PERIOD_FRAMES);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  int32_t mixed[AUDIO_PERIOD_FRAMES * AUDIO_CHANNELS] = {0};
  size_t consumed = 0;
  bool writable = false;
  for (size_t i = 0; i < AUDIO_SESSION_MAX; ++i) {
    struct audio_object *audio = sessions[i];
    if (!audio) {
      continue;
    }
    lock_audio(audio);
    /* Exit and RELEASE invalidate before any subsequent mix commit. Exit is
     * BSP-only, so it cannot interleave with this bounded IF=0 publication. */
    bool valid = audio->owner && !audio->failed && !audio->cleanup_pending;
    unlock_audio(audio);
    if (!valid) {
      continue;
    }
    size_t count = audio->count < frames ? audio->count : frames;
    for (size_t j = 0; j < count; ++j) {
      size_t source = (audio->head + j) % AUDIO_QUEUE_FRAMES;
      mixed[j * AUDIO_CHANNELS] += audio->queue[source * AUDIO_CHANNELS];
      mixed[j * AUDIO_CHANNELS + 1] += audio->queue[source * AUDIO_CHANNELS + 1];
    }
    audio->head = (audio->head + count) % AUDIO_QUEUE_FRAMES;
    audio->count -= count;
    lock_audio(audio);
    size_t free_before = audio->free_frames;
    audio->free_frames += count;
    writable |= free_before < AUDIO_WRITE_MAX / AUDIO_FRAME_BYTES &&
        audio->free_frames >= AUDIO_WRITE_MAX / AUDIO_FRAME_BYTES;
    if (count < frames && !audio->starved && audio->starvations != UINT64_MAX) {
      ++audio->starvations;
    }
    audio->starved = count < frames;
    unlock_audio(audio);
    if (count > consumed) {
      consumed = count;
    }
  }
  for (size_t i = 0; i < frames * AUDIO_CHANNELS; ++i) {
    int32_t value = mixed[i];
    output[i] = value > INT16_MAX ? INT16_MAX : value < INT16_MIN ? INT16_MIN : value;
  }
  if (writable) {
    readiness_notify();
  }
  return consumed;
}

void audio_sessions_fail(void)
{
  require_worker();
  uint64_t flags = cpu_save_interrupts();
  for (size_t i = 0; i < AUDIO_SESSION_MAX; ++i) {
    struct audio_object *audio = sessions[i];
    if (audio) {
      lock_audio(audio);
      bool first_failure = !audio->failed;
      audio->failed = true;
      audio->head = audio->count = 0;
      audio->free_frames = AUDIO_QUEUE_FRAMES;
      if (first_failure && audio->discontinuities != UINT64_MAX) {
        ++audio->discontinuities;
      }
      unlock_audio(audio);
    }
  }
  readiness_notify();
  cpu_restore_interrupts(flags);
}

struct syscall_result audio_call(struct audio_object *audio, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation < AUDIO_ACQUIRE || operation > AUDIO_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct process *process = process_current();
  if (!(rights & AUDIO_RIGHT_PLAYBACK) || process->space != audio->space) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  size_t payload = operation == AUDIO_WRITE ? 2 * sizeof(uint64_t) : 0;
  size_t reply_size = operation == AUDIO_ACQUIRE ? sizeof(struct audio_acquire_reply) :
      operation == AUDIO_STATUS ? sizeof(struct audio_status_reply) :
      operation == AUDIO_WRITE ? sizeof(uint64_t) : 0;
  if (request_size != payload || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct { uint64_t buffer, length; } write = {0};
  if (payload && !copy_from_user(&write, request_address, payload)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (write.length > AUDIO_WRITE_MAX) {
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  if (write.length % AUDIO_FRAME_BYTES) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if ((operation == AUDIO_WRITE &&
        !user_buffer_check(write.buffer, write.length, USER_BUFFER_READ)) ||
      (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  lock_audio(audio);
  bool owned = audio->owner == process;
  uint64_t generation = audio->generation;
  unlock_audio(audio);
  if (operation != AUDIO_ACQUIRE && !owned) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct audio_request *request =
      (struct audio_request *)bsp_request_prepare(BSP_SERVICE_AUDIO);
  request->process = process;
  request->audio = audio;
  request->operation = operation;
  request->generation = generation;
  request->length = write.length;
  if (write.length) {
    KASSERT(copy_from_user(request->pcm, write.buffer, write.length));
  }
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  if (status == CALL_OK && reply_size) {
    KASSERT(copy_to_user(reply_address, &request->reply, reply_size));
  }
  size_t result = status == CALL_OK ? reply_size : 0;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, result};
}

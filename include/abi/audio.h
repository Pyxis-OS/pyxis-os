#ifndef ABI_AUDIO_H
#define ABI_AUDIO_H

#include <abi/message.h>

#define AUDIO_RIGHT_PLAYBACK (UINT64_C(1) << 0)
#define AUDIO_ACQUIRE UINT64_C(1)
#define AUDIO_WRITE UINT64_C(2)
#define AUDIO_STATUS UINT64_C(3)
#define AUDIO_RELEASE UINT64_C(4)
#define AUDIO_RATE 48000
#define AUDIO_CHANNELS 2
#define AUDIO_FRAME_BYTES 4
#define AUDIO_QUEUE_FRAMES 3840
#define AUDIO_WRITE_MAX 4096
#define AUDIO_SESSION_MAX 8
#define AUDIO_FORMAT_S16LE UINT64_C(1)
#define AUDIO_STATE_READY UINT64_C(0)
#define AUDIO_STATE_FAILED UINT64_C(1)

/* Exclusive process-owned session in this grant's space. Hidden spaces keep
 * playing. Closing/copying handles does not release/transfer ownership; exit
 * does. Conversion and resampling belong to userspace. No audible drain or
 * play cursor is promised, and already mixed frames can outlive release. */
struct audio_acquire_reply {
  uint64_t generation, rate, channels, format, capacity_frames;
};

/* Nonblocking atomic copy: <=4096 bytes, complete four-byte stereo frames.
 * Full queue accepts nothing. Zero length is a validated no-op. */
struct audio_write_request {
  struct message_header header;
  uint64_t buffer, length;
};

struct audio_status_reply {
  uint64_t generation, capacity_frames, free_frames;
  uint64_t starvations, discontinuities, state;
};

/* STATUS/RELEASE/WRITE require the acquiring process. WAIT_WRITABLE observes
 * room for a maximum write, reserves nothing; terminal failure is WAIT_ERROR. */
_Static_assert(sizeof(struct audio_acquire_reply) == 40, "audio acquire layout");
_Static_assert(sizeof(struct audio_write_request) == 32, "audio write layout");
_Static_assert(sizeof(struct audio_status_reply) == 48, "audio status layout");

#endif

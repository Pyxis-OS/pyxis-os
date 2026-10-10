#ifndef KERNEL_DEBUG_RSP_H
#define KERNEL_DEBUG_RSP_H

#include <stddef.h>
#include <stdint.h>

#define DEBUG_RSP_BYTES 1024

/* Parked BSP, COMPLETE only. Requests include framing/checksum or one ACK byte.
 * Command identity remains unchanged for a retry until its response is ACKed.
 * Results include the RSP ACK and framed response. Continue emits only '+'.
 * False means no operation ran and no response could be produced. */
bool debug_rsp_handle(uint64_t command, const uint8_t *request, size_t length,
    uint8_t *output, size_t capacity, size_t *output_length, bool *continue_requested);
bool debug_rsp_valid(const uint8_t *request, size_t length);
void debug_rsp_reset(void);
/* Unsolicited notification, without an RSP ACK or a command/cache mutation. */
bool debug_rsp_stop_reply(uint8_t *output, size_t capacity, size_t *length);

#endif

#ifndef REMOTE_TRANSFER_H
#define REMOTE_TRANSFER_H

#include "buffer.h"
#include "presentation.h"
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>

/* Reserve before consuming a peer byte or host input: includes the complete
 * upload catalog reply and ordinary input remaining in the same read batch. */
#define TRANSFER_REPLY_RESERVE 16384u

struct file_transfer;

struct file_transfer *transfer_create(const char *download_directory,
    const volatile sig_atomic_t *interrupted);
void transfer_destroy(struct file_transfer *transfer);
void transfer_output(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, unsigned char byte, int64_t now);
/* True consumes transfer confirmation/cancellation input; false preserves the
 * client's ordinary input path. Ctrl+] always uses that ordinary close path. */
bool transfer_input(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, unsigned char byte, int64_t now);
void transfer_pump(struct file_transfer *transfer, struct presentation *screen,
    struct byte_buffer *outgoing, int64_t now);
bool transfer_failed(const struct file_transfer *transfer);
int transfer_timeout(const struct file_transfer *transfer, int64_t now,
    bool can_present, bool can_reply);
void transfer_fresh_line(struct file_transfer *transfer, struct presentation *screen);

#endif

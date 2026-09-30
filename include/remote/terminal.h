#ifndef REMOTE_TERMINAL_H
#define REMOTE_TERMINAL_H

#include <stdint.h>

/* TCP frames have two big-endian uint32_t fields (type, payload length).
 * Payloads are independent of the native ABI and never contain C structs. */
#define REMOTE_HEADER_SIZE 8u
#define REMOTE_PAYLOAD_MAX 4096u
#define REMOTE_COLUMNS_MAX 512u
#define REMOTE_ROWS_MAX 256u
#define REMOTE_HELLO_SIZE 12u
#define REMOTE_TAB_WIDTH_SIZE 8u
#define REMOTE_ERROR_SIZE 4u
#define REMOTE_FINAL_SIZE 16u
#define REMOTE_COMMAND_COMPLETE_SIZE 16u

#define REMOTE_HELLO 1u
#define REMOTE_INPUT 2u
#define REMOTE_END_INPUT 3u
#define REMOTE_CLOSE 4u
#define REMOTE_READY 16u
#define REMOTE_OUTPUT 17u
#define REMOTE_FRESH_LINE 18u
#define REMOTE_TAB_WIDTH 19u
#define REMOTE_ERROR 20u
#define REMOTE_FINAL 21u
#define REMOTE_COMMAND_COMPLETE 22u

#define REMOTE_OPTION_NO_SHELL_ECHO (1u << 0)
#define REMOTE_OPTIONS REMOTE_OPTION_NO_SHELL_ECHO

#define REMOTE_COMPLETION_EXITED 1u
#define REMOTE_COMPLETION_FAULTED 2u
#define REMOTE_COMPLETION_TERMINATED 3u
#define REMOTE_COMPLETION_LAUNCH_FAILED 4u
#define REMOTE_COMPLETION_BUILTIN 5u
#define REMOTE_COMPLETION_REJECTED 6u
#define REMOTE_COMPLETION_LAUNCHED 7u

#define REMOTE_CAUSE_SHELL_EXIT 1u
#define REMOTE_CAUSE_CLIENT_CLOSE 2u
#define REMOTE_CAUSE_SERVER_ERROR 3u
#define REMOTE_PROCESS_EXITED 1u
#define REMOTE_PROCESS_FAULTED 2u
#define REMOTE_PROCESS_TERMINATED 3u
#define REMOTE_DRAIN_COMPLETE 1u
#define REMOTE_DRAIN_TIMEOUT 2u
#define REMOTE_ERROR_BAD_FRAME 1u
#define REMOTE_ERROR_LAUNCH 2u
#define REMOTE_ERROR_RESOURCE 3u
#define REMOTE_ERROR_INTERNAL 4u
#define REMOTE_ERROR_HELLO_TIMEOUT 5u

static inline uint32_t remote_decode_u32(const unsigned char *bytes)
{
  return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
         (uint32_t)bytes[2] << 8 | bytes[3];
}

static inline uint64_t remote_decode_u64(const unsigned char *bytes)
{
  return (uint64_t)remote_decode_u32(bytes) << 32 | remote_decode_u32(bytes + 4);
}

static inline void remote_encode_u32(unsigned char *bytes, uint32_t value)
{
  bytes[0] = (unsigned char)(value >> 24);
  bytes[1] = (unsigned char)(value >> 16);
  bytes[2] = (unsigned char)(value >> 8);
  bytes[3] = (unsigned char)value;
}

static inline void remote_encode_u64(unsigned char *bytes, uint64_t value)
{
  remote_encode_u32(bytes, (uint32_t)(value >> 32));
  remote_encode_u32(bytes + 4, (uint32_t)value);
}

/* HELLO: columns, rows, options (u32). Unknown option bits are rejected.
 * NO_SHELL_ECHO suppresses only the root shell's line-editor presentation.
 * INPUT: 1..4096 bytes. END_INPUT/CLOSE: empty.
 * READY/FRESH_LINE: empty. OUTPUT: 1..4096 bytes. TAB_WIDTH: u64, 1..32.
 * ERROR: code (u32). FINAL: cause, process reason, exit status, drain (u32).
 * COMMAND_COMPLETE: sequential command number (u64, starts at 1), kind (u32)
 * and status (u32): signed exit-code bits for EXITED, 0 success or 1 failure
 * for BUILTIN, zero otherwise. Ordered with terminal records, before FINAL.
 * READY occurs once before terminal records and FINAL. ERROR can reject HELLO
 * before READY or precede FINAL. EOF is not a completion acknowledgment. */

#endif

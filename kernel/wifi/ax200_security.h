#ifndef AX200_SECURITY_H
#define AX200_SECURITY_H

#include <abi/syscall.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AX200_CCMP_KEY_BYTES 16u
#define AX200_CCMP_IV_BYTES 8u
#define AX200_CCMP_TIDS 16u
#define AX200_CCMP_PN_MAX UINT64_C(0xffffffffffff)

struct ax200_ccmp_key {
  bool installed;
  uint8_t id, bytes[AX200_CCMP_KEY_BYTES];
  uint64_t receive_pn[AX200_CCMP_TIDS], transmitted;
};
struct ax200_security {
  struct ax200_ccmp_key pairwise, group;
};

/* Sole BSP radio worker, IF=1. The caller closes ordinary admission and drains
 * TX before replacement/removal; no key state is touched by another worker. */
bool ax200_security_supported(void);
enum call_status ax200_security_install(struct ax200_security *security, bool group,
    unsigned key_id, const uint8_t key[AX200_CCMP_KEY_BYTES], uint64_t receive_pn,
    uint64_t deadline_ns);
enum call_status ax200_security_remove(struct ax200_security *security, bool group,
    uint64_t deadline_ns);
/* Call only after confirmed key retirement, or during terminal quarantine when
 * no further firmware operations will use this state. Does not touch DMA. */
void ax200_security_clear(struct ax200_security *security);

enum ax200_security_rx_result {
  AX200_SECURITY_RX_OK,
  AX200_SECURITY_RX_INVALID,
  AX200_SECURITY_RX_NO_KEY,
  AX200_SECURITY_RX_INTEGRITY,
  AX200_SECURITY_RX_REPLAY,
};
struct ax200_security_rx {
  size_t payload_offset, payload_bytes;
  uint64_t pn;
  uint8_t key_id, tid;
  bool group;
};
/* Non-QoS legacy data only. Read-only view into the caller's bounded owned
 * frame, with firmware padding still present. Match AP/address/generation and
 * perform ordinary duplicate validation before accepting the PN. */
enum ax200_security_rx_result ax200_security_receive(const struct ax200_security *security,
    const uint8_t *frame, size_t bytes, uint32_t status, bool padded, uint8_t mac_flags1,
    struct ax200_security_rx *view);
bool ax200_security_accept(struct ax200_security *security, const struct ax200_security_rx *view);
/* Firmware supplies the IV/PN. This wrapper admits only an installed PTK and
 * accounts each accepted encrypted MPDU before the 48-bit PN can wrap. */
bool ax200_security_transmit(struct ax200_security *security, const uint8_t *frame,
    size_t bytes, bool explicit_rate, uint32_t rate);

#endif

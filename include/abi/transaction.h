#ifndef ABI_TRANSACTION_H
#define ABI_TRANSACTION_H

#include <abi/provider.h>

#define TRANSACTION_RIGHT_STAGE (UINT64_C(1) << 0)
#define TRANSACTION_RIGHT_COMMIT (UINT64_C(1) << 1)
#define TRANSACTION_RIGHT_ABORT (UINT64_C(1) << 2)
#define TRANSACTION_RIGHTS (TRANSACTION_RIGHT_STAGE | TRANSACTION_RIGHT_COMMIT | \
  TRANSACTION_RIGHT_ABORT)

#define TRANSACTION_STAGE UINT64_C(1)
#define TRANSACTION_COMMIT UINT64_C(2)
#define TRANSACTION_ABORT UINT64_C(3)

/* Operations have no payload or request grants. STAGE transfers one owned
 * writable FILE with no reply bytes. COMMIT freezes staging and transfers an
 * owned readable typed resource using provider_open_reply, or a failed outcome
 * without a resource. Descriptions and optional copied error bodies follow
 * the typed OPEN contract. ABORT returns no bytes or grants.
 * Authority for each operation is independent. Deadlines are absolute; the
 * original transaction deadline is never extended. COMMIT is attempted once:
 * a lost delivered reply is uncertain, never a reason to replay submission.
 * Later COMMIT calls cannot transfer another reader. */

#endif

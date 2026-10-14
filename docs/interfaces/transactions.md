# Transaction resources

PROTOCOL_TRANSACTION is an exported object with CALL transport. Its
[ABI](../../include/abi/transaction.h) and SDK `transaction.h` define three
independently attenuable rights and operations. No operation accepts payload
bytes or attached grants; authority comes from the held control grant.

| Operation | Reply |
| --- | --- |
| STAGE | One owned writable FILE; zero reply bytes. Multiple calls obtain references to the same staging resource. |
| COMMIT | `provider_open_reply` with an owned readable FILE/STREAM, or a failed resource outcome without a resource. |
| ABORT | No reply bytes or grants; cancellation cannot undo a remote side effect. |

The issuer freezes staging before submission and drains already admitted writes.
Known producer failure prevents submission. COMMIT is attempted once; a lost
delivered reply reports uncertain outcome, never automatic retry. Later calls
cannot transfer another reader. Deadlines are absolute and do not extend the
original transaction deadline. Returned resources follow the caller's control
lineage, including later cut. Immutable error bodies copied by DESCRIBE own
their bytes independently of that lineage and the provider's lifetime.

SDK collection validates reply extents, counts, type, actual masks and description;
it closes malformed/provisional attachments. Descriptions are declared snapshots,
not permission to operate on a resource. [Typed OPEN](file-providers.md) supplies
the same outcome and error-body vocabulary. [Libc](../userland/stdio.md#typed-resources-and-transactions)
dispatches by attested type and applies exactly-once flush/final-close behavior.

Prepared HTTP uses this control alongside private preparation and STATUS/METADATA
facets during the [HTTP transition](http-transactions.md). Generic clients never
interpret an HTTP status or fall back to the legacy URI route.

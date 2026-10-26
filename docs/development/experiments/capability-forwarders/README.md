# Capability forwarders: task 1 qualification

## Inputs and baseline

- Initial eight-mode code/image: `ed1cc1605cc7b6ede2a09935480a645b200d5922`,
  based on main `be055891` (includes #825). Userland #259:
  `fafaddaeac04d472dcdb2865fac7cc0f64803d64`.
- Existing compiler image `pyxis-llvm23.1.3-e0801c2`; no compiler change.
  Ordinary `make -j16 image` passed. Nested KVM Q35, 2 GiB, 1/4 CPUs,
  VirtIO-net/rng, standard VGA, OVMF; remote TCP 2323 through localhost 24084.
  No measurement instrumentation or new ordinary klog lines.
- Unchanged-main `3b714754` build/4-CPU boot passed before implementation.
  One local CALL64 baseline (256 messages, 100 rounds) returned 100 verified
  samples: median 123.927 ms, range 87.598–805.372 ms. A build overlapped part
  of this non-isolated run; it supports no performance attribution.
- The [owner's native baseline](../capability-model-baseline.md) is merged in
  #825, closing task 1's prerequisite. Repeat its matched profile after A and C.
  No native forwarder results or matched after timing are claimed here.

## Manual checks

Run each in its own plain Remote session:

```text
session bin://counter.pxe --forwarders bin counter.pxe textfs.pxe
session bin://counter.pxe --provider-forwarders text bin counter.pxe
session bin://counter.pxe --completion-cut bin counter.pxe
session bin://counter.pxe --authority-close
session bin://counter.pxe --queued-cut
session bin://counter.pxe --cut
session bin://counter.pxe --retire-full
session bin://counter.pxe --exit
```

All eight passed on both 1 and 4 CPUs; every session returned exit status 0
and complete group drainage. They cover native directory/file derivation and
COPY; explicit/last-controller CUT; supervisor-copy takeover, unmanaged slots,
empty slots and terminal slot CUT; queued DENIED/NOT_DELIVERED; delivered
REPLY after CUT; stale cut handles after export reuse; full-queue retirement
and provider exit.

The provider mode also covers exported FILE retargeting, both native/exported
WRONG_TYPE refusals without changing targets, provider-open child/COPY lineage,
powerless controller copies, and recovering a slot after its private current
provider loses its last controller. Old opened children remain denied; explicit
slot CUT denies its new children. Independent provider grants remain usable.
The authority mode observes empty-group cleanup through WAIT-only forwarding
while retaining wrapper control and its bound launcher: neither prolongs the
closed native CONTROL authority.

The completion mode receives UDP DENIED with reason DENIED and flags
NOT_STARTED|CANCEL_REQUESTED (3), then acknowledges it. A started archive FILE
read retains OK, reason 0 and PROGRESS_KNOWN (4) after CUT, with the independently
checked byte intact. At `start_backend`, interactive 4-CPU GDB observed committed
SUBMIT's original context STARTED while its owned backend context was PENDING.
After all cases, GDB on each CPU configuration found `operations == NULL`,
12 live bootstrap provider roots and no temporary derived nodes. No injected
calls or guest-memory edits were used; raw JSONL/GDB captures remain in `build/`.

## HTTP and mux review follow-up

Fixed code/image `f6932b31a662ddcced81d48fe92dd380ddec7348`, based on
main `60ec274b`, with userland `5cd789a737f6da07c0035cfa723abcd7ab188b0d`
(rebased onto `6f19f49`) and main's fs `d84f8084`. Ordinary image build passed.
The endpoint now derives both fresh COMMIT attachments; unrelated pre-existing
attachments on other operations keep their lineage. Observation retention is
independent of ordinary facet closure, not of source CUT.

Use the existing host fixture, then the manual counter mode:

```sh
python3 tools/demo/ci-jobs-server.py --host 127.0.0.1 --port 18080 --speed 4
```

```text
session bin://counter.pxe --http-lineage http http://10.0.2.2:18080/jobs
```

GET and POST passed on both 1 and 4 CPUs (exit 0, complete drainage); the host
fixture recorded one POST per run. The cases release the initial observation before COMMIT to force
adoption of its fresh observation. Each consumes the response through EOF,
checks final STATUS/METADATA, then source CUT denies response READ, STATUS and
METADATA. A fresh GET on the independent original provider still succeeds.
POST also verifies the uploaded byte count and that COMMIT freezes its writer.
One-CPU GDB after cleanup found no operation/derived nodes and the 12 live
bootstrap provider roots.

On the fixed 4-CPU image, manual HMP keyboard input in the packaged Multiplexer
space exercised left/right and top/bottom splits; digit and directional focus;
per-pane echo output; close confirmation/termination of a running Lua child;
and normal shell exit followed by removing all remaining panes. Remote `--mux`
at negotiated 80×24 exercised split/focus/output, Ctrl+] disconnect with a live
Lua child (client_close, process terminated, drain complete), reconnect and
normal exit. Host --columns/--rows were ceilings, not the negotiated geometry.
Interactive GDB after cleanup found local ARMED/PASSTHROUGH counts zero,
owned execution-group counts zero in Multiplexer and Remote, and no outstanding
forwarder operations/derived nodes. Nine bootstrap provider roots remained;
the three Multiplexer providers retired with its session. Captures/screens stay
local in `build/`; no injected guest calls/writes or performance claims.

## Source findings and limits

Source audits corrected post-commit SUBMIT denial, UDP cancellation metadata,
slot identity inherited from its first provider, fresh admission after natural
RETIRE, and narrowed observers prolonging native close-bearing authority.
The runtime cases above qualify the corresponding representative behavior;
the caretaker-after-RETIRE race is source-reviewed, not directly reproduced.
BSP allocation, short guards, source generations and cleanup ownership remain
unchanged; lineage/hop bounds are tunable at 32, not ABI.

A preceding checkpoint also read `text://guide` and inspected a held FILE with
both provider-service and file-export ancestors. Packaged Remote deliberately
denies namespace management; its replacement attempt did not qualify global
namespace replacement. CUT racing delivery during OPEN/PREPARE/HTTP COMMIT, held files across
global service replacement, allocation-failure injection and hardware-specific
native effects were not run. The accepted same-representation restriction has
no FILE bridge; see [debt](https://git.internal/PyxisOS/pyxis-os/issues/1278).

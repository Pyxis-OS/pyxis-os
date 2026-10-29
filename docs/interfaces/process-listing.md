# Process listing

A program can list the processes and execution groups of its own space. A
program granted the system process view can list every space. The record layout
and constants are in `include/abi/process_view.h`.

## Authority (chronium, 2026-10-29)

- **Own space, implicit.** `SYSCALL_SPACE_PROCESSES` (12) lists the caller's
  own space. It takes no handle and needs no capability, space setting or
  manifest entry. Nothing in the call names a space, so other spaces never
  appear.
  - **Groups don't narrow it.** A process in a nested, sealed or contained
    group still sees its whole space.
  - **Grant profiles don't remove it.** Listing is not a grant, so a contained
    job can list its space.
  - **Shared spaces show everyone's processes.** For example, Remote clients
    sharing a space see each other's processes.
- **Every space, by request.** A process view object with `LIST` lists every
  published space, in registry order.
  - **Bootstrap:** the kernel gives boot init one `process_view` (`LIST`). It
    is not exported to ordinary children through environment aliases.
  - **Bundles:** a bundle requests the view in its manifest and receives it
    under the current bundle grant policy
    ([temporary debt](https://git.internal/PyxisOS/pyxis-os/issues/1480)).
  - **Without the grant:** a program has no handle, so it cannot make the
    call.

**Never in a row:** argv beyond its first word's last path component, the
environment, the working directory or the capability table.

## The call

Both entry points take a reply address and a capacity, and return the same
reply:
- **Entry points:** `SYSCALL_SPACE_PROCESSES` takes them as arguments; the
  `LIST` call takes them as its reply buffer, after a bare `message_header`.
- **Reply:** a `process_list_header`, then `returned` fixed-size records.
- **Result:** the reply's byte count.

**Capacity:**
- At most `PROCESS_LIST_REPLY_MAX` (64 KiB) is produced per call.
- If the records don't fit, the header sets `PROCESS_LIST_TRUNCATED` and
  `total` gives the full count; retry with more room.
- There is no cursor.
- A capacity smaller than the header fails `CALL_BUFFER_TOO_SMALL`.
- An unwritable reply fails `CALL_BAD_BUFFER`.

**Consistency:**
- **Structure:** each space's groups and processes come from one pass under
  that space's execution tree guard.
- **Figures:** CPU time, memory and state are samples taken during the pass.
- **Across spaces:** the system view is consistent within each space, not
  across spaces.
- **Copy-out:** records are written to a kernel buffer, allocated before the
  pass and copied out after it.

**Rows:**
- **Order:** group records come first, in preorder from each root group, then
  process records.
- **Membership:** a process is listed from publication until it retires.
  Prepared, unpublished members of a launch batch never appear; a batch
  appears whole.
- **Kernel workers** have no process and never appear.

## Records

| Field | Process | Group |
| --- | --- | --- |
| `key` | per-boot serial | per-boot serial |
| `group_key` | its group, 0 when ungrouped | its parent, 0 for a root |
| `launched_by` | the launching process's key, 0 for boot init | 0 |
| `space` | the space's fixed name | the space's fixed name |
| `label` | argv[0]'s last path component, up to 31 bytes | empty |
| `state` | `RUNNING`, `WAITING` or `EXITING` | `SEALED`, `STOPPING` and `COMPLETE` flags |
| `threads` | live threads | member processes |
| `depth` | 0 | 1 for a root |
| `user_ns`, `kernel_ns` | CPU time so far | 0 |
| `private_bytes` | committed private memory | 0 |
| `start_ns` | monotonic time of publication | 0 |

**Field details:**
- **Keys:** processes and groups share one series. Keys are never reused,
  are not process IDs, and no call accepts one.
- **`launched_by`** is information only, and it can name a process in another
  space.
- **Labels** are chosen by the launching program and can be spoofed.
- **State:**
  - `RUNNING` means some thread is running or queued for a CPU.
  - `WAITING` means every thread is blocked.
  - `EXITING` means the process has a result or a stop request and still has
    threads.
- **CPU time** comes from [exact TSC accounting](../kernel/smp.md#cpu-time-accounting):
  - it is current to the last syscall boundary, switch or local timer tick;
  - the kernel converts cycles with the calibrated TSC rate or, under KVM,
    kvmclock's scale;
  - with neither, it is 0 and `PROCESS_LIST_CPU_UNAVAILABLE` is set.
- **Private memory** is the process address space's owned backing: image,
  stacks, MEMORY and JIT regions. It is not RSS, and it does not attribute
  owned objects such as RAM files, collectors or pixels
  ([debt](https://git.internal/PyxisOS/pyxis-os/issues/1604)).

## Costs and limits

- **Cost when nobody lists:** a key at creation, a label copy during
  preparation, one timestamp and one list link at publication, and one unlink
  at retirement. A space's ungrouped launches now publish under its execution
  tree guard, as grouped ones already did. Owned-backing changes update one
  per-address-space counter.
- **Cost of a listing:** the pass holds the space's tree guard while it samples
  each process, up to the reply's capacity. That delays launches and group
  changes in that space for the duration. Samples take each process's lifetime
  lock and the scheduler queue lock. That and the 64 KiB cap are
  [debt](https://git.internal/PyxisOS/pyxis-os/issues/1603).
- **Implicit listing is a deliberate exception** to "authority comes only from
  held capabilities". It exposes labels and figures to every program in the
  space.
- **Stopping from the list** is later work.

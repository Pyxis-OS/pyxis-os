# Git slice 3 qualification

2026-10-24: final userland `e638a29240fcb5319537d2857b9f00ff8672a130`,
Go `f93462692a63de8d56cc3059e94f558f253fbbbb`; pinned go-git v5.19.3 /
go-billy v5.9.2 and the recorded dependency patches. Linux and P1F builds pass
fixed vendoring and offline compilation, retaining full notices and provenance.
P1F SHA-256:
`ecccf5db18d403541140e1f9b5f116212657ba72283940051fc27568be289363`.

## Setup

The [slice 2 record](../git-slice2/README.md) is the local-workflow baseline.
No kernel/ABI, Go-fork or ports change is needed. Reuse kernel `e57d9701e051` in
nested KVM/Q35, `max,+invtsc`, four vCPUs, 4 GiB, modern VirtIO net/RNG and
read-only VirtIO-SCSI ISO. Home is RAM-npfs in the 2 GiB logical live pool.
The fixture uses DNS `10.0.2.3` (QEMU's resolver proxy) because its previous
numeric public resolver cannot resolve the supplied private endpoint. This
changes only the qualification archive, not production network defaults.

Build both targets through `userspace/git/build.sh`, as in the Git guide, and
copy the ordinary development bundle into writable home. P1F code was checked
at `71801f7` before the documentation-only update and rebase; the final build's
executable hash is identical. No package ABI is relabelled for this core.

The only authenticated destination is chronium's disposable
`https://git.internal/codex/git-push-test.git`. The public
`ci/internal-ca.crt` supplies the Homelab Root CA via exact-origin `sslCAInfo`.
A one-response in-memory producer supplies the scoped token to the guest's
credentials file; no host credential copy, command argument or history entry
contains it. Remote output is discarded during that window; only completion
statuses are retained. Remove credentials, exit the RAM-only guest and delete
the qualification input afterwards. These cleanup steps are complete; the token
can be revoked. No credential-bearing guest disk, raw capture or screenshot is
retained.

The accepted simple-push form requires an upstream. Its initial-setup command
is still a proposal, so this run uses an ordinary non-secret repository-config
fixture declaring `origin` and `main`'s upstream. It does not claim a delivered
first-push CLI workflow. All actual clone, commit, push and pull operations run
through the native executable.

## Results

| Check | Observed result |
| --- | --- |
| Init and unborn state | Init creates main; empty log/status/cached diff work; first commit is parentless. Existing .git refuses. Remote add/list/set-url work. Bare push without upstream refuses. |
| CA and DNS | Public roots alone refuse the private endpoint; selected Homelab Root CA and split-DNS proxy permit verified HTTPS. Missing selected CA refuses. |
| Authenticated transfer | Native first push, authenticated clone, subsequent peer pushes and current-upstream pushes pass. Port 443 normalization retains the same exact-origin selection. |
| Push selection | An unrelated local branch is not published: independent Linux ls-remote advertises only main. Missing commit-tip object refuses locally. Removing credentials makes push refuse; anonymous clone still passes. |
| Fast-forward and ahead | Peer update pulls cleanly; already-current and locally-ahead pulls succeed without discarding local commits. |
| Dirty/index refusal | Tracked worktree edit and staged addition each refuse pull. The original local HEAD and local contents remain. |
| Collision refusal | An untracked file at a newly fetched path refuses checkout, preserving it and local HEAD; explicit removal permits a later fast-forward. |
| Divergence and rejection | Sibling commits refuse pull; local push refuses non-fast-forward, with the remote branch retaining its peer commit. |
| Directory transitions | Fast-forward creates a new parent directory, changes a tracked file into a directory/child, then changes that directory back to a file. Resulting status is clean and content matches. |
| Independent inspection | Anonymous Linux clone plus fsck --full --no-reflogs passes; history has the expected parentless first commit and remote main ends at b12f862fc612ef37c996af38b0df8b8bace921d4. |

An early push reached Forgejo but then failed on the library's local tracking
write through read-only storage. The CLI now suppresses that implicit write,
reports remote success separately, and explicitly publishes just the selected
tracking ref. An explicit retry followed independent remote inspection; it was
not automatic. The final dependency patch additionally rejects missing,
duplicate or unrelated push acknowledgements before reporting success; malformed
server responses were inspected in source, not injected in this qualification.

Raw non-secret control logs and build evidence remain under
`/shared/present/git/slice3/`. No GitHub authenticated write, native/PXE,
persistent-media timing, concurrent-writer stress, power cut or forced transport
failure is claimed. There is no whole-operation memory/deadline bound or
whole-worktree crash-atomicity guarantee. Existing sync-on-close remains pending
a separate native measurement. No public package publication follows this run.

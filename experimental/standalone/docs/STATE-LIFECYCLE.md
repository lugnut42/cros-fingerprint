# State directories and session lifecycle

Implemented offline on 2026-09-27. Public experimental APIs:
include/cros_fp_state.h, include/cros_fp_context.h, include/cros_fp_restore.h.
None is linked into the read-only diagnostic.

## Directory boundary

cfp_state_directory resolves a relative path beneath a trusted anchor descriptor.
Deployment should open `/` as the anchor and use `var/lib/cros-fingerprint` with
expected owner 0. Tests use a private temporary directory as the trust anchor;
they do not claim the world-writable /tmp ancestor satisfies deployment policy.

Every traversed directory must be owned by root or the expected owner and not
writable by group/other. The leaf must have the expected owner and mode 0700.
Symlinks, dot components, repeated/trailing slashes, and absolute paths fail.
OPEN never creates. CREATE creates only the leaf, refuses an existing entry,
syncs the leaf and parent, and never repairs permissions. Unexpected restrictive
umask or inherited policy may cause failure; a created leaf can remain after a
failure and needs administrative inspection. No automatic removal occurs.

The anchor and its ancestors are the caller's trust boundary. Root or the same
trusted UID can still rename/mutate directories; this does not defend against
those attackers. Mount and ACL policy, provisioning permissions, dump suppression,
and process lifetime remain deployment responsibilities. Filesystem fsync failures
for directory creation are handled but not yet injected in tests.

## Recovery policy

A new epoch always starts blocked. Only durable validated seed storage plus a
fresh, known-unset device seed permit restoration. Unknown/already-set device
state blocks a new process; no equality is inferred from the status bit.

Completion is accepted only for the pending generation. Reset, ownership loss,
cancellation, or relevant errors invalidate that generation. Generation
exhaustion fails closed. A SEEDED phase only records successful seed restoration;
it is neither a selected user context nor proof of authentication. The caller
must report actual reset/disconnect/ownership events; this model does not detect
them itself, authorize users, or prevent a privileged competing device client.

cfp_restore_seed loads existing state only, checks FP_ENC_STATUS v0 and FP_SEED
v0 support, rejects an already-set/unknown seed, sends the version-4 seed once,
and verifies the seed-set flag afterwards. Generation checks reject invalidation
during the exchange. Successful status is meaningful only under uninterrupted
exclusive ownership and the preceding successful seed write. No retries or
secret generation occur. Secret request/local buffers are cleared before return.

## Context switching

cfp_context_begin requires a seeded session and nonzero 32-byte trusted context.
It performs no I/O. Ticks send FP_CONTEXT v1 ASYNC, then GET_RESULT with the same
ID, spaced at least 100 ms apart. Only GET_RESULT BUSY is retried. A backward
clock, timeout, malformed response, other controller error, cancellation, or
lost generation fails the operation and clears the retained ID.

The caller must verify v1 support and hold one exclusive operation lock for the
whole switch, not merely for each command. There is no global lock in this
protocol component. It must not begin two context switches on the same session.
The caller drives monotonic ticks and bounds each synchronous exchange. It must
also reject completions past the overall deadline: the injected pre-call time
alone cannot measure elapsed ioctl time. The smoke-test executor now checks time after each exchange and wraps the
complete experiment in a five-second supervised worker.

Cancellation invalidates host state; it does not claim that a running sensor
reset stopped. Device mode/event cleanup and proof of a fresh context are needed
before future matching. Empty/mixed-user galleries remain forbidden by design.
Real seed provisioning and context switching succeeded on 2026-09-28;
seed/context restoration after one normal host reboot succeeded on 2026-09-28.
Explicit controller-only restart and power-loss recovery remain unverified.


The standalone executor is now built and tested offline. It deliberately uses
one test identity rather than claiming production account authorization. See
SEED-CONTEXT-EXPERIMENT.md for the completed first hardware action and remaining limitations.


## Account and reader binding preflight (2026-09-28)

`include/cros_fp_binding.h` defines in-memory schema-1 binding metadata and
`cfp_validate_gallery`; `src/binding.c` implements the pure validation boundary.
It is not an on-disk serialization format and is not linked into diagnostics
or the seed/context experiment. No enrollment/upload executor exists yet.

The trusted caller must select an active registry entry after authorization and
supply its nonzero 32-byte reader-generation, account-incarnation, and context
IDs. Reader generation denotes an installation/reader provisioning epoch, not
sensor model metadata. Reusing a UID or username must not reuse an incarnation.
These identifiers are compared field by field, not through structure padding.
Only binding schema 1 and template format 4 are accepted.

The entire gallery must pass before any template command. Empty galleries,
identity mismatches in any entry, zero IDs, unknown schemas/formats, duplicate
or invalid fingers, null/empty blobs, and blobs beyond the validated reader's
size limit are rejected. Count is bounded by the reader's capacity and ten
unique fingers. The function does not inspect blob bytes, parse template
structure, establish cryptographic integrity, or authenticate callers. Tiny
nonempty blobs can pass this metadata boundary; firmware/template validation
must still reject malformed templates before accepting any authentication.

The caller must hold an exclusive operation lock and keep metadata and blobs
stable through upload. This API cannot stop mutation after validation or detect
retirement of a registry entry by itself. Persistent registry creation, random ID generation, and retirement/tombstones
are now implemented offline (ACCOUNT-REGISTRY.md). Rename migration and the real
trusted fprintd adapter remain unimplemented. A copied/matching binding alone grants
no authority. U04/U05 have offline metadata-policy coverage. U01 and the registry portion of
U03 now have offline coverage; U02 and account-management integration remain open.

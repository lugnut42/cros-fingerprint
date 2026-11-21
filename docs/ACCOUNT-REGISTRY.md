# Persistent account registry

Implemented and tested offline on 2026-09-28. API:
`include/cros_fp_registry.h`; implementation: `src/registry.c`.
This is an experimental library component with no installed service. The local
administrative CLI now exposes explicit registration and checks (ADMIN.md);
retirement remains a library operation pending controller cleanup integration.
It is not linked into the diagnostic or hardware seed/context experiment.

## Trusted boundary

The caller supplies a dedicated directory securely opened under the private
state root, expected owner (root in deployment), and a trusted provisioning
reader-generation ID. The directory must be owner-owned mode 0700. Ancestor
validation belongs to `cfp_state_directory` and the caller. No production
registry directory is created by this development work.

Account selection must follow real service authorization. UID and username are
lookup keys, not proof of caller identity. Returned bindings identify a stored
account incarnation; they are not authentication results. No boolean or print
metadata can grant caller authority through this API.

All requests take a newly opened directory file description and nonblocking
exclusive flock, including reads and calls in forked processes. This serializes
cooperating registry clients. A privileged or same-owner writer remains trusted.
Device operations need a broader ownership lock: a binding copied before
retirement is not magically revoked. The service must stop in-flight operations,
invalidate cached bindings/session generations, and clear device state before
reassigning an account or reporting complete biometric deletion.

## Actions

- INITIALIZE: explicitly create an empty registry in an empty directory with a
  supplied, nonzero/non-ff 32-byte reader-generation ID. Existing entries block
  initialization. It does not generate a seed or infer a hardware identity.
- REGISTER: require an existing valid registry and a matching reader generation.
  Refuse any active entry with the requested UID or name. Generate two separate
  32-byte IDs with one OS-randomness call: account incarnation and controller
  context. Reject short/failed randomness, zero/ff IDs, and any identity collision
  with existing active or retired entries. No retry or silent replacement.
- LOOKUP: require the exact active UID/name pair and matching generation. Missing
  state or an inactive account returns missing without RNG or file creation.
- RETIRE: require the exact current binding in addition to UID/name/generation.
  Persist an inactive entry; retain its IDs permanently. A stale request cannot
  retire a newly registered incarnation with the same username and UID.

A later explicit REGISTER may reuse a retired name/UID but gets fresh IDs. The
old print bindings then fail gallery validation against the new incarnation.
Other users remain unchanged. No automatic OS account-deletion detection or
rename migration exists: administrative account lifecycle integration is still
a production gate. If an OS account is deleted/recreated without retirement,
UID/name alone cannot distinguish it; this component does not claim otherwise.

Names are limited to 1–63 ASCII letters, digits, underscores, hyphens, and dots.
They are stored fields, never path components. UIDs are stored as unsigned
64-bit integers. The prototype retains at most 64 entries, including retired
entries. Exhaustion requires administrative review; it never discards tombstones.

## Record and durability

`registry` is an exact-length checksummed snapshot, owner-owned mode 0600,
regular file, with one hard link. Symlinks, FIFOs, directories, wrong ownership,
permissive modes, malformed lengths, and unknown schema tags are refused.
Format 1 uses fixed-width little-endian fields:

| Offset/length | Field |
| --- | --- |
| 0 / 8 | Magic `CFPREG01` |
| 8 / 32 | Reader generation |
| 40 / 4 | Entry count, at most 64 |
| 44 / count × 144 | Entries |
| Final 32 | SHA-256 of all preceding bytes |

Each 144-byte entry contains UID (8), zero-padded name (64), account ID (32),
context ID (32), active flag (1), and zero reserved bytes (7). Bindings returned
by this schema use binding schema 1 and template format 4. Noncanonical padding,
invalid IDs/flags, duplicate identities, and conflicting active UID/name pairs
are rejected even with a valid checksum. SHA-256 uses OpenSSL EVP and detects
accidental corruption; it does not authenticate against privileged tampering.
This registry and its account-specific contexts must not be logged or committed.

Every mutation exclusively creates `.registry.pending`, writes a complete
snapshot, fsyncs and closes it, atomically replaces `registry` using renameat,
and fsyncs the directory before returning success. Any pending entry blocks
ordinary operations; the library never deletes it automatically. Before loading
an existing snapshot for use, the library also fsyncs its file and directory.
All outputs remain unchanged on failure. Internal record/identity buffers are
explicitly cleared; locked-memory and library-temporary audits remain open.

A pre-publication failure leaves the old snapshot in place and may leave a
pending file, blocking access. Failure after publication but before directory
sync is uncertain: the call fails without output, but a later successful load
can validate/sync the published snapshot. Do not assume failed retirement left
an active account, or failed registration created nothing. No automatic recovery
or rollback is implemented. Actual power-loss durability has not been tested.
A failed initial creation may leave pending evidence; preserve it for inspection.
Deletion/rollback of the whole directory cannot be detected without an external
provisioning anchor. Ordinary operations never recreate missing registry state.

## Verified and remaining work

`test-registry` uses synthetic randomness and temporary directories. It verifies
persistent distinct identities, reopening from a child process, retirement and
recreation, old-gallery rejection, stale retirement refusal, and preservation of
other users. It covers every truncated length and changed byte of a one-account
snapshot, semantic corruption with recomputed checksums, unsafe file types,
permissions, directory locks, two competing registration processes, identity
collisions with tombstones, capacity exhaustion, and injected random/write/sync/
publication failures. The mutation tests exercise short writes as well.

The normal suite, focused ASan/UBSan run (local leak detection disabled), and
GCC -fanalyzer passed. These are not proof of caller authorization, account
management hooks, crash consistency on real power loss, or biometric behavior.

The host store now connects generation to durable seed provisioning without
adopting the isolated experiment records, and invalidates its host session
before retirement (PROVISIONED-STORE.md). Next integrate authorized account
selection and supervised device cleanup, and resolve fresh-process recovery
with an already-set controller seed. Template storage/deletion, rename migration, suspend/event
cleanup, and isolated fprintd integration remain required before login use.

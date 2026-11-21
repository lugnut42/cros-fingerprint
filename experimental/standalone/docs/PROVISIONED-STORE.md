# Provisioned store and operation ownership

Implemented offline on 2026-09-28. APIs: `cros_fp_provision.h` and
`cros_fp_store.h`. The component initially added no production directory, CLI, device operation,
or service installation. A separate administrative CLI is now implemented
(ADMIN.md), but no real prototype store has been provisioned. Existing root-only hardware-experiment records are
preserved and are not adopted by this format.

## Provisioning record

A dedicated 0700 directory holds one 0600, single-link regular file named
`provision`: eight-byte magic `CFPPRV01`, random generation (32 bytes), random
seed (32 bytes), and OpenSSL SHA-256 over the preceding 72 bytes (104 total).
Creation draws 64 bytes from the OS RNG. Both fields must be neither all-zero
nor all-ff, and they must differ. This generation identifies a host provisioning
epoch; it is not derived from the seed or proof of physical reader identity.

The existing seed-store machinery now supports both 32-byte legacy payloads
and this separate 64-byte payload. Legacy seed/context names, tags, sizes, and
public APIs are retained. Provisioning uses `.provision.pending`, independent
flock, explicit empty-directory creation, no-replace publication, file/directory
sync, unchanged caller output on failure, and cleared temporary buffers.
Load never creates. The checksum is accidental-corruption detection, not
protection against trusted-owner tampering. See SEED-STORE.md for the shared
filesystem, durability, and secret-handling limits.

## Store ownership

`cfp_store_open` receives an already securely opened private root directory.
It validates owner/mode 0700 and holds a separately opened exclusive nonblocking
flock until `cfp_store_close`. Under this lock it either:

- Explicitly creates `provision/` and `accounts/` in an empty root, writes the
  provisioning record, and initializes the registry with that same generation.
- Loads the existing provisioning record and validates the entire registry,
  including an empty registry, against that generation using `CFP_REGISTRY_CHECK`.

Missing, corrupt, pending, or mismatched state never triggers regeneration or
repair. Failure leaves caller output unchanged and preserves any partial disk
state for inspection. A failed create may leave directories/records; retrying
create then refuses the nonempty root. This is not an all-or-nothing filesystem
transaction. A complete record published before a reported sync failure may be
accepted by a later successful load after validation and syncing. Ordinary open
requires both parts. It cannot detect rollback or replacement of the entire
store with an internally consistent older copy.

The returned noncopyable handle owns the lock, registry descriptor, provisioning
secrets, and a BLOCKED seed session. Opening a second cooperating owner fails
busy. `cfp_store_account` supplies the generation internally for register,
lookup, and retire; callers cannot choose a different generation per account.
Raw registry APIs remain low-level trusted building blocks and must not bypass
the outer ownership lock in an integrated service. The handle is not thread-safe.

## Retirement and stale context results

Every retirement attempt invalidates the handle's seed session before registry
I/O, even if the request is stale or storage fails. This conservatively blocks
all host operations sharing that session. A pending context tick then cancels
without another transport call. A completed context operation also rechecks
its session generation before returning success; the previous early return
could report stale success after invalidation and now has a regression test.

Successful registry retirement means its tombstone is durably stored, not that
the physical reader has stopped or that biometric deletion is complete. The
service must still stop device mode, drain stale events, clear templates, and
invalidate any cached binding/results before reuse. The controller seed may
remain set after host invalidation; fresh recovery still fails closed until a
reviewed restart/proof mechanism is available.

Close invalidates the host session, erases provisioning secrets, and releases
descriptors. All operations must be stopped/cancelled before close and must
not retain pointers across close/reopen. Caller authorization, real account
lifecycle hooks, memory locking/dump suppression, and device ownership remain
integration duties.

## Evidence and next work

`test-provision` verifies independent synthetic fields, persistence, invalid RNG,
exact lengths, corruption at each byte, and refusal by the legacy seed loader.
`test-store` verifies empty-root initialization, no creation on ordinary open,
exclusive ownership, persistent account bindings, generation mismatch after
swapping provisioning directories, missing-seed refusal, incomplete creation,
pending registry rejection, and pending-context cancellation after retirement.
Failed retirement also leaves the host session blocked. Existing seed, registry,
context, and workflow suites remain part of `make test`.

This is host-side composition, not a hardware restoration executor. The current
`cfp_restore_seed`/experiment CLI still use legacy experiment records. An explicitly supervised preparation executor is now implemented with offline
tests: it restores only the validated provisioned seed, checks context/seed
support, drives a registered account context, and holds store/device ownership.
Its administrative root CLI loads existing prototype state only. HANDOFF.md
records its current validation and pending live-test prerequisites. It must handle stop,
event drain, timeout and disconnect before enrollment. Already-set-seed recovery
and actual service authorization remain open gates. No real seed was generated
and no hardware was accessed in this implementation pass.

# Security acceptance scenarios

Design dated 2026-09-27. These are acceptance specifications; implementation
coverage is recorded below. Do not treat every row as an executed test.
Implement each behavior with a focused failing test, then the smallest change
that passes. Use only synthetic secrets/templates and disposable directories.

| ID | Scenario | Required assertion |
| --- | --- | --- |
| S01 | Explicit create in empty private state | RNG supplies exactly 32 bytes; durable record precedes any seed command |
| S02 | Ordinary open with no seed | No RNG, creation, seed command, or silent fallback |
| S03 | Existing valid seed | Load exact stored bytes; never overwrite |
| S04 | Short/trailing/corrupt/unknown-version record | Reject; outputs contain no partial secret; no device command |
| S05 | Symlink, directory, wrong owner, permissive file or ancestor | Reject without following/replacing unsafe state |
| S06 | RNG fails or returns partial bytes | No accepted record and no device command |
| S07 | Failure at write/fsync/rename/directory-fsync | No false durable success; old committed state remains usable where applicable |
| S08 | Concurrent initial provisioners | One durable winner; loser never programs a different seed |
| S09 | Seed disappears while template/context records remain | No automatic regeneration or record deletion |
| S10 | Fresh process sees device seed-set | Recovery required; bit alone never establishes secret equality |
| S11 | Device resets during a session | Prior session invalidated; no old success event accepted |
| U01 | Two synthetic users, same reader seed | Different stable contexts; each restored across process restart |
| U02 | Client requests another user's context | Trusted authorization rejects before context or template command |
| U03 | Username/UID recycled | Old registry entry is retired; no implicit inheritance |
| U04 | Print owner/reader/generation mismatch | Reject before upload or matching |
| U05 | Mixed-user or empty identify gallery | Reject; no controller-wide matching fallback |
| C01 | FP_CONTEXT async then BUSY then success | Same 32-byte ID in every poll; no templates before completion |
| C02 | Context switch times out/cancels | Session becomes unusable; late events cannot report success |
| C03 | Concurrent client or vanished owner | Serialize/reject; cleanup before reassignment |
| C04 | Seed already set during context reset | Do not assume reset clears or verifies seed |
| T01 | Truncated/oversized/unknown-format template | Reject before upload; bounded allocations |
| T02 | Template authentication failure in firmware | No match success or fallback to another template |
| T03 | Disk full during enrollment commit | No enrollment-completed result; clean uncommitted controller state |
| T04 | Partial transfer or crash between chunks | Session invalidated; no use of partially loaded template |
| T05 | Delete one user's record | Durable deletion and device cleanup; other users untouched |
| T06 | Match index outside active gallery | Protocol error; never select a different user's print |
| T07 | Persisting a match-updated template fails | Explicit tested failure policy; never claim durable update |
| I01 | Isolated fprintd claim for another account | Real PolicyKit/caller checks apply; driver metadata grants no authority |
| I02 | Service sandbox cannot access state | Clear failure; no relaxed permissions or default secret |
| I03 | Service restart, suspend, reader disconnect | No stale successes; preserve password path; recovery rules enforced |

S01–S10 are the next TDD increment. U02/I01 require integration tests; unit tests
with a boolean named "authorized" do not prove actual caller authorization.
Firmware authentication and hardware reset behavior cannot be proved with fakes.

## Seed-store coverage (2026-09-27)

`tests/test_seed.c` exercises the storage portion of S01–S09: explicit creation,
load without creation, preservation of existing/nonempty state, every truncated
record length, trailing data, a changed byte at each record offset, permissions,
wrong expected owner, symlinks, hard links, FIFOs, random failure/short return,
short writes, write/file-sync/publication/directory-sync failures, interrupted
state, and two racing processes. All use synthetic bytes.

This does not complete S01–S10. There is still no account/template registry or proof of an already-set seed
identity. A separate directory helper, restoration policy, and supervised
seed/context experiment workflow are now implemented offline.
The record API accepts an already securely opened directory; test-state now
covers the separate descriptor-relative directory helper. Tests do not simulate
power loss, root changing another account's file owner, arbitrary read failures,
or every possible filesystem operation failure. S10/S11 have offline policy coverage; actual event detection and hardware
recovery remain unverified.
See SEED-STORE.md for the exact API and failure contract.


## Subsequent offline coverage

`test-state` covers S05 path traversal plus S10/S11 generation policy.
`test-restore` tests durable load before seed mutation, capability/status gates,
single seed writes without retry, confirmation, and in-exchange invalidation.
`test-context` covers C01/C02 and generation invalidation during a reply.
`test-smoke` connects storage, seed restoration, and context switching; it tests
late completions against the post-call deadline as well as preflight refusals.
These tests do not establish actual D-Bus authorization, mode/event draining,
account retirement, or authentication. No state-changing hardware run occurred.


## Gallery binding coverage (2026-09-28)

`test-binding` covers the metadata boundary for U04/U05: valid same-account
prints, mixed galleries, a mismatch at every byte of each 32-byte identity,
recreated-account identity mismatch, zero IDs, unknown schemas/formats, empty
and oversized galleries, duplicate/invalid fingers, and missing/oversized blobs.
All bytes are synthetic. This pure function has no transport or storage access;
future upload code must invoke it for the complete gallery before commands.
It does not establish registry persistence, account retirement, caller
permissions (U02/I01), template cryptographic validation, or hardware matching.
Normal host reboot seed/context restoration is now observed on hardware; see
SEED-CONTEXT-EXPERIMENT.md. Already-set seed identity remains unresolved.


## Persistent registry coverage (2026-09-28)

`test-registry` covers U01 persistence and distinct synthetic account contexts,
and the registry portion of U03: explicit retirement, fresh identities on
re-registration, retained tombstones, and rejection of stale retirement actions.
Its gallery integration test rejects an old print against the recreated account.
A child process reopens the registry and obtains the same stored binding.
Corruption, unsafe files, concurrent registrations, capacity exhaustion, identity
collisions, and injected storage/RNG failures are covered; see ACCOUNT-REGISTRY.md.

U03 is not complete at the OS/service boundary: account deletion/recreation must
explicitly retire the old entry, invalidate live sessions, and clean up prints.
The registry cannot detect account recycling from UID/name alone. U02/I01 still
need actual authorization integration. No reader access or real account setup
occurred in these tests. Already-set seed identity remains unresolved.


## Provisioned ownership coverage (2026-09-28)

`test-provision` covers durable generation+seed storage and malformed records.
`test-store` tests registry/provision generation agreement, missing/pending state
without regeneration (S09), exclusive outer ownership (part of C03), and host
session invalidation before both successful and failed retirement. Its pending
context tick cancels without another device call. `test-context` now explicitly
rejects cached completed success after session invalidation (S11/C02).

These fakes do not prove device stop/event draining, real account authorization,
or hardware recovery. The new store is not connected to the legacy hardware
experiment CLI and makes no device commands. See PROVISIONED-STORE.md.

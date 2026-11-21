# Explicit prototype storage administration

Implemented 2026-09-28; offline-tested and now exercised in the explicitly
authorized first live prototype setup (PROTOTYPE-LIVE-PLAN.md). Executable:
`build/cros-fingerprint-admin`. This is a standalone administrative tool, not a
service or a fingerprint authentication interface. It has no device transport
linked in and does not restore seeds or start contexts/capture/enrollment.

## Commands

| Command | Behavior |
| --- | --- |
| `--plan` | Print the fixed state path, supported actions, and deadline; no execution |
| `--provision` | Create a previously absent private root, durable generation/seed record, and empty account registry |
| `--register UID NAME` | Require a current OS name/UID match, then durably register a new account/context identity in an existing store |
| `--check` | Open, validate, and sync an existing store and registry |
| `--check-account UID NAME` | Also require a current OS name/UID match and an active exact registry entry |

All executing commands require effective UID 0 in the production binary. The
state path is fixed to `/var/lib/cros-fingerprint-prototype`. There is no path,
owner, randomness, timeout, or test override in its CLI/environment. Ancestors
must already exist and pass secure directory traversal. Even an existing empty
leaf blocks provisioning; no command repairs permissions or adopts old state.
The isolated `/var/lib/cros-fingerprint-experiment` records are never read or
migrated by this tool. Provisioning generates a separate seed/generation.

The store lock serializes checks and registrations with the preparation executor.
Exclusive leaf creation arbitrates initial provisioning. Registration rejects
active UID/name reuse; check commands never generate missing state. Username/UID
validation is a lookup, not account-incarnation detection: administrative hooks
for OS account deletion/recreation are still required.

Core dumps are suppressed before the worker starts. All lookup, traversal,
randomness, validation, and storage work runs inside the existing five-second
supervisor. Successful output contains only action, `status: ok`, and
`hardware_access: false`. Account IDs, context IDs, seeds, names, and UIDs are
not printed. Root authority and dump suppression are not locked-memory or
offline-disk protection.

## Failures and limits

Exit 2 means invalid usage; 1 means authority, setup, storage, or output failure;
124 is worker timeout; 125 is worker/IPC failure. Failed executing commands print
no success JSON. Storage errors include only the numeric `cfp_store_status`.
A failed operation may already have published data: preserve the entire state
root, including pending files. Do not remove pending evidence or blindly retry
provisioning. Checks also fsync validated state and can fail on storage errors.

The supervisor bounds the parent's wait, not an uninterruptible kernel call.
A failed/timed-out write is not rolled back. Registry pending files block further
use; initial partial creation blocks reprovisioning. Inspect and design recovery
explicitly. There is no repair, overwrite, migration, backup, retirement, or
biometric-deletion command. Retirement needs coordinated controller cleanup;
a storage tombstone alone must not be advertised as fingerprint deletion.

## Offline tests

`tests/admin_driver.c` is a separate test-only entry point using the same parser,
worker, supervisor, directory traversal, and store APIs. Its trusted platform
selects a temporary anchor, synthetic account lookup/RNG, and injected failures.
Only this test executable understands `CFP_TEST_*` environment variables. Tests
also call the production executable for non-executing plan/argument checks and,
when unprivileged, authorization refusal.

`tests/test_admin_cli.py` covers creation, duplicate refusal, account validation,
reopening, no regeneration, corrupt/partial state, unsafe paths, exclusive locks,
concurrent provisioning, RNG/write/sync errors, NSS-like hangs, and interrupted
writes. A hung registry writer is killed, its locks are released, and its pending
file remains. Synthetic RNG calls verify dump suppression before returning data.
No real state root, account registry, or seed was created by these tests.

## First live storage experiment

Scope: create one independent prototype store and register the explicitly chosen
current OS UID/name; check it; leave all private records in place. No device
commands, reset, enrollment, PAM, packages, or services are part of this scope.

1. Inspect `build/cros-fingerprint-admin --plan` from the reviewed build.
2. With explicit authorization for this live scope, run
   `pkexec experimental/standalone/build/cros-fingerprint-admin --provision`.
3. Run that executable with `--register UID NAME` for the authorized account,
   then `--check` and `--check-account UID NAME`. Substitute the actual UID/name;
   the command verifies the pair against the OS inside its deadline.
4. Expect only the allowlisted success JSON. Preserve state on every failure.
   Retain the original experiment records separately. Do not automatically run
   preparation afterward: it is a different, reader-changing experiment.

The user subsequently authorized the combined setup scope in
PROTOTYPE-LIVE-PLAN.md. Provisioning, account registration/checks, and one reader
preparation have now succeeded; retain the real prototype records. Standalone preparation currently exits
with the reader seed set, so a later process remains blocked on unknown seed
identity. A useful enrollment experiment needs a continuous owned session or a
proven recovery mechanism, plus mode/event cleanup. This storage tool does not
resolve those prerequisites.

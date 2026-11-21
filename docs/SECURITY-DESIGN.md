# Security and lifecycle design

Updated 2026-09-27. Status: prototype design baseline; implementation and
validation remain required. Milestone 2 is not complete. Nothing in this
file authorizes automatic provisioning or establishes login readiness.

## Threat model

Protect the seed, encrypted templates, account-to-template associations, and
fresh authentication results against ordinary local users, cross-user requests,
corrupt/truncated files, stale events, concurrent operations, and process crashes.
Trust the kernel, root, the selected service, and firmware for this prototype.
A compromised root or service can impersonate authentication results; a local
root-only file does not protect secrets against offline disk access. Disk
rollback, malicious firmware, physical sensor spoofing, and ChromeOS-equivalent
TPM protections are not solved by this design. Keep password fallback.

## Evidence and implications

Source references and precise limitations are in SECURITY-SOURCES.md.

- fprintd checks the D-Bus caller identity and PolicyKit permissions, records a
  claimed username, loads that user's prints, and supplies the username for
  enrollment. Its file loader checks username, finger, and device compatibility.
- libfprint's enrollment, verification, and identification callbacks provide
  FpPrint objects; their username is nullable, caller-controlled metadata, not
  proof of identity. Driver-specific data can be serialized with the print.
- The inspected EC revision accepts a 32-byte seed once while its seed-set bit
  remains set. Its context reset clears templates/user state but retains that
  bit. Seed status does not identify the secret currently held by the device.
- FP_CONTEXT v1 starts an asynchronous reset; GET_RESULT installs the supplied
  user ID after the reset finishes. Every poll must carry the intended context.
  It is a state-changing operation, never part of read-only diagnostics.
- Template format is 4; the inspected crypto implementation provides HKDF and
  AES-GCM with firmware-held key material. This is not evidence that our host
  authorization, record ownership, or replay protections are correct.

## Integration decision

Keep fprintd as the intended authorization and print-storage owner, with a
libfprint driver providing controller operations. No new public D-Bus daemon
or PAM module is justified by the reviewed interfaces. The first standalone
experiment will be an explicitly invoked administrator tool with no login role.

A separate long-running privileged helper remains conditional: introduce one
only if device ownership, secret delivery, or cancellable I/O cannot fit the
service/driver lifecycle. A provisioning command is an administrative utility,
not a general-purpose background service. It must never run from probe/open.

The reviewed fprintd unit restricts writes to its state directory and already
lists /dev/cros_fp in DeviceAllow. A future deployment must deliberately arrange
read access to seed/context records and any required writes; do not assume a
driver can create arbitrary state files under its sandbox. No unit is changed
now. Integration needs an isolated service test against a pinned libfprint base.

## Seed lifecycle

Prototype choice: one random 32-byte seed per installation/reader generation,
created by an explicit provisioning action using the OS cryptographic RNG.
Never derive it from a username, machine ID, password, or compiled constant.
Do not implement our own cryptography. A single device seed is acceptable only
with independent user contexts and host authorization; it is not a shared
constant distributed to every installation.

Proposed private state root: /var/lib/cros-fingerprint, root-owned mode 0700;
secret files root-owned 0600. This is a design path, not an installed directory.
Use directory-relative operations, reject symlinks/non-regular files, validate
owner, permissions, length, and schema, and create without replacing existing
state. Serialize provisioning. Persist the complete record and fsync its file
and directory before sending the seed to the controller. A failed durable write
must result in no seed command. Do not pass seeds in argv, environment, logs,
JSON diagnostics, core dumps, or repository fixtures. Minimize and explicitly
clear secret copies; assess locked memory and dump suppression at implementation.

Storage must distinguish intentionally unprovisioned state from a missing or
corrupt existing generation. Verification/open never generates replacement
secrets. Explicit initial provisioning must also refuse evidence of existing
records. Reprovisioning is a separate destructive administrative workflow.

| Situation | Required behavior |
| --- | --- |
| Valid saved seed, device seed unset | Restore the saved seed under exclusive ownership; do not generate another |
| Device seed set by this still-valid exclusive session | Continue only if no reset/ownership uncertainty occurred |
| Device seed already set on a fresh process | Treat seed identity as unknown; do not infer equality from the bit |
| Missing/corrupt seed with existing records | Disable fingerprint operations; preserve files; require recovery |
| RNG, permission, short-read, or fsync failure | Fail without sending seed/context/template commands |
| Seed lost, reader replaced, or firmware key material changes | Treat templates as potentially unrecoverable; explicitly re-enroll if recovery fails |

Restart recovery with an already-set seed remains a gate. Options are a reviewed
controller restart that demonstrably clears the seed, or a firmware-supported
proof that the stored secret matches. Neither a sensor-context reset nor
reloading a template alone is assumed to prove seed equality. Investigate this
before enrollment; no automatic reset, reboot, or seed overwrite is permitted.
The initial design accepts unavailable fingerprint authentication over guessing.

Root-only storage is the prototype baseline, not a claim of hardware-backed
custody. Evaluate TPM sealing and its upgrade/recovery behavior separately.
Backups of seed and context records must be protected as secrets; copies alone
are not promised to work on another controller. Do not add automatic backup.

## User identity and context

Generate a separate random, nonzero 32-byte controller context for each account
incarnation under a given reader generation. Persist the mapping before use.
The context is an opaque identifier, not authentication evidence or a password.
Do not derive it solely from a reusable username or UID. Do not accept a context
chosen by an untrusted desktop client.

The trusted integration must map its authorized account to a registry entry;
print metadata is checked against that entry. Each record binds installation/
reader generation, opaque account ID, context ID, template format, and finger.
Reject missing/mismatched ownership, mixed-user galleries, empty galleries,
unknown schemas, and unsupported formats before loading templates. Never let
an empty identify gallery match whatever happened to remain in the controller.

Account deletion/recreation is an explicit lifecycle requirement: retire the
old mapping and prints before reusing a login name/UID. UID+name matching alone
cannot detect every account recreation. Until account-management integration
exists, this is an administrative requirement and an unresolved production gate.
Renames require an explicit mapping migration, not accidental adoption of files.

Switching users must stop the current mode, complete context reset, discard stale
events, install the intended context, and load only that user's selected prints.
Hold one operation/ownership lock across the entire sequence. Context command
polls must repeat the same ID. A failed or cancelled switch leaves the session
unusable until cleanup has been confirmed. Advisory locks coordinate our own
processes; they do not stop a privileged process from accessing /dev/cros_fp.

## Template persistence and results

Keep firmware-encrypted template blobs opaque. Never export raw images as part
of enrollment or verification. Validate bounded length, format, owner, reader
binding, and record schema before upload; the firmware must validate the
cryptographic template. Do not equate host-side header validation with integrity.

For the standalone prototype, store records under the private state root with
atomic replacement and durable writes. For integration, use FpPrint driver data
for versioned binding metadata and the blob; let fprintd store prints. Confirm
its actual durability/error behavior in integration tests rather than claiming
the current storage implementation provides our full transaction policy.

Complete enrollment only after the template and mapping are durably stored.
On failure, clear the uncommitted in-memory template and report failure. Persist
match-updated templates before claiming that the update will survive restart;
choose and test whether a failed update aborts authentication before integration.
Deletion must remove the host record durably and clear corresponding device
state before reporting complete. It must not delete other users' records.

A result belongs to one live operation, device session, and context generation.
After cancellation, disconnect, reset, timeout, suspend, or caller disappearance,
ignore late results and fail the operation. A host generation counter alone
cannot tag an unlabelled kernel event: mode shutdown, event draining, and a new
confirmed context are also required before a new operation. Test these on fake
transport first. Match indexes must refer to the current loaded gallery.

## Outstanding gates and next implementation

1. Build an offline seed-store component with injected randomness/filesystem
   failures and no hardware calls. Separate create from load; test atomicity,
   permissions, symlinks, corruption, concurrent creation, and lost-state refusal.
2. Implement and test a pure session/recovery policy, then FP_CONTEXT v1 command
   sequencing with bounded busy polling, cancellation, and identical user IDs.
3. Resolve already-set-seed recovery using the matching firmware source and a
   concrete, separately scoped hardware experiment. Confirm mode/event cleanup.
4. Establish account-registry lifecycle and the exact trusted adapter boundary.
5. Test template persistence failures and isolated fprintd compatibility before
   any system installation or login integration.

SECURITY-TESTS.md defines the acceptance scenarios. These are planned tests,
not passing tests. Real enrollment remains a separate experiment with specified
state changes, private storage location, and cleanup. No real seed or biometric
material was created in this design pass.

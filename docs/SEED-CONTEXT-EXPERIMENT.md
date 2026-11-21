# Seed and context hardware experiment

Prepared 2026-09-27. Executed successfully with user authorization on 2026-09-28.
This is a single administrator experiment, not enrollment or login integration.
It does not complete the account-registry or already-set-seed recovery design.

## Exact action

After explicit authorization, run from the reviewed checkout:

```sh
pkexec /home/t/Work/cros-fingerprint/build/cros-fingerprint-seed-context-test --initialize
```

`--plan` displays the scope without root, device access, or filesystem writes.
Running without an explicit action is an error. The implementation is
src/experiment_main.c plus src/smoke.c; library pieces are separately tested.

The command will:

1. Require root, suppress core dumps, and set a private umask. Open /dev/cros_fp
   without following symlinks, check its type/owner, and take an advisory lock.
2. Query metadata. Refuse a different target model/vendor/product, non-v4
   template format, loaded templates, or an already-set/unknown seed.
3. Explicitly create /var/lib/cros-fingerprint-experiment and its seed/context
   subdirectories, all root-owned 0700. Existing state blocks initialization.
4. Confirm required context-command support. Durably store independent random
   32-byte seed and context records under separate subdirectories (0600 files).
   Validate both records before the first device mutation.
5. Check seed/encryption command support and status again, set the saved seed
   once with FP_SEED v0/format 4, and verify the seed-set status.
6. Run FP_CONTEXT v1 asynchronous reset/completion with the saved test context.
   Poll at 100 ms intervals, with a two-second context deadline checked before
   and after calls. The entire worker has a five-second parent deadline.
7. Query metadata again and return reviewed JSON. No secrets or context IDs are
   printed. Success requires zero templates and seed-set status.

Only GET_CMD_VERSIONS, FP_INFO, FP_ENC_STATUS, FP_SEED, and FP_CONTEXT are issued.
There is no capture, enrollment, template upload/export, authentication change,
service installation, firmware update, or automatic controller reboot.
The context is an isolated test identity, not associated with a login account.

## What remains afterwards

The seed and context stay in reader memory as firmware permits. The private
files remain on disk for controlled recovery. A later fresh process refuses an
already-set seed; --restore only loads existing files when the reader reports
its seed unset. This test does not establish that a host reboot clears the seed,
and never reboots or resets the controller automatically.

Failure after a state-changing command can leave device state uncertain. Stop
and inspect metadata; do not retry blindly, delete saved secrets, or recreate
state. A partial durable write can leave a pending record requiring manual
review. No cleanup action is automatic. Preserve the experiment files until
recovery behavior has been established. No biometric cleanup is required because
none is collected. File deletion would not prove secure erasure from storage.

The directory is separate from proposed production state. It must not later be
silently adopted as a production account registry. Root-only plaintext storage
is the prototype baseline; secrets are not TPM sealed or locked out of swap.

## Verification and limits

`test-smoke` runs the complete workflow using synthetic storage and an allowlisted
fake device. It rejects existing templates, unsupported context commands, missing
records, already-set seeds, and a late context reply. `test-restore`,
`test-context`, `test-state`, and `test-seed` cover lower-level failure cases.
CLI tests exercise --plan and invalid arguments only, never a live mutation.

Normal tests and ASan/UBSan passed (local leak detection disabled as previously
documented); GCC -fanalyzer completed without diagnostics for the experiment.
A read-only preflight reported fprintd inactive and fuser reported no holder.
That is a point-in-time observation, not proof that no privileged client can
open the device later. Recheck before execution. Advisory locks coordinate our
own tools but do not exclude a client that ignores them.

The supervisor bounds the parent wait. SIGKILL cannot immediately stop every
uninterruptible kernel operation. A timed-out operation may outlive the parent;
inspect process/device state before any further experiment.


## Observed execution (2026-09-28)

The user authorized the proposed action by asking to continue after the explicit
scope/permission question. Rechecked fprintd: inactive; fuser reported no holder.
Ran the reviewed --initialize command via pkexec. It exited 0. A separate
read-only diagnostic in a fresh process also exited 0 and reproduced the result.

The controller accepted FP_SEED and completed FP_CONTEXT v1. Postflight reported
valid_flags=1, status=9, seed_set=true, zero loaded templates, dirty bitmap 0,
format 4, errors=1023, and fault_flags=0. Only bit 0 is advertised valid, so the
extra raw status bit is not treated as an independently supported context-status
field. Successful context completion is based on the command acknowledgement.

Metadata-only filesystem verification found all three experiment directories
owned by root with mode 0700. Both seed and context records are root-owned mode
0600 and 72 bytes. No secret contents were printed, copied into the repository,
or included in logs. These files are now real private state and must be preserved.

No fingerprint was captured/enrolled and no authentication configuration or firmware image
was changed. The reader remains initialized. Recovery after a normal host reboot is now verified below. Service restart
with an already-set seed and power-loss recovery remain unverified.

A fresh invocation of --restore while the seed remained set exited 1 with
CFP_RECOVERY_REQUIRED (numeric status 12), as designed. The worker's preflight
refused before filesystem setup or seed/context writes. This validates the
fresh-process refusal path on hardware, not successful post-reset recovery.

## Next controlled recovery check

The next experiment requires a user-scheduled normal computer reboot. Do not
reboot automatically. After the user returns, first run the read-only diagnostic
and recheck reader ownership/service use. If the reader reports a known-unset
seed and zero templates, run the existing --restore action against the same
saved records, without initialization or regeneration. Check the resulting
seed status and successful context completion. Preserve all private records.
If a normal reboot does not clear the seed, stop and review before proposing
another reset mechanism; do not assume a reboot is a controller power cycle.

Reset-source research retrieved common/system.c at e07b67c with SHA-256
`de67a581d318aedca30a74e8aad9559815277436d6348e00a721bc99b71a2ed0`.
It contains EC_CMD_REBOOT_EC handling, but board/bloonchipper/board.h and board.c
requests returned HTTP 503 from the source server. This is a source-download
failure, not a local device failure. No reader reboot command was sent and no
board-specific reset-scope guarantee was inferred from the generic handler.


## Post-reboot restoration observed (2026-09-28)

The user returned after the scheduled reboot and asked to resume the test.
Read-only preflight reported fprintd inactive and fuser reported no holder.
The diagnostic exited 0 with valid_flags=1, status=0, seed_set=false, zero
loaded templates, dirty bitmap 0, template format 4, and fault_flags=0.
This normal host reboot cleared the controller seed in this observed run;
it does not establish the behavior of every restart or power transition.

`make test` passed all offline suites. Reviewed the existing restore path and
ran `pkexec /home/t/Work/cros-fingerprint/build/cros-fingerprint-seed-context-test --restore`.
It exited 0, loading the existing private records without initialization or
regeneration. FP_SEED was accepted and FP_CONTEXT v1 completed successfully.
A separate fresh-process read-only diagnostic exited 0 and confirmed
valid_flags=1, status=9, seed_set=true, zero templates, dirty bitmap 0,
format 4, errors=1023, and fault_flags=0. Only seed bit 0 is advertised valid;
context completion is established by the experiment command's success.

The saved records were preserved and no secret contents were displayed.
This verifies seed/context restoration after one normal reboot. It does not
verify biometric template restoration or matching, since none are enrolled.
The reader is initialized again. Do not rerun initialization or blindly retry
restoration while its seed remains set. Next development work is already-set
seed recovery and account/reader binding before live enrollment; suspend,
power loss, and isolated service integration remain open.

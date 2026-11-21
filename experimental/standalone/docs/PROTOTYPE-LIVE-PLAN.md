# First live prototype setup — authorized and executed

Prepared 2026-09-28; explicitly authorized by the user and executed in this
session. Final independent mode check was cancelled while awaiting desktop authentication
before the user's planned reboot. This is a seed/context integration experiment, not enrollment
or login. Offline implementation and full normal/ASan/UBSan tests pass. The
read-only configured-mode query has returned mode 0 on the real reader.

## Requested scope

Create one independent root-only prototype store at
`/var/lib/cros-fingerprint-prototype`, register the selected current OS account,
and run the supervised preparation executor once while holding store/device
locks. This generates a new host generation/seed and account/context records,
then sends FP_SEED and FP_CONTEXT only after target metadata, zero templates,
known-unset seed, supported commands, and configured idle checks succeed.

The original `/var/lib/cros-fingerprint-experiment` records are retained and
never read, overwritten, or adopted by these commands. This new store does not
inherit their identity. No biometric image/template is captured or exported.
No mode write, controller reboot, firmware change, service installation, PAM,
lock-screen integration, or system libfprint replacement is in scope.

Successful preparation leaves the controller seeded. A fresh process still
cannot establish seed identity from its status bit; it must refuse further
preparation while already set. A later experiment may therefore require an
explicit host reboot or a separately reviewed recovery step. No reboot/reset or
seed replacement follows automatically. Mode 0 is a configuration observation,
not proof that the whole event path is quiescent.

## Execution after approval

All binaries are from `experimental/standalone/build`. Check fprintd status
and cooperating device holders before state-changing reader work. Confirm the
actual selected UID/name against the OS. The current shell reports UID 1000 and
name t; recheck rather than relying on this note indefinitely.

1. Run the read-only metadata and --mode-status diagnostics. Require the target,
   zero loaded templates/faults, known-unset seed, and mode 0. On any discrepancy,
   stop the live experiment and report it; no automatic recovery.
2. Run `pkexec .../cros-fingerprint-admin --provision` once. The fixed root must
   be absent; an existing or partial root is a refusal, never permission to clean
   or replace it.
3. Run `pkexec .../cros-fingerprint-admin --register UID NAME` and
   `--check-account UID NAME` using the authorized current account.
4. Run `pkexec .../cros-fingerprint-prepare --prepare UID NAME` once.
5. If it succeeds, run separate read-only metadata/mode diagnostics. Expect
   seed_set=true, zero templates/faults, and configured mode 0. Only allowlisted
   status fields are recorded. Do not infer unknown encryption-status bits.
6. Validate the new store with `cros-fingerprint-admin --check`. Preserve all
   private files, including after any failure; never print their contents.

A preparation timeout/error can leave the reader partly changed and a successful
seed write cannot be undone by closing the process. Preserve records, perform
only read-only observation, and request a new recovery scope if needed. This
experiment proves neither enrollment/matching nor secure authentication.

## Why approval is needed

AGENTS.md requires that the scope of real reader state changes be authorized
before execution. The prior experiment authorized its own isolated records; it
is not authorization to provision this new identity. This plan makes the new
writes and state changes concrete. Approval of this plan does not authorize
capture/enrollment, reboot, retirement, or login integration.

## Execution record

The user approved this exact scope with “yes” and reaffirmed continuation after
an interruption. No setup worker remained running at resume. Fresh preflight
reported seed unset, zero templates and dirty bitmap, zero fault flags, model
543/format 4, and configured mode 0. fprintd was inactive; privileged fuser found
no reader holder (exit 1, no output).

Provisioning, registration of the current OS account, and check-account all
returned success. The single preparation run exited 0. Its postflight reported
seed_set=true (valid_flags=1, raw status=9), zero loaded templates/dirty bitmap,
and zero fault flags. Successful preparation also includes configured-mode-0
postflight internally. A separate metadata diagnostic independently reproduced
those values. A separate admin --check validated the saved store successfully.
Only bit 0 of encryption status is interpreted; other bits remain unsupported.

The additional independent --mode-status invocation waited in pkexec before
the diagnostic worker started. It was cancelled with SIGTERM before the user's
planned reboot; it produced no mode result. The preparation's internal mode-0
postflight succeeded, but this separate check was not completed. No further
state-changing invocation is authorized by a delayed or failed postflight check.

Real records now exist under /var/lib/cros-fingerprint-prototype. Preserve them
and the separate original experiment records. No secrets or biometric material
were displayed. No capture, enrollment, mode write, reset, reboot, or login
integration occurred.

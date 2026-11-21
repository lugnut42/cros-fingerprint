# Current state — 2026-11-20: working on this machine

Read this section first; everything below it is history from the September
sessions and is superseded where it conflicts.

The reader works for day-to-day use via the patched `crfpmoc` libfprint driver
and stock fprintd. Verified live on 2026-11-20: enroll, match, wrong-finger
rejection (build-tree examples and fprintd), then sudo, polkit and the Omarchy
lock screen as reported by the user. A fingerprint sudo after a reboot is
confirmed in the journal (2026-11-20 17:12); the user also reported success
after the follow-up lid close/open check.

What is installed / changed on the system:

- `libfprint-crfpmoc-local 1.94.10-1` (built by `packaging/PKGBUILD` from the
  reference checkout with all five `patches/` applied) and official
  `fprintd 1.94.5-2`. Rebuild with `cd packaging && makepkg -f --nodeps`.
- `/etc/pam.d/sudo` (backup: `/etc/pam.d/sudo.before-fingerprint`),
  `/etc/pam.d/polkit-1` (new), `/etc/pam.d/omarchy-lock-fingerprint`, the
  fprintd resume hook and stop-timeout drop-in (written by `omarchy-apply-lock`).
  SDDM login is password only. Undo: `scripts/enable-fingerprint-auth.sh --undo`,
  then `sudo pacman -Rns fprintd libfprint-crfpmoc-local`.
- One enrolled print (right index) under `/var/lib/fprint`.

Driver patches added this session (tests are run by hand, see each script):

- `libfprint-4df05918-fp-info-versions.patch`: negotiates FP_INFO v1/v2/v3
  (this firmware has only v2/v3) with bounded variable-length decoding.
  `tests/test_reference_fp_info.py <driver dir>`.
- `libfprint-4df05918-live-fixes.patch`: enroll result codes stored regardless
  of debug logging; seed decision uses the seed bit; enroll idles the sensor
  before FP_CONTEXT. `tests/test_reference_live_fixes.py <driver dir>`.

Decisions made by the user on 2026-11-20:

- Goal is a working reader for sudo and lock screen; shortest path preferred
  over finishing the standalone stack.
- Use the driver's fixed public seed/context for now, harden later. A
  machine-unique seed is a follow-up and costs only a re-enroll. The standalone
  seed store/registry/admin tools in `src/` are not used by the working setup.

Do not:

- Run Omarchy's `omarchy-setup-security-fingerprint` (it only detects USB
  readers and would replace our library with `libfprint-git`). Decline any
  Omarchy offer to install `libfprint-git`.
- Run the old `--restore`/`--prepare` tools: they program a different seed, and
  prints enrolled while it is set fail after the next reboot.

Open items: machine-unique seed; the
checkout's replay fixture no longer matches the open/enroll sequence; identify
with more than five prints fails; a poor touch during verify reports no-match
rather than retry. `~/fp-smoke` holds smoke-test logs and a root-owned test
print that can be deleted.

# Reboot handoff — 2026-09-28

Resumed on 2026-09-28 at the user’s request. See the resume checkpoint below;
the pause instructions and stopping-point observations describe the prior session.

User explicitly requested a pause to reboot. Stop development/hardware work
until the user returns and asks to resume. Repository: /home/t/Work/cros-fingerprint.
Read AGENTS.md, README.md, PLAN.md, and this file first.

## Stopping point

The account-specific preparation workflow and its supervised CLI are implemented
and their focused tests pass. This is an offline development milestone, not
working fingerprint enrollment or login. Final full-suite results are recorded
at the bottom of this file before ending the session.

Latest additions:

- `include/cros_fp_prepare.h`, `src/prepare.c`, `tests/test_prepare.c`: existing
  store/account lookup before device commands; target metadata/zero-template/
  known-unset seed gates; supported context command; validated stored seed
  restoration; bounded asynchronous context setup; metadata postflight. Guarded
  transport checks timeout and session invalidation around every exchange.
- `src/prepare_main.c`, `tests/test_prepare_cli.py`: new executable
  `build/cros-fingerprint-prepare`. `--plan` is non-executing. Explicit
  `--prepare UID NAME` requires root, verifies the current OS UID/name pair,
  opens only existing `/var/lib/cros-fingerprint-prototype` state, takes store
  and device locks, suppresses dumps, and runs under the five-second parent
  supervisor. No create/register/migration/capture/reset functionality.
- `cfp_restore_validated_seed` factors the existing reviewed seed-command path
  for a validated store seed. Legacy `cfp_restore_seed` remains the storage
  wrapper for the original experiment. Fixed a regression where cancellation
  during preflight could be lost before seed programming.
- `src/linux.c` now clears the ioctl packet before freeing it on success and
  error paths. A linker-wrapped free in `test-linux` verifies clearing before
  release. Previously the packet could retain seed/context bytes on the heap.
- Makefile builds/runs the new workflow and CLI tests. All tests remain offline.

Previously completed: read-only diagnostic; legacy seed/context experiment;
provisioning envelope binding generation+seed; persistent account registry with
retirement tombstones; store ownership; gallery identity validation; cancellation
of cached context successes after session invalidation. See PROVISIONED-STORE.md,
ACCOUNT-REGISTRY.md, STATE-LIFECYCLE.md, and TESTING.md.

## Actual hardware/private state

No hardware operation or real provisioning occurred during this development
pass. The only real state remains the isolated original experiment under
`/var/lib/cros-fingerprint-experiment` (root-only seed/context records). Preserve
those files; never print their contents, regenerate them, or adopt them as a
production account registry. No fingerprint has been captured or enrolled.
No PAM, lock-screen, firmware, system library, or service configuration changed.

The earlier normal reboot cleared the reader seed. Explicit restoration from
those same saved experiment records succeeded, and a separate diagnostic
confirmed seed_set=true, zero templates, and zero fault flags. That was the
last hardware observation, not a prediction of the next boot. See
SEED-CONTEXT-EXPERIMENT.md. The new prototype state directory has NOT been
created; the new preparation CLI cannot yet perform a successful live run.

## Resume after reboot

1. Run the existing read-only diagnostic outside the sandbox with root access:
   `pkexec /home/t/Work/cros-fingerprint/build/cros-fingerprint-diagnostic`.
   Check reader usage (`systemctl is-active fprintd`, privileged holder check
   if needed). Sandbox visibility is not evidence of a missing device.
2. Record actual seed/template/fault status. Do not automatically restore merely
   because the seed is unset: the old-record reboot restoration was already
   demonstrated, while a new provisioning experiment needs a separate reviewed
   scope. An already-set seed is unknown to a fresh process and remains blocked.
3. Continue reviewing/integrating the new preparation executor. Add an integrated
   supervisor/fake-device test for hanging exchanges and ownership release.
   Existing supervisor tests cover deadline behavior, but the new CLI currently
   has only --plan/argument tests; the workflow has injected transport tests.
4. Resolve controller restart/recovery and mode/event cleanup using pinned
   firmware/kernel sources before enrollment. The reviewed context reset does
   NOT clear/prove the seed. Do not invent or automatically send reset commands.
5. Prepare explicit prototype provisioning/account registration tools and a
   reviewable live-test plan if appropriate. The new storage API exists, but
   no administrative provisioning CLI or migration from experiment state exists.
   Keep password fallback and leave system login integration for later milestones.

## Research and remaining gates

This pass tried the label-matching `e07b67c` firmware sources. Gitiles header fetch
returned HTTP 503 even outside the sandbox; the coreboot GitHub mirror URL for
fpsensor_state.cc returned 404. No new firmware conclusions or reset behavior
were established. /tmp source caches from before the last reboot were absent.
SECURITY-SOURCES.md records the previously reviewed file hashes and limitations.
Do not rely on /tmp surviving reboot.

The preparation success is only seed/context setup. It does not prove device
mode stopped, event queues drained, template persistence, matching, account
lifecycle hooks, or fprintd authorization. Releasing its process/store lock
leaves the device seed set; fresh-process recovery is deliberately still blocked.
The generic supervisor cannot force an uninterruptible kernel call to terminate.

All repository contents were already untracked at the start and remain local;
no commit, push, PR, or installation was performed. Preserve the working tree.

## Final validation before pause

- `make test`: passed (exit 0), including account preparation, all existing
  storage/protocol/workflow tests, and all non-executing CLI tests.
- Full ASan/UBSan suite: passed (exit 0) with
  `ASAN_OPTIONS=detect_leaks=0 make BUILD=build-prepare-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test`.
  Leak detection remains disabled for the previously documented local limitation.
- No tests or hardware workers remain running from this pass.
- No static-analysis run was performed on the latest preparation changes;
  prior static-analysis results apply only to their documented earlier changes.

Paused here at the user's request. Ready for the user to reboot.

## Resume checkpoint — 2026-09-28

Ran the existing privileged read-only diagnostic after the user asked to resume.
It reported FP_INFO v3, model 543, six formats, template format 4, zero loaded
templates, zero fault flags, and seed_set=false. The host service query reported
fprintd inactive. No seed restoration, provisioning, capture, or enrollment ran.
The original private experiment records remain untouched.

Added integrated preparation/supervisor coverage to tests/test_prepare.c. At
every exchange in the successful fake-device sequence, a worker opens the
existing synthetic store and independently opens/locks a fake device file,
checks that competing opens cannot acquire either lock, then hangs inside the
transport. The real supervisor must return timeout without publishing success;
the test verifies worker exit, reacquisition of both locks, and a blocked session
on reopening the store. Worker-exit polling is bounded and explicitly cleans up
a surviving worker on failure. This covers interruptible userspace hangs, not
uninterruptible kernel calls, device mode/event cleanup, or the privileged CLI’s
NSS/path/device validation. Full CLI integration remains open.

Validation: make test passed. The changed preparation test also passed under
ASan/UBSan with leak detection disabled as previously documented. A temporary
supervisor mutation omitting SIGKILL failed the worker-exit assertion as expected;
the test killed/reaped that worker. No production behavior change was needed.

Next: review controller restart/recovery and mode/event cleanup against pinned
sources, then design the explicit provisioning/account-registration CLI and live
test scope. Keep fresh-process already-set-seed recovery blocked pending evidence.

## Continued checkpoint — event transport (2026-09-28)

User asked to continue. Label-matching firmware retrieval still returned 503
from Gitiles and 404 from the coreboot mirror. Pinned and reviewed upstream Linux
chardev source instead, with interface cross-checks against installed headers;
see SECURITY-SOURCES.md for exact revision, hashes, and applicability limits.

Added `include/cros_fp_events.h`, `src/events.c`, `tests/test_events.c`, and
`docs/EVENTS.md`. Make now builds/runs the new offline test. The API validates
nonblocking descriptors, explicitly subscribes to fingerprint events through the
scalar mask ioctl, and discards bounded complete events until EAGAIN. It rejects
malformed records and invalidates the handle on failures. It does not return
match data, send EC commands, or claim that an empty queue proves fresh events.
The component remains separate from the current CLIs pending cleanup integration.

Recorded intended failing tests for open and drain before implementation. Full
`make test` and full ASan/UBSan suites passed; GCC analyzer passed for events.c.
No new hardware action occurred. Existing private experiment state is untouched.

Remaining work: recover the label-matching firmware source (prior /tmp cache
is gone) and running-kernel implementation provenance; establish mode shutdown,
completion, and in-flight-event handling before joining the event primitive to
preparation/enrollment. Unknown already-set seeds remain blocked. Independent
next work can implement explicit administrator-only provisioning/account tools
using the existing store API, with an offline test root and no implicit reader
access. A real provisioning experiment still needs a concrete reviewed scope.

## Continued checkpoint — administrative CLI (2026-09-28)

User asked to continue. Implemented `build/cros-fingerprint-admin` with explicit
`--provision`, `--register UID NAME`, `--check`, `--check-account UID NAME`, and
non-executing `--plan`. New files: `include/cros_fp_admin.h`, `src/admin.c`,
`src/admin_main.c`, `tests/admin_driver.c`, `tests/test_admin_cli.py`, and
`docs/ADMIN.md`. Make builds both the production executable and a separate
offline test entry point and runs the CLI tests.

Production configuration fixes owner 0, root anchor `/`, and the existing
prototype state path. It checks current OS account name/UID before account
operations, suppresses dumps, and supervises all lookup/filesystem work. No test
configuration is exposed by the production CLI or environment. Creation requires
a missing leaf; ordinary operations never generate state. The same store locks
serialize it with preparation. Only allowlisted action/status output is printed.
No hardware transport is linked in. Retirement is deliberately not exposed as
a CLI operation until it can coordinate device cleanup.

Tests first failed against the CLI stub and then passed. Full normal and
ASan/UBSan suites passed, as did GCC analyzer for the new production sources.
The synthetic CLI tests include concurrency, interrupted writes and pending
preservation, lock release, lookup timeouts, account checks, and corrupt/unsafe
state. TESTING.md records commands and scope.

No production prototype state was created or registered, and no reader command
ran in this increment. Private experiment records remain untouched. ADMIN.md
contains a concrete first live storage-only experiment for later authorization;
do not silently run it or follow it with preparation. All work remains local
and uncommitted.

Next useful development: recover the matching firmware reference and establish
mode shutdown/event completion, then implement a continuous owned enrollment
session. A preparation command that exits with the seed set cannot hand seed
identity to a fresh process. The administrative tool removes the host-storage
setup gap but does not resolve this recovery gate. The exact production NSS/root
path and the live storage experiment also remain to validate.

## Ongoing recovery/reuse checkpoint — 2026-09-28

User requested continued work, pausing only when input is needed, then asked
whether existing solutions change the build decision and whether they are secure.
Answered that no candidate or our own prototype has established secure login;
confirmed the former maintainer's AUR poisoning warning and distinguished it
from a proven driver exploit. REUSE-REVIEW.md records the refreshed comparison.
The current libfprint branch and upstream rust-fp match the prior pins. The Redrix
Rust fork uses a newer crosec dependency that still requires FP_INFO v1.

Firmware retrieval succeeded for state, command header, sensor task,
authentication helpers, and generic reset handler. Prior state/header hashes
match exactly. Full commit and matching board configuration remain unresolved.
References are now retained in ignored `.reference-cache/` rather than solely
/tmp; exact hashes are in SECURITY-SOURCES.md and the cache manifest.
Mode writes acknowledge configuration only, not task completion. Context reset
retains the seed and does not clear the aggregate event word. Authenticated modes
are gated by established-session state; do not use failed auth/session commands
as seed-clearing hacks. Controller-only reset is still not approved/implemented.

Added src/mode.c and tests/test_mode.c. The read-only allowlist permits only the
exact FP_MODE v0 DONT_CHANGE payload, after capability negotiation. Preparation
now refuses nonzero configured mode before seed and after context. Added
--mode-status to the diagnostic without changing default metadata JSON. Focused
red/green tests, full normal and sanitizer suites, and analyzer checks passed.
Privileged read-only mode query returned mode=0/configured_idle=true. No reader
state-changing command ran.

An isolated pinned libfprint build is in progress. Approved official build
dependencies were installed (versions in REUSE-REVIEW.md). Source is cached under
.reference-cache/libfprint-checkout; build is build-libfprint-reference. A saved
build-only patch fixes dictionary iteration and crfpmoc's missing udev helper.
Configuration succeeded. No candidate library or driver has been installed into
the system, and no candidate hardware operation has run. Transfer debugging in
the candidate can dump seed/context requests; this needs fixing before any use
with private records. No new prototype provisioning occurred.

Isolated reference build completed: the saved Meson patch allows crfpmoc-only
configuration with UDEV support. `meson compile -C build-libfprint-reference -j 2`
passed. Its three generic unit-test suites passed; crfpmoc replay/hardware tests
were not run. The full tarball has LGPL COPYING and source copyright notices.
No candidate examples were executed or libraries installed. README/source
security concerns remain; compilation is not approval for live use.

Prepared PROTOTYPE-LIVE-PLAN.md for the next actual integration experiment:
independent prototype store, current-account registration, one supervised
seed/context preparation, and read-only postflight. It requires explicit user
scope approval because it generates a new persistent identity and changes reader
state. It is NOT yet authorized or executed. A successful run leaves the reader
seeded; fresh-process recovery still blocks, and later reboot/reset needs its
own scope. Next response should handle the user's approval/constraints, not
silently run provisioning. Original experiment records remain untouched.


## Authorized prototype setup — executed

The user approved PROTOTYPE-LIVE-PLAN.md, interrupted the initial preflight, then
asked to continue. Fresh checks passed with unset seed, mode 0, no templates or
faults, inactive fprintd, and no reader holder reported by privileged fuser.

The real prototype store was created, current account registered and checked,
and the supervised prepare command ran exactly once successfully. Preparation
and an independent metadata diagnostic reported seed_set=true, zero templates,
and zero fault flags. Admin --check passed. The successful preparation itself
includes an idle-mode postflight. These are actual live results, superseding
older statements that the prototype directory has not been created.

The separate final --mode-status process waited for desktop PolicyKit
authentication. Only pkexec PID 43043 was present, with no diagnostic worker.
Before the user's planned reboot, this request was cancelled with SIGTERM.
It returned no mode result. Preparation's internal mode postflight did succeed.

Both /var/lib/cros-fingerprint-prototype and /var/lib/cros-fingerprint-experiment
contain real private records to retain; neither belongs in Git. The reader is
seeded now. Fresh-process preparation must still refuse unknown already-set
seed identity. No enrollment/capture, reset/reboot, or login changes ran. See
PROTOTYPE-LIVE-PLAN.md for the complete scope/results and remaining limits.

## Reboot checkpoint — 2026-09-28

User requested a good stopping point because they need to reboot. Work is paused
at this checkpoint; no reboot was initiated by the agent. All code, tests, patches,
and notes are saved locally in this checkout, still uncommitted/untracked. The
ignored reference checkout and build remain on disk as well.

Completed a focused candidate fix: saved
`patches/libfprint-4df05918-command-logging.patch` removes raw request dumps for
all commands. `tests/test_reference_logging.py` failed on the original logger,
then passed on the patched logger. The candidate rebuilt and all three generic
unit suites passed. See REUSE-REVIEW.md for scope and invocation. No candidate
hardware operations or system library installation occurred. FP_INFO adaptation
was inspected but not begun; four request sites still hardcode v1.

On resume after reboot:

1. Read this checkpoint, AGENTS.md, and PROTOTYPE-LIVE-PLAN.md. Retain both real
   private stores. Do not rerun provisioning or registration.
2. Observe the reader with read-only metadata and mode diagnostics and check
   the existing prototype store. A previous host reboot cleared the reader seed;
   verify current state rather than assuming this reboot did the same.
3. Continue offline compatibility/recovery work. The previous authorization was
   one preparation run and is consumed; any further live preparation needs its
   own scoped authorization. Do not reuse the legacy experiment identity for
   the prototype, and never regenerate records to bypass an already-set seed.

No enrollment, capture, matching, or login integration has occurred. The prototype
store contains the most recently loaded identity; the older experiment store is
separate. Reboot does not require deleting either store.

## Latest resume — 2026-09-28

User asked to pick up the project. Read-only metadata reports FP_INFO v3,
model 543, six formats, template format 4, seed unset, zero loaded templates,
and zero fault flags. The existing prototype admin --check returned ok.
Host fprintd is inactive. Neither private store was replaced or reprovisioned;
no preparation, capture, enrollment, or system configuration change ran.
The separate --mode-status invocation remained at PolicyKit authentication with
only pkexec present and was cancelled with SIGTERM. No current mode result was
obtained; do not substitute the previous session's mode observation.

Added `tests/test_reference_replies.py` and
`patches/libfprint-4df05918-reply-length.patch` for the isolated libfprint candidate.
The actual ioctl helper now rejects short/oversized successful replies before
copying output. Current callers require exact lengths, including requested frame
chunks and empty responses. A future variable-sized FP_INFO response needs an
explicit actual-length API and bounded parser. The focused test first failed on
the old helper, then passed normally and under ASan/UBSan; logging regression,
candidate rebuild, three generic libfprint suites, and standalone make test pass.
See REUSE-REVIEW.md and TESTING.md. The patch is applied in the reference checkout
after the earlier build/logging patches and saved outside the ignored cache.

Next development: negotiate FP_INFO versions and decode v2/v3 in the candidate,
with tests covering all four callers and malformed responses. Recovery, mode/event
completion, recorder output and private-memory clearing still require review.
Further real preparation needs a newly scoped authorization; the prior single
run authorization remains consumed. No candidate library has been installed.

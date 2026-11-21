# Development plan

Created 2026-09-27. Status: milestone 1 complete. The read-only C diagnostic
passes offline tests and reproduced the recorded target-reader metadata.
Milestone 2 now has a documented prototype security design and acceptance
scenarios; implementation and recovery validation remain open.

See DECISIONS.md for the source comparison, architecture, reference-pinning
limits, and license; TESTING.md records the observed red/green runs.

## Objective

Provide a maintainable userspace implementation for the target ChromeOS
fingerprint controller, with a path to libfprint/fprintd integration. The first
deliverable is a tested read-only diagnostic. Login is a later milestone, not
the initial acceptance criterion.

## What is established

- Linux driver binding and the `/dev/cros_fp` interface work on the target.
- The controller responds to protocol capability and status queries.
- Existing crfpmoc and rust-fp implementations are relevant prior art.
- The inspected crfpmoc fork hardcodes an older sensor-info command version
  than this device supports; its key/context policy also needs review.
- We have not captured, enrolled, matched, saved, or restored a fingerprint.

See HARDWARE.md for the measured values and SOURCES.md for provenance.

## Provisional architecture

    Diagnostic CLI / libfprint adapter
                  |
    Enrollment and verification state machines
                  |
    Version negotiation and protocol encoding/decoding
                  |
          Transport interface
             /          \
    Scripted test fake   Linux ioctl + event transport
                              |
                         /dev/cros_fp

Secret provisioning and per-user context ownership are a separate design
concern. Do not force them into the diagram above until the integration review
identifies the correct owner and lifecycle.

Use C11 with explicit-length buffers and an injected transport. Keep the
protocol layer separate enough to exercise without fprintd, PAM, or a
system-wide install. The initial build uses Make and a standalone C test
harness; revisit Meson/GLib when implementing the actual libfprint adapter.

## Milestones and exit criteria

### 0. Establish the implementation baseline

1. Pin the ChromeOS protocol reference and candidate prior-art revisions.
2. Compare crfpmoc and rust-fp for supported commands, template format 4,
   asynchronous user-context setup, event handling, and key persistence.
3. Inspect the current libfprint device-discovery and driver interfaces. Determine
   whether a library-only design can support the required security lifecycle.
4. Record the architecture and license decisions before importing code.
5. Establish a minimal reproducible build, test command, and offline fixture
   layout. CI should run the same unprivileged tests.

Exit: a documented choice of implementation base, dependencies, license, and
transport interface. Existing code may reduce scope; we are not committed to
rewriting everything.

### 1. Read-only protocol diagnostic — first TDD increment

Suggested first red/green slice:

1. Write a test supplying sensor-info version mask `0b1100`.
2. Assert the client selects supported version 3 and never requests version 1.
3. Run the test and observe the expected failure.
4. Implement negotiation and rerun it successfully.
5. Add version-2-only and unsupported-version cases before extending behavior.

Continue with tests for:

- Encoding command-version queries with a 16-bit command ID.
- Decoding version-2 and version-3 sensor information and template metadata.
- Enforcing response-length and advertised capture-format-count bounds; unused
  or zero-filled buffer space must not become additional reported formats.
- Distinguishing transport failure, controller error, and malformed payload.
- Interpreting `0x03ff` as unknown dead-pixel count, without inventing a sensor
  failure; decoding the actual initialization/communication fault bits.
- Encryption-status flags, including unknown flags and unsupported fields.
- Permission denied, absent device, unsupported command, and bounded execution.
- Rejecting state-changing and biometric-data commands in diagnostic mode.
- A stable, whitelisted JSON output containing no secrets or biometric data.

Exit: all tests pass offline, and one explicitly run hardware diagnostic
reproduces the relevant recorded facts. No seed setup or capture takes place.

### 2. Security and lifecycle design

Resolve docs/SECURITY-DESIGN.md before enrollment on real hardware:

- Ownership, generation, storage, and recovery of the seed.
- Stable and distinct user contexts, and how users are authorized.
- Template storage, format validation, deletion, and concurrency.
- Behavior after firmware reset, suspend, process crash, or lost secret.
- The division of responsibilities between driver, fprintd, and any helper.

Exit: a reviewed design with a threat model and tests for isolation and failure
behavior. A hard-coded shared seed/context is not an acceptable production
shortcut. TPM use is a design option to investigate, not an assumed requirement
or a promise of ChromeOS-equivalent protection.

### 3. Standalone enrollment and matching

Use scripted transport tests to drive the state machines before live tests:

- Finger down/up, partial scans, progress, completion, retry, and timeout.
- Cancellation and disconnect at every meaningful state.
- Correct context initialization and asynchronous completion handling.
- Bounded template transfers and format/version validation.
- Matching the enrolled finger; rejecting other fingers.
- Deletion and cleanup without accidentally removing another user's data.

Exit: one explicitly authorized test enrollment can be verified and deleted
using standalone tools. No system login changes. Any biometric artifacts stay
outside the checkout and are not included in logs.

### 4. Persistence and robustness

Test program restart, controller restart where justified, suspend/resume, and
full machine reboot as separate cases. Verify recovery of the same identity
and templates, rejection of wrong/corrupt contexts and templates, and safe
failure when secrets are unavailable. Test concurrent access and stale events.

Exit: repeatable verification across these transitions and a documented cleanup
and recovery procedure. A successful match in one process is insufficient.

### 5. libfprint and Arch integration

Integrate with standard libfprint enrollment, verification, and cancellation
interfaces. First test from the build tree or an isolated service configuration;
do not replace system libraries to run unit tests. Produce a reviewed Arch
package from pinned source, with explicit dependencies and rollback instructions.

Exit: standard tools work reliably and package install/remove behavior is
reviewable. Build recipes never elevate privileges during compilation.

### 6. Omarchy authentication

Identify the actual login/lock components and their fingerprint support. Scope
PAM changes narrowly, retain password fallback, and test authentication failure,
timeout, and service unavailability as well as success. Disk unlocking at boot
is a separate feature and is outside this plan.

Exit: explicitly authorized screen-unlock/login integration with documented
rollback and the preceding reliability criteria met.

## TDD workflow

For each small behavior: specify it with a failing test, run that test, implement
the minimum, run the relevant suite, and refactor. Record actual commands and
outcomes. A compile error or unrelated environment failure is not the intended
red state. Hardware observations inform new regression tests but do not replace
automated tests.

Use synthetic responses derived from the documented protocol. Keep raw buffers,
secrets, and real templates out of test fixtures. Hardware tests remain opt-in
and distinguish read-only operations from actions that alter reader state.

## Immediate next work

Resumed after the user-requested reboot on 2026-09-28. The read-only check
confirmed an unset seed, zero loaded templates, and zero fault flags. Integrated
preparation/supervisor hanging-transport and lock-release tests now pass. Read
HANDOFF.md for validation, remaining CLI coverage, and next recovery-review steps.
A bounded Linux event subscription/drain primitive now has offline tests
(EVENTS.md). It is not integrated into hardware workflows; firmware quiescence
and event freshness remain unproven. Label-matching firmware state, command header, sensor task, and authentication
source files have now been recovered and content-pinned. Mode responses only
reflect configuration; controller restart/mode cleanup review remains incomplete.
Read-only mode querying and idle preparation pre/postflight gates now pass tests;
a live read-only query returned configured mode 0. Reuse/security reassessment
is recorded in REUSE-REVIEW.md; a complete new integration stack is not assumed.
Explicit prototype storage administration now has an offline-tested CLI for
provisioning, registration, and checks (ADMIN.md). The explicitly authorized first live
prototype store/account setup and seed/context preparation have now succeeded
(PROTOTYPE-LIVE-PLAN.md). The reader is seeded; retain both private state stores.

The offline seed-store component now covers the storage subset of S01–S09;
see SEED-STORE.md for scope and untested boundaries. Secure state-directory setup, a seed-session recovery policy, and fake-tested
seed restoration/context sequencing are now implemented (STATE-LIFECYCLE.md).
A supervised seed/context experiment executor is also built and tested offline.
The seed/context hardware experiment in SEED-CONTEXT-EXPERIMENT.md succeeded
with explicit authorization on 2026-09-28. That experiment initialized the reader; real
private records must be retained for recovery. The latest post-reboot diagnostic
in HANDOFF.md reports an unset seed. A normal host reboot cleared the seed, and restoration from the same saved
seed/context records succeeded on 2026-09-28; a fresh diagnostic confirmed it.
The fresh-process already-set-seed refusal is now verified on hardware. The
normal-host-reboot seed/context recovery check is complete for one observed run.
Suspend and power-loss recovery remain unverified. No reboot is automated. Account/reader gallery binding validation now has an offline implementation
and tests (STATE-LIFECYCLE.md). Persistent account registration/retirement now has an offline implementation
and tests (ACCOUNT-REGISTRY.md). A provisioned host store now binds the seed and registry generation and
invalidates host sessions on retirement (PROVISIONED-STORE.md). The supervised
hardware executor, OS account lifecycle hooks, and trusted adapter still need
integration before enrollment. SECURITY-DESIGN.md chooses explicit provisioning, restricted
local storage for the prototype, and separate random user contexts, with
fprintd as the intended authorization/storage owner. SECURITY-SOURCES.md
records the pinned fprintd review and firmware sources matching the device label.

Resolve already-set-seed recovery, account incarnation tracking, and isolated
service integration before marking the security milestone complete. The seed
status bit cannot prove that the controller holds our saved secret. No real
enrollment or seed provisioning follows automatically from this design.

The Linux adapter and one-shot deadline supervisor are implemented. A live
run of the C JSON CLI on 2026-09-27 selected FP_INFO v3 and reproduced six
formats, model 543, template format 4, no loaded templates, and an unset seed.
The supervisor bounds the CLI's wait, not the lifetime of an uninterruptible
kernel operation; a persistent service would need a different cleanup policy.

No reliable schedule estimate is available yet. Estimate the next milestone
after reviewing the existing implementations rather than estimating a complete
driver rewrite up front.

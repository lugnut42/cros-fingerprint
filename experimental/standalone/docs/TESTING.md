# Testing and TDD record

## Reference reply-length regression — 2026-09-28 resume

`tests/test_reference_replies.py` takes the explicit pinned transfer source path
documented in REUSE-REVIEW.md. It requires GLib/GIO development files and is kept
separate from the standalone offline suite. The original helper failed on an
unexpected one-byte reply to a zero-response request; the patched helper passes
the full synthetic short/exact/oversized/error matrix with output canaries.
The focused test also passed with
`ASAN_OPTIONS=detect_leaks=0 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.
The logging regression, isolated Meson rebuild, all three generic unit suites,
and standalone `make test` passed. No candidate hardware testing was performed.

## Run locally

Requirements: Linux, a C11 compiler, Make, Python 3 for CLI/JSON tests, and OpenSSL headers/libcrypto for seed-store tests. No root, network, GLib, device, or service
is required.

```sh
make test
```

Run an individual test after building:

```sh
./build/test-protocol rejects_bad_lengths
```

Build separately with memory/undefined-behavior instrumentation:

```sh
make BUILD=build-sanitize \
  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined' test
```

The development sandbox prevents LeakSanitizer from operating. In that
environment only, run the sanitizer command with `ASAN_OPTIONS=detect_leaks=0`.
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled; this is not a
successful leak check. CI retains normal sanitizer defaults.

Build output is ignored under `build/` and `build-sanitize/`. `make clean`
removes these two directories only. GitHub Actions is configured but has not
run until the repository is published and the workflow executes.

## Negotiation: eight tests

- Prefer FP_INFO v3 when the device advertises v2 and v3.
- Select v2 on a v2-only device.
- Reject empty, legacy-only, and unknown-only version sets without changing
  the output parameter.
- Ignore unknown future versions when a supported version is available.
- Reject truncated and oversized capability responses.
- Distinguish controller failure from response-format failure.
- Propagate transport failure without retrying or consuming response data.
- Reject invalid arguments before touching the transport.

The fake asserts that every exchange is GET_CMD_VERSIONS **v1**, that the target
is encoded as a little-endian **16-bit** FP_INFO ID, and that the response
capacity is four bytes. No FP_MODE, reset, enrollment, seed, or template command
reaches the fake transport. Boundary tests deliberately attempt forbidden
commands and verify their rejection.

## Observed red/green sequence (2026-09-27)

1. Added a compiling stub and the v2/v3 selection test. `make test` failed at
   the assertion expecting CFP_OK, as intended (not a compiler failure).
2. Implemented the capability query and v3 selection. `make test` passed one
   test.
3. Added fallback, invalid-version, malformed-response, error, and argument
   regression cases. `make test` failed on v2 selection. Individual runs of
   `rejects_bad_lengths` and `reports_controller_failure` also failed at their
   intended status assertions.
4. Added v2 selection and validation/error handling. `make test` passed all
   eight tests.
5. The first sanitizer invocation could not complete because LeakSanitizer is
   incompatible with the sandbox's tracing restrictions. Rerun with leak
   detection disabled to check addresses and undefined behavior separately.
   The rerun passed all eight tests with no ASan/UBSan findings; leak detection
   remains unverified locally.

## Decoder and diagnostic-boundary increment (2026-09-27)

Added two further executables: `test-decode` and `test-readonly`. All three run
under `make test` and the same sanitizer target.

- Synthetic v2/v3 fixtures exercise all metadata fields, six advertised formats,
  trailing padding, and v3 image offsets. Every length below the required
  response size is rejected without changing the output structure.
- Zero formats, excessive advertised counts, inconsistent template counts,
  unsupported versions, and invalid arguments are covered.
- Error decoding distinguishes unknown dead-pixel counts from fault flags and
  retains unknown raw bits. Encryption parsing preserves unknown flags and
  distinguishes unavailable seed status from an unset seed.
- The diagnostic boundary permits only GET_CMD_VERSIONS v1, FP_INFO v2/v3,
  and FP_ENC_STATUS v0 with their expected request lengths. A capability query
  targeting FP_SEED is allowed without issuing FP_SEED. A sweep of all 65,536
  command IDs at version zero permits only FP_ENC_STATUS; additional cases
  check permitted versions and reject malformed requests before dispatch.

Observed red/green sequence: compiling stubs failed the intended assertions
in `./build/test-decode info`, `./build/test-decode encryption`, and
`./build/test-readonly`. Implemented decoding and allowlisting, and routed
negotiation through that boundary. `make test` then passed all three
executables. ASan/UBSan also passed with `ASAN_OPTIONS=detect_leaks=0`; the
existing local leak-check limitation still applies.

The decoder uses a fixed application resource bound of 32 capture formats;
this is not a claimed firmware limit. Larger counts return CFP_LIMIT_EXCEEDED.
V2 image offsets are explicitly unavailable. Raw status bits are retained for
future interpretation. Metadata decoding does not validate or access images.

No hardware commands were run for that decoder increment.


## Linux transport, supervisor, and JSON CLI (2026-09-27)

Added `test-linux`, `test-diagnostic`, and a JSON fixture executable consumed
by `tests/test_cli.py`. The default suite remains offline and unprivileged.
It uses `/dev/null` to test ioctl rejection, not a fingerprint reader.

Observed TDD sequence:

1. The Linux adapter stub failed its first expected successful exchange.
   Implemented host-ABI framing, valid byte counts, bounded allocation,
   controller-result preservation, and errno reporting. The tests passed.
2. Collector and supervisor stubs failed their expected-success assertions
   independently. Implemented the four-query sequence and a fork/pipe/poll
   supervisor using a monotonic deadline. Tests passed, including a hung worker
   with an 80 ms deadline (under one second allowed by the test) and early exit.
3. JSON tests first failed because the serializer emitted no JSON. After
   implementing it, the CLI test failed for missing device-error reporting.
   Implemented device open/validation and error output. All checks passed.
4. `make test` and the full ASan/UBSan suite passed, with local leak detection
   disabled for the previously documented sandbox restriction.
5. Ran the explicitly privileged read-only CLI against the target; exit 0,
   matching metadata. See HARDWARE.md. GitHub CI has not run yet.

Tests cover short and oversized ioctl replies, unchanged outputs on adapter
failure, controller errors, simulated EACCES/ENOENT/EIO/EINTR/ETIMEDOUT with no
retry, capability-query order, stopping on controller errors, timeout and
worker crash, valid JSON with null unknown fields, missing paths, non-character
devices, ioctl rejection, and usage errors. Real permission denial and a real
hung kernel ioctl were not induced. No capture/enrollment behavior is tested.

The supervisor is intentionally limited to a one-shot CLI. After a timeout it
sends SIGKILL and performs only a nonblocking reap, then the CLI exits. A worker
in uninterruptible kernel sleep can remain until the kernel operation ends;
its process is reparented on CLI exit. This design must not be reused as a
persistent-service lifecycle without additional child management.


## Offline seed store (2026-09-27)

A compiling stub failed intended assertions in `test-seed basic`, `faults`,
`unsafe`, and `concurrent`. Implemented creation/loading with independent
flock descriptions, strict metadata checks, checksum validation, durable
no-replace publication, and unchanged outputs on failure. These checks passed.
Added regression coverage for every record-byte corruption and truncated
length, trailing bytes, hard links, FIFOs, interrupted staging, and reloading
a published record after directory-sync failure.

`make test` and the full ASan/UBSan build passed. Leak detection remains disabled
locally for the documented sandbox restriction. GitHub CI has not run. The
seed test uses deterministic synthetic data in temporary directories and
cleans successful runs. It never invokes the production RNG or a device.
OpenSSL libcrypto is required only for this component, not the diagnostic.
Acceptance gaps are listed in SECURITY-TESTS.md and SEED-STORE.md.

## State paths, recovery policy, and context sequencing (2026-09-27)

`test-state directories` and `test-state recovery` first failed their intended
assertions against compiling stubs. Implemented descriptor-relative traversal,
ancestor/leaf ownership and mode checks, explicit leaf creation, and seed-session
generations. Tests cover symlinks at intermediate/leaf positions, traversal and
empty components, unsafe ancestors/leaves, wrong owner, absent paths, no
implicit creation, already-set/unknown seeds, stale completion, and generation
exhaustion. Both then passed normally and with ASan/UBSan.

`test-context sequence` and `failures` first failed at context initialization.
Implemented asynchronous v1 sequencing with stable IDs, 100 ms poll spacing,
bounded dispatch deadline, controller/transport errors, cancellation, and
clearing retained IDs. Fake transport verifies both command framing and user ID
on every call. A separate failing regression showed invalidation during an
exchange could return a stale completion; post-exchange generation checks fix it.

`test-restore` first failed at the expected missing-storage status. Implemented
load-only restoration: durable seed load, capability checks, initial seed status,
one seed command, and post-command status verification. Tests cover missing
storage with zero device calls, unsupported commands, unknown/already-set seed,
controller/transport errors without retries, and failed status confirmation.
A second red regression injected invalidation during the seed command; stale
completion is now rejected without reviving the session.

These are offline components, not linked into the diagnostic. Their production
callers must own and supervise the device. No state-changing hardware command
has been run. The monotonic time passed to context ticks limits dispatch; a
future executor must also check completion time and supervise blocked I/O.


## Supervised seed/context experiment preparation (2026-09-27)

Added distinct context-ID records after a focused test failed at creation;
refactored the tested seed storage implementation to share record mechanics
while retaining distinct names and magic values. Tests verify round-trip and
cross-type rejection. No production RNG was invoked by the tests.

`test-smoke` first failed at its existing-template refusal assertion. Implemented
the complete synthetic preflight, durable records, restore, context-switch, and
postflight sequence. It passes with unsupported/missing/already-set cases and a
late successful reply that must still produce timeout. The experiment CLI test
first failed on --plan, then passed after implementation. It exercises only
non-executing arguments. No root-required action runs under `make test`.

The final normal and ASan/UBSan suites passed, including all new executables.
GCC -fanalyzer compiled the complete experiment without diagnostics. Local
leak checking remains unavailable. The built experiment has NOT been run against
the reader; SEED-CONTEXT-EXPERIMENT.md records the proposed scope.


## Authorized hardware seed/context validation (2026-09-28)

The user authorized the prepared experiment. --initialize completed with exit 0;
a separate diagnostic confirmed seed_set=true and zero templates. Metadata-only
inspection confirmed root ownership, directory modes 0700, record modes 0600,
and 72-byte record sizes. A fresh --restore invocation refused the already-set
seed with exit 1 / CFP_RECOVERY_REQUIRED before mutation. No biometric data was
captured and no secret contents were printed. Details and the proposed reboot
recovery check are in SEED-CONTEXT-EXPERIMENT.md. No code changed in this pass,
so the previously passing offline tests were not redundantly rerun.


## Gallery binding red/green record (2026-09-28)

Added the binding API and synthetic gallery tests with an initial accept-all
stub. `make build/test-binding && ./build/test-binding` compiled and failed at
line 27, the empty-gallery rejection assertion (exit 1). Implemented complete
metadata validation; `make test` then passed, including `test-binding` and all
existing suites. The focused ASan/UBSan build and run also passed:

```sh
make BUILD=build-binding-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' build-binding-sanitize/test-binding
ASAN_OPTIONS=detect_leaks=0 ./build-binding-sanitize/test-binding
```

Leak detection remains disabled for the previously documented local limitation.
No real secrets, templates, device commands, or system configuration were used
in this development increment. Hardware and service integration remain untested
for this new API. See STATE-LIFECYCLE.md for its caller obligations and limits.


## Persistent registry red/green record (2026-09-28)

Added a compilable registry stub returning missing and synthetic lifecycle/fault
tests. `make build/test-registry && ./build/test-registry` failed at line 71,
initial registry creation expected to succeed (exit 1). Implemented the locked,
checksummed snapshot store and registration/lookup/retirement lifecycle; the
same command passed. Expanded regression coverage to malformed/unsafe state,
retirement write failures, tombstone collisions/exhaustion, and concurrency.

`make test` passed all suites. Focused ASan/UBSan also passed:

```sh
make BUILD=build-registry-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' build-registry-sanitize/test-registry
ASAN_OPTIONS=detect_leaks=0 ./build-registry-sanitize/test-registry
```

`cc -Iinclude -O0 -std=c11 -Wall -Wextra -Wpedantic -Werror -fanalyzer -c src/registry.c -o /tmp/cfp-registry-analysis.o`
completed without diagnostics. Leak detection remains disabled for the existing
local limitation. No device commands, real identities/biometrics, or system
configuration changes occurred. ACCOUNT-REGISTRY.md documents unverified
hardware/service behavior and remaining lifecycle requirements.

## Provisioned store and retirement red/green record (2026-09-28)

`test-provision` first compiled with missing-return stubs and failed at line 29,
expected successful creation. Generalized the existing record mechanics to a
separate 64-byte provisioning payload; the new test and original seed suite
then passed. `test-registry` failed at line 73 for the new CHECK action before
implementation, then passed with whole-registry/generation validation.

`test-store` first failed at line 47 for explicit store initialization against
its missing-return stub. Implemented lifetime root locking, joint generation
validation, no-regeneration open, and retirement invalidation. Expanded tests
cover swapping provisioning directories, missing provisioning state, incomplete
creation, and failed retirement. All passed.

A separate regression in `test-context` failed at line 51: a completed context
operation returned cached success after its seed session was invalidated.
Moved completed-result return after the session-generation check. The regression
then passed without another transport call.

Final `make test` passed all suites. The full ASan/UBSan suite also passed:

```sh
ASAN_OPTIONS=detect_leaks=0 make BUILD=build-lifecycle-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test
```

GCC `-fanalyzer` compilation of `src/store.c` and `src/seed.c` completed without
diagnostics, with C11 and all normal warning flags plus `-Werror`. Local leak
detection remains disabled. All record bytes/RNG outputs were synthetic. No
hardware access or installation occurred. See PROVISIONED-STORE.md for the
remaining device cleanup, authorization, and executor integration requirements.

## Account preparation checkpoint (2026-09-28, paused for reboot)

Red/green evidence: `test-prepare` compiled with a stub and failed at line 67
(expected missing-account storage error), then passed after implementation.
Fault injection now tests transport failure, late completion, cancellation,
and oversized response at every exchange in the successful sequence. It also
covers existing seeds/templates, wrong target, missing context support, retired
accounts, and bounded BUSY polling without seed-write retries.

`test-restore` failed at line 55 after adding cancellation during preflight;
the fixed path checks generation before beginning seed restoration and passes.
`test-linux` failed at line 14 when its wrapped free inspected nonzero ioctl
packet bytes. Clearing the complete allocation on every free path made it pass.
`test_prepare_cli.py` failed on --plan against its initial exit-2 stub, then
passed with the supervised administrator CLI. These CLI tests never execute
--prepare with valid hardware arguments.

At the user's pause request, `make test` and the full ASan/UBSan suite under
`build-prepare-sanitize` both exited 0. Sanitizer flags were
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` with matching linker
flags and `ASAN_OPTIONS=detect_leaks=0`. No new hardware commands or private-state
provisioning ran. Remaining integrated-supervisor/live-test work is in HANDOFF.md.

## Post-reboot supervisor coverage (2026-09-28)

Added integrated hanging-transport coverage at every preparation exchange in
`test-prepare`, using the real supervisor, synthetic account store, and separately
opened fake-device locks. The worker verifies contention before hanging; the
parent verifies timeout, unchanged report, worker exit, lock reacquisition, and
blocked recovery state. No production behavior changed. A temporary mutation
omitting supervisor SIGKILL failed at `CHECK(exited)` as intended, and the test
cleaned up the surviving worker. The unchanged supervisor passed.

`make test` passed. The changed test passed with ASan/UBSan via the existing
`build-prepare-sanitize/test-prepare` target, using the flags above and
`ASAN_OPTIONS=detect_leaks=0`. These tests do not run the privileged CLI worker,
access real hardware, or prove cleanup of an uninterruptible kernel operation.
The separately authorized read-only post-reboot observation is in HANDOFF.md.

## Linux event subscription/drain (2026-09-28)

`make build/test-events && ./build/test-events` first compiled and failed at
line 37 against the open stub (expected subscription success). After implementing
open it failed at line 42 against the drain stub (expected empty-queue success).
Implementing bounded draining made the tests pass. Expanded cases cover queue
limits, malformed records, unexpected types, saved errno, syscall failures,
invalid arguments, and invalidation after failure. No hardware events were used.

`make test` passed (exit 0), including the new event test and previous preparation
supervisor coverage. The full ASan/UBSan suite passed (exit 0):

```sh
ASAN_OPTIONS=detect_leaks=0 make BUILD=build-prepare-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test
```

Leak detection remains disabled for the documented environment limitation.
`cc -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror -fanalyzer -c src/events.c`
completed without diagnostics (output placed in /tmp). System syscall wrappers
and event delivery remain unverified on a real reader. No mode, reset, seed,
context, capture, or enrollment command ran during this increment.

## Administrative storage CLI (2026-09-28)

Added tests before implementation. The production and offline-driver executables
compiled against an exit-2 CLI stub; `python3 tests/test_admin_cli.py build` then
failed at the expected `--plan` success assertion (line 39). Implemented the
parser, administrator gate, dump suppression, supervised worker, store actions,
and allowlisted output. The complete offline CLI test then passed.

The test driver substitutes a trusted temporary anchor, owner, synthetic
accounts/RNG, and storage faults at build time. It invokes the same parser,
worker, supervisor, and filesystem APIs as the production executable. Production
plan/argument checks and unprivileged execution refusals are tested separately.
No successful production administrative operation was invoked.

Expanded tests passed for two concurrent provisioning processes (one success,
one refusal), timed-out account lookup, timed-out registry writes with preserved
pending evidence and released locks, malformed arguments, OS-account mismatch,
no regeneration, corrupt/missing state, duplicate registration, unsafe leaf
paths, locks, and RNG/write/sync failures. Synthetic RNG callbacks also require
core/dump suppression. Outputs are checked against an exact nonsecret allowlist.

`make test` passed (exit 0). The full ASan/UBSan suite passed (exit 0) using:

```sh
ASAN_OPTIONS=detect_leaks=0 make BUILD=build-prepare-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test
```

GCC `-fanalyzer` with normal C11 warning flags and `-Werror` compiled both
`src/admin.c` and `src/admin_main.c` without diagnostics. Leak detection remains
disabled for the existing documented limitation. Real root-path provisioning,
NSS integration, power-loss durability, and hardware use remain unverified.

## Read-only mode query and preparation gates (2026-09-28)

Red evidence: `test-mode` failed at line 25 against its stub; the new exact-query
allowlist test failed at test_readonly.c line 41 before the exception existed.
`test-prepare` failed at line 156 because an active configured mode did not block
seed programming. The CLI test failed at line 25 before --mode-status parsing.
After implementation all focused tests passed. Mode-query cases cover unsupported
versions, malformed lengths, controller/transport errors, unchanged failure
outputs, and unknown returned bits. Read-only boundary cases reject all mode
writes, extra bits, and the authenticated command version. Preparation checks
zero configured mode before seed programming and after context completion; all
existing per-exchange faults and supervised hangs include these new exchanges.

`make test` and the full `build-prepare-sanitize` ASan/UBSan suite exited 0 using
the previously recorded flags with leak detection disabled. GCC -fanalyzer
compiled mode.c and prepare.c with normal warning flags and no diagnostics.

A separately invoked privileged read-only check:
`pkexec experimental/standalone/build/cros-fingerprint-diagnostic --mode-status`
returned `{ "schema_version": 1, "mode": 0, "configured_idle": true }`.
This confirms only the configuration word; no mode was changed, and it is not
proof of sensor-task completion or stale-event cleanup. No seed/context/reset or
biometric command ran during this increment.

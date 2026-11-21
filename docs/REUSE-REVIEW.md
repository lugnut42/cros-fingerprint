# Reuse and security checkpoint — 2026-09-28

## Decision

No inspected candidate is a verified secure drop-in solution for this reader.
This does not establish that a complete new driver is necessary or safer. Prefer
adapting a reviewed libfprint driver for the eventual integration, retaining this
repository's protocol tests and explicit storage/session policy. Do not duplicate
libfprint/fprintd discovery, authorization, print storage, or PAM functionality.

The standalone code is still useful for hardware experiments and regression
tests. Its passing tests do not establish secure login. Neither the candidates
nor this project have received a complete security audit. No candidate installer,
AUR recipe, or privileged service has run during this comparison.

## Current revision check

GitHub branch/commit API queries on 2026-09-28 returned:

| Candidate | Pinned revision | Finding |
| --- | --- | --- |
| acd407/libfprint, feature/crfpmoc | `4df05918f4670f5eed684c1771d2183f2e2df279` | Same revision as the original review |
| ChocolateLoverRaj/rust-fp | `2d0b547e8800eea66d06fb52ed946f52cab30e37` | Same revision as the original review |
| acd407/rust-fp | `7036b4dbba82267da85ebb3c10e238186553564c` | Fork startup-retry changes; still fixed seed/context |

The Rust fork's Cargo.lock selects FyraLabs/crosec-rs at
`e904785fa9388c9d591370bd4d9a8121bae85ec9`. Its
[crosec/src/commands/fp_info.rs](https://github.com/FyraLabs/crosec-rs/blob/e904785fa9388c9d591370bd4d9a8121bae85ec9/crosec/src/commands/fp_info.rs)
still panics if FP_INFO v1 is absent. Our reader advertises only v2/v3, so even
this fork requires protocol adaptation. Its seed/context behavior is visible in
[rust-fp/src/drivers/cros_fp.rs](https://github.com/acd407/rust-fp/blob/7036b4dbba82267da85ebb3c10e238186553564c/rust-fp/src/drivers/cros_fp.rs).
Do not import Rust code without resolving the license limitation in DECISIONS.md.

The libfprint fork's
[crfpmoc.c](https://github.com/acd407/libfprint/blob/4df05918f4670f5eed684c1771d2183f2e2df279/libfprint/drivers/crfpmoc/crfpmoc.c)
still requests FP_INFO v1 at multiple call sites and initializes fixed seed and
context values. Its encryption-status callback branches on the entire status
being zero, rather than interpreting the valid seed bit. A nonzero status also
does not prove stored seed identity. These require review and tests before use.
The existing enrollment, transfer, cancellation, and libfprint callbacks are
reuse candidates, not security-approved components. No working local enrollment
has been demonstrated with this revision.

Official MR 512 remains inaccessible through the reviewed endpoint's bot gate;
its current merge/review status was not verified. No claim here depends on a
third-party catalogue's upstream status.

## The AUR incident is separate

The [former maintainer's report](https://www.cnblogs.com/acd407/articles/19669407)
says they abandoned `libfprint-crfpmoc-git`, that it was subsequently poisoned,
and advises against using it. This is a first-party package warning. It does
not establish malicious code in the separate GitHub driver, nor did this review
verify that the package was removed. Do not describe it as a confirmed driver
exploit or claim current package cleanup was verified. The project prohibition
on using the package or executing its recipe remains in force.

Fixed public seed/context values are a different design concern. They do not
alone prove a specific exploit or that templates are plaintext; firmware also
uses device-side key material. What is missing is a reviewed per-user ownership,
secret-lifecycle, and recovery argument. Replacing constants with random bytes
without durable ownership/recovery would not solve that problem.

## Next integration comparison

Before choosing a final adapter base, pin an accessible upstream libfprint
revision, review the fork delta and build scripts, and assess a minimal patch for
version negotiation, bound parsing, per-user contexts, seed ownership, and event
freshness. Use the existing offline fixtures. Build in isolation, then validate
one explicitly scoped hardware experiment. Do not install an unreviewed fork
into system authentication merely to see whether it works.

## Additional logging and build findings

The pinned fork's `crfpmoc-ec-transfer.c` calls a raw outgoing-buffer hex dump
when transfer debugging is enabled. The seed/context requests use that same
path. This is a concrete conditional secret-logging concern, not a claim that
ordinary logging always leaks or that the code is malicious. Remove/redact it
before sending private records through a derivative of this transport.

For an isolated build, the reviewed tarball was extracted using Python's `data`
filter under `.reference-cache/libfprint-checkout/acd407-libfprint-4df0591`.
Build outputs go to `build-libfprint-reference`; no install target is run.
After explicit tool approvals, official repository packages were installed:
meson 1.12.0-1, ninja 1.13.2-3, libgusb 0.4.9-2, glib2-devel 2.88.3-1,
and meson's python-tqdm 4.70.0-1 dependency. Existing system libfprint, PAM,
services, and udev rules were not replaced by this build.

Minimal configuration first failed because the non-introspection test fallback
iterated a dictionary with one variable. Also, crfpmoc lacked its own udev helper
declaration, so selecting only it would omit UDEV discovery support. The small
`patches/libfprint-4df05918-build.patch` fixes those two build declarations in the
local reference checkout. Configuration then succeeded with:

```sh
meson setup build-libfprint-reference .reference-cache/libfprint-checkout/acd407-libfprint-4df0591 --wrap-mode=nodownload -Ddrivers=crfpmoc -Dintrospection=false -Ddoc=false -Dudev_rules=disabled -Dudev_hwdb=disabled -Dinstalled-tests=false -Dgtk-examples=false
```

Driver replay tests are skipped in this configuration; their fixture provenance
and introspection/emulation setup have not been reviewed for use. Build or generic
unit-test success would not establish device compatibility or authentication
security. The FP_INFO and secret/context behavior are not changed by this patch.

The pinned reference tarball SHA-256 was
`607581ddf7bddeef393b6e05218c262ba2c23a02e2c181a944b3cd6c0306ec59`.
With the saved build patch, `meson compile -C build-libfprint-reference -j 2`
completed successfully. `meson test -C build-libfprint-reference --suite unit-tests
--no-rebuild --print-errorlogs` passed all three selected suites: fpi-device,
fpi-ssm, and fpi-assembling. These are generic library tests; no crfpmoc replay or
hardware test ran. Candidate examples were built but not executed. No install
command ran and the system libfprint remains untouched.

## Local command-logging fix — 2026-09-28

`patches/libfprint-4df05918-command-logging.patch` removes raw outgoing payload
logging for every command in the pinned candidate. The local reference checkout
has this patch applied in addition to the build patch. Metadata logging remains.
The offline regression compiles the actual static logger with instrumented
logging sinks and synthetic bytes, checks all 16-bit command IDs with transfer
debugging on/off, and checks an empty request. It failed on the original payload
dump and passed after the patch. Rebuild and all three generic unit suites passed.

Run the focused regression with:

```sh
python3 tests/test_reference_logging.py .reference-cache/libfprint-checkout/acd407-libfprint-4df0591/libfprint/drivers/crfpmoc/crfpmoc-ec-transfer.c
```

This covers the command logger only. The optional umockdev recorder is a separate
raw-output path and has not been approved for private data. FP_INFO v1 remains
hardcoded at four request sites (open, commit, capacity, verify), and reply-length
handling must be reviewed before adapting those sites. No compatibility patch
or candidate hardware test was started. This fix does not establish secure login.

## Local reply-length fix — resumed 2026-09-28

The candidate ioctl helper accepted short successful replies, leaving zero-filled
tails for callbacks to parse, and silently truncated oversized replies.
`patches/libfprint-4df05918-reply-length.patch` rejects replies whose length differs
from the requested length before copying output. All current command callers
require fixed-size replies, explicitly sized frame chunks, or empty replies.
Future variable-length FP_INFO v3 support must introduce actual-length propagation
and bounded parsing rather than weakening this check for existing callers.

Apply after the saved build and command-logging patches. It is applied in the
ignored reference checkout; no system library changed. The regression extracts
the actual ioctl helper, uses GLib errors and a fake ioctl, and checks expected
sizes 0–32 against returns -1–64 and success/error EC statuses. Rejected replies
must leave output untouched; successful copies must preserve the output canary.
No reader or real biometric data is used.

```sh
python3 tests/test_reference_replies.py .reference-cache/libfprint-checkout/acd407-libfprint-4df0591/libfprint/drivers/crfpmoc/crfpmoc-ec-transfer.c
```

The original helper failed because it accepted one byte when zero were expected.
The patched helper passes normally and under ASan/UBSan with leak checking
disabled. The command-logging regression, isolated rebuild, three generic unit
suites, and standalone `make test` also pass. This is a transport hardening step;
FP_INFO negotiation, secret ownership, recorder output, memory clearing, and
event/recovery behavior remain open. No candidate hardware operation ran.

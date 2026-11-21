# Initial implementation decisions

Date: 2026-09-27. Scope: read-only protocol foundation, not login readiness.

## Comparison of inspected implementations

| Concern | acd407 crfpmoc fork | ChocolateLoverRaj rust-fp + pinned crosec |
| --- | --- | --- |
| Device access | Linux misc device `cros_fp`, libfprint UDEV registration | `/dev/cros_fp`, own library and D-Bus service |
| Sensor-info protocol | Hardcodes FP_INFO v1 | Queries versions, then panics if v1 is absent |
| This reader's v2/v3-only FP_INFO | Requires adaptation | Requires adaptation |
| Template format | Defines version 4 | Seed command uses version 4 |
| User context | FP_CONTEXT v1 async request and completion polling | FP_CONTEXT v1; polls up to 20 times with 100 ms sleeps |
| Secret/context policy | Fixed seed and context in normal execution | Fixed seed of repeated `a`, fixed context of repeated `0xaa` |
| Events and cancellation | libfprint state machines, pollable event input, cancellation and suspend hooks | Async event waits, reset detection, multiple panic/unwrap paths; service has a timeout TODO |
| Device-open behavior | Driver open and operation state machines | Resets mode and sensor before reading information |
| Integration | libfprint enroll, verify/identify, cancel, clear-storage callbacks | Separate CLI, D-Bus service, PAM module |

This is a targeted source review, not a full security audit. It does not prove
either implementation can be exploited, or that all current forks have these
limitations. Shared seeds/contexts do not establish per-user isolation; the
complete authorization and storage architecture needs its own review.

Revisions:

- crfpmoc: `acd407/libfprint`, `4df05918f4670f5eed684c1771d2183f2e2df279`.
- rust-fp: `ChocolateLoverRaj/rust-fp`,
  `2d0b547e8800eea66d06fb52ed946f52cab30e37`.
- Its Cargo.lock pins `ChocolateLoverRaj/crosec-rs` to
  `b67daa7fd2edda15935c5a4d81000bc0767d7572`.

Relevant code: `libfprint/drivers/crfpmoc/{crfpmoc.c,crfpmoc.h}`,
`rust-fp/src/drivers/cros_fp.rs`, and
`crosec/src/commands/{fp_info,fp_set_context,fp_set_seed}.rs`.

## Decision: small new C protocol core

Write the first read-only layer in C11 from the documented protocol, with no
copied driver implementation. Keep it independent of GLib and the Linux device
API. Protocol tests need a compiler and Make; CLI/JSON tests also use Python 3. This is a limited new component,
not a commitment to replace all existing enrollment or integration work.

An injected synchronous exchange callback carries command ID, version, explicit
request/response lengths, and a separate controller result. Unit tests substitute
a scripted transport. The Linux adapter now implements the native-endian
20-byte ioctl header, separate from little-endian EC payloads. Its ABI layout
was checked against the installed 7.2.5-3-omarchy kernel headers:
`include/linux/platform_data/cros_ec_chardev.h` and `cros_ec_proto.h`.
It records errno separately from controller status and never substitutes the
allocated capacity for the returned valid byte count.

The blocking adapter is used only within a supervised worker in the hardware
CLI. A monotonic five-second deadline covers opening, querying, closing, and
collecting the worker result. The parent sends SIGKILL and does a nonblocking
reap on failure, then exits. An uninterruptible kernel operation may outlive
that request; this bounds the CLI wait rather than guaranteeing kernel ioctl
cancellation. The one-shot supervisor is not suitable for a persistent service
without a separate child-reaping and stuck-worker policy. No retries occur.

JSON schema version 1 exposes only selected metadata and status fields.
Unavailable values are null. Failed diagnostics emit no JSON. The CLI rejects
symlinks and non-character-device paths and opens with O_RDONLY/O_CLOEXEC.

## Decision: libfprint remains the integration target

The inspected libfprint interface and crfpmoc proposal demonstrate UDEV/misc
discovery and operation callbacks for enrollment, matching, cancellation, and
suspend. That makes a future adapter plausible. This internal driver API is
not stable; revalidate against a pinned upstream version before integration.

The interface does not settle seed custody or user authorization. The security review now selects fprintd for caller authorization and template
storage, with an explicit administrative provisioning utility. A separate
background helper remains conditional on demonstrated lifecycle or I/O needs.
Seed recovery and account mapping still require implementation and validation;
see SECURITY-DESIGN.md and SECURITY-SOURCES.md. Do not hide secret provisioning in probe/open.

## Decision: license

New project code is **LGPL-2.1-or-later**, compatible with the intended libfprint
integration. The full LGPL 2.1 text is in LICENSE; per-file SPDX identifiers
and the README specify the later-version option. The license text was copied
verbatim from the inspected libfprint checkout; no driver code was copied.

The inspected rust-fp repository has no top-level license file or license field
in its library manifest. Treat it as research material, not code to copy. Any
future imported source needs its own provenance and license review.

## Decision: build and tests

Use Make plus the available C compiler, with C11 and warnings treated as errors.
The diagnostic has no external runtime library dependency. The separate
seed-store component uses OpenSSL libcrypto for SHA-256; CLI tests use Python 3. Tests use explicit checks
that remain enabled under `NDEBUG`. They can run offline and without root.

Meson/GLib may become appropriate when an actual libfprint adapter exists. Avoid
installing them solely for the small first protocol component. CI uses the same
`make test` command and a separate address/undefined-behavior sanitizer build.

## Reference pinning

The ChromeOS header retrieved from
`https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/main/include/ec_commands.h?format=TEXT`
on the investigation date has decoded-content SHA-256:

`26712323b3d6cd28f0fe92d99feb1f9fd14eec0d9a936915faf7b7331d75878e`

The HEAD metadata returned commit
`0d18457b2e4b3d6ee5e6e84e16d810c8e398b9a0`, but fetching the header through that
commit URL returned HTTP 503. Do not claim that commit-to-file correspondence
was verified. The content hash above pins what was actually inspected. Resolve
the immutable retrieval before a later reference update. The header itself is
not vendored or a build-time download dependency.


## Seed-store increment

The seed store is a separate offline component, not linked into diagnostics.
It uses a fixed 72-byte versioned record and OpenSSL SHA-256 for accidental
corruption detection; this is neither encryption nor a MAC. Atomic no-replace
publication uses linkat followed by removal of the staging name and directory
fsync, instead of replacement rename. Failed attempts never return a seed.
See SEED-STORE.md for durability, pending-state, and trusted-directory limits.

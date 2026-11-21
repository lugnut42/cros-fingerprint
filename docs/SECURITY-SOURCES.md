# Security source review

Inspected 2026-09-27. Reference code was read, not built or installed. No
third-party source was imported. This is a targeted review, not a security audit.

## fprintd

Official repository: https://gitlab.freedesktop.org/libfprint/fprintd

Pinned checkout: `b10251ea9028dee04e02146ec177253288ec0d3d`
(commit dated 2026-07-24), read from /tmp/cros-fprintd-security-review.

- [src/device.c](https://gitlab.freedesktop.org/libfprint/fprintd/-/blob/b10251ea9028dee04e02146ec177253288ec0d3d/src/device.c):
  caller UID comes from the D-Bus connection; selecting another username checks
  PolicyKit. Enrollment sets the print username from the claimed session;
  verification loads that user's prints. Claim ownership and vanished clients
  have explicit handling.
- [src/file_storage.c](https://gitlab.freedesktop.org/libfprint/fprintd/-/blob/b10251ea9028dee04e02146ec177253288ec0d3d/src/file_storage.c):
  serializes prints; loading checks finger, username, and device compatibility.
  This is not a complete crash-durability audit.
- [data/fprintd.service.in](https://gitlab.freedesktop.org/libfprint/fprintd/-/blob/b10251ea9028dee04e02146ec177253288ec0d3d/data/fprintd.service.in):
  StateDirectory=fprint with mode 0700, ProtectSystem=strict, and explicit
  DeviceAllow=/dev/cros_fp. These are upstream source settings, not verification
  of this machine's installed unit or packaging.
- [D-Bus interface documentation](https://fprint.freedesktop.org/fprintd-dev/Device.html):
  public contract for Claim, enrollment, verification, and PolicyKit actions.

Inference: existing fprintd responsibilities can cover caller authorization
and template persistence. This does not establish seed delivery or account
incarnation tracking; those need explicit integration work.

## libfprint

Local reference remains the reviewed acd407 fork at
`4df05918f4670f5eed684c1771d2183f2e2df279`; this is not asserted to be current
upstream. Inspected `libfprint/fpi-device.c` functions for enroll/verify/identify
data and `libfprint/fp-print.c` metadata accessors and serialization.

The callbacks expose prints/gallery data. Serialized prints include metadata
and driver data. Username is user-provided and nullable; possessing a print
object is not authorization. Confirm the final adapter against a pinned
upstream base before implementation.

[Official FpPrint API](https://fprint.freedesktop.org/libfprint-dev/FpPrint.html)
corroborates the username and serialization interfaces.

## Firmware revision matching the device label

The running RW firmware label ends in `e07b67c`. Gitiles returned these files
at that revision, stored only in /tmp/cros-ec-security-review:

| File at e07b67c | SHA-256 of decoded retrieved content |
| --- | --- |
| [common/fpsensor/fpsensor_state.cc](https://chromium.googlesource.com/chromiumos/platform/ec/+/e07b67c/common/fpsensor/fpsensor_state.cc) | `58b066f9f39deca7e0b0010771cde64ee80c5c966bf3145a43db08a1832d43f3` |
| [common/fpsensor/fpsensor_crypto.cc](https://chromium.googlesource.com/chromiumos/platform/ec/+/e07b67c/common/fpsensor/fpsensor_crypto.cc) | `0f9abbe3bc06c30aae9cf69b0b1a160cb344896a205b870298b647369ceeafc1` |
| [include/ec_commands.h](https://chromium.googlesource.com/chromiumos/platform/ec/+/e07b67c/include/ec_commands.h) | `9ca749d84437ea292ebf1a2597d10d7ad669e00d1fc8c95295b155ad1ae5ddd3` |

The commit-metadata request returned HTTP 503, so the full commit hash is not
verified. Content hashes pin the files actually inspected. Matching a firmware
label does not prove binary equivalence, build options, or runtime semantics.
No firmware was read back or changed.

In `fpsensor_state.cc`, `fp_command_tpm_seed` checks format, rejects a second
seed, and sets the seed bit. `fp_reset_context` retains the seed bit while
clearing user/template state. `fp_command_context` starts reset asynchronously;
GET_RESULT copies the supplied user ID once reset completes. Encryption status
marks only the seed-set field valid. Therefore a context reset cannot be
assumed to clear the seed and status cannot establish seed identity.

The header specifies format 4 and 32-byte seed/user ID. Crypto code uses
rollback entropy, a seed input, optionally configured OTP material, HKDF, and
AES-GCM. Template format and function availability do not prove all optional
protections are enabled on this machine. No host crypto code was copied.

Also retrieved main-branch `fpsensor_state.cc`, SHA-256
`c8f85fa66b317a3b1b4eac824560c781b995b8e6443daf8cdf9b34910e7a48f6`.
Prefer the label-matching reference above for this investigation. Main-branch
crypto retrieval failed with HTTP 503; no conclusions depend on it.

## Historical background

[ChromeOS fingerprint design](https://chromium.googlesource.com/chromiumos/platform/ec/+/01aa26ee/docs/fingerprint/fingerprint-authentication-design-doc.md)
(last updated 2019) describes firmware-held templates and key derivation inputs.
It is historical architecture context, not a security guarantee for this port.

## Linux event-queue review (2026-09-28)

Reviewed upstream Linux `drivers/platform/chrome/cros_ec_chardev.c` at
[75f2c0b3690702c90863c2e138cb5520670845ea](https://github.com/torvalds/linux/blob/75f2c0b3690702c90863c2e138cb5520670845ea/drivers/platform/chrome/cros_ec_chardev.c).
Downloaded content SHA-256:
`decd900d1c85e854c56bb215ccd758d5e9a76468e63dd773cdb826bd7f58203f`.
This pins the reviewed reference, not this machine's patched kernel source.
No kernel implementation was copied, compiled, or installed.

The reviewed driver allocates per-open queue/subscription state. The mask ioctl
accepts a scalar value and does not flush existing events. A nonzero mask selects
event reads; zero selects legacy version text. Nonblocking empty reads return
EWOULDBLOCK. Each read consumes one event, truncating and discarding excess bytes
when the buffer is short. Release unregisters the notifier and frees that open
queue; it sends no sensor-mode/reset command. Capacity/allocation failures can
drop events without a queued loss marker.

Inference: a new descriptor, successful drain, or process exit cannot establish
firmware quiescence or event freshness. The initial adapter only subscribes and
discards; it is not a cleanup proof. See EVENTS.md for its contract and limits.

The installed 7.2.5-3-omarchy headers corroborate the event-mask ioctl number,
fingerprint event type 5, and 32-bit event payload. Header SHA-256 values:

- `include/linux/platform_data/cros_ec_chardev.h`:
  `75385fa241734f2d5667980b6ce460320ec0a36db694bb919eada44d54c02564`
- `include/linux/platform_data/cros_ec_commands.h`:
  `810948db3ebfbb11ba11bc9593c030f75d38b55bc4c1be22d3ef1775c73c7faf`

Paths are relative to `/usr/lib/modules/7.2.5-3-omarchy/build`. Header consistency
is not proof of running event-driver behavior. The adapter was written from
these interface facts without importing GPL implementation code.

Fresh retrieval of label-matching firmware `e07b67c` failed again: Gitiles TEXT,
+show, and commit JSON endpoints returned HTTP 503 outside the sandbox; the
coreboot raw mirror returned 404. The earlier content hashes remain recorded,
but source files were not recovered. No new firmware reset/mode conclusions
were established, and already-set-seed recovery remains blocked.

## Recovered firmware references and mode semantics (2026-09-28)

Gitiles retrieval succeeded later in the session. `fpsensor_state.cc` and
`ec_commands.h` exactly match the hashes previously recorded above. Additional
files retrieved under `e07b67c` are pinned by decoded content:

| Path | SHA-256 |
| --- | --- |
| `common/fpsensor/fpsensor.cc` | `42c3b8fe8a3618d341d9e0414f8e5612ec6d54f8dbcee1a1807c8bd2a5beee69` |
| `common/fpsensor/fpsensor_auth_commands.cc` | `88c20e14f53a9b3ccd9d9b336239a815cb74f165754c2f4e1843fad8ba032e03` |
| `include/fpsensor/fpsensor_auth_commands.h` | `4fde6f7a7d2f24ad5c104971c449d92e09a3330835eefad03abd8e4a64abe66d` |
| `common/system.c` | `de67a581d318aedca30a74e8aad9559815277436d6348e00a721bc99b71a2ed0` |

File links use the existing Gitiles base, for example
[fpsensor.cc](https://chromium.googlesource.com/chromiumos/platform/ec/+/e07b67c/common/fpsensor/fpsensor.cc).
Full commit metadata still failed, and matching board build configuration has
not been recovered. Content pinning and label agreement do not prove binary or
configuration equivalence. Local references now survive reboot in ignored
`.reference-cache/`, with a SHA256.json manifest. They are not imported into the
build or distributed as project source. Their existing copyright notices remain;
the separate license fetch failed, so do not copy their implementations.

In `fpsensor_state.cc`, exact FP_MODE DONT_CHANGE skips mutation and returns
sensor_mode. A mode write updates that variable and posts task configuration;
its response is not confirmation that the sensor task finished. In `fpsensor.cc`,
context reset executes in that task, then clears RESET_SENSOR. Reset does not
clear the aggregate fingerprint event word. An empty Linux queue and configured
idle therefore do not establish event freshness. These findings justify idle
pre/postflight checks but not an automatic mode-stop/reset recovery sequence.

`fingerprint_auth_enabled` tests SESSION_ESTABLISHED, not command-version support.
Without that state, the reviewed mode handler permits the classic flow; with it,
enrollment/matching require a MAC. Do not establish or deliberately corrupt an
authenticated session as a seed-clearing workaround. Runtime behavior remains
to validate and unsupported status bits remain uninterpreted in diagnostics.

`system.c` handles EC_REBOOT_COLD using system_reset(SYSTEM_RESET_HARD), with
board-dependent host/PD shutdown paths. This is insufficient to authorize a
controller-only reset without matching board/build and transport validation.
No controller reboot, mode write, or authentication command was issued.

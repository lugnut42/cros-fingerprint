# Target hardware and investigation record

Observed locally on 2026-09-27. These are diagnostic observations, not evidence
that authentication works.

| Item | Observation |
| --- | --- |
| Machine | HP Elite Dragonfly Chromebook, Redrix |
| DMI family | Google_Brya |
| Distribution | Omarchy 4.0.4-1, Arch-based |
| Running kernel | 7.2.5-3-omarchy |
| Transport | SPI; driver `cros-ec-spi` |
| Userspace device | `/dev/cros_fp`, root-owned, mode 0600 |
| Relevant loaded modules | `cros_ec_spi`, `cros_ec_dev`, `cros_ec_chardev` |
| Controller | ST STM32F412CX, firmware board `bloonchipper` |
| RO firmware | `bloonchipper_v2.0.5938-197506c1` |
| RW firmware | `bloonchipper-v2.0.30778-e07b67c` |
| Active firmware | RW |
| Sensor identifiers | vendor 541282374; product 9; model 543; version 1 |
| Reported image dimensions | 160 × 160, 8 bits per pixel |
| Advertised capture formats | 6 |
| Maximum loaded templates | 5 |
| Currently loaded templates | 0 |
| Maximum template size | 5156 bytes |
| Template format version | 4 |
| Encryption seed status | Not set at time of query |

The earlier apparent absence of `/dev/cros_fp` was caused by the execution
sandbox hiding device nodes. Checking outside it confirmed the node exists.
Do not diagnose missing kernel support from sandbox visibility alone.

## Advertised command versions

These were read with `EC_CMD_GET_CMD_VERSIONS` version 1. Querying support did
not execute the commands being queried.

| Command | ID | Versions |
| --- | --- | --- |
| FP_MODE | 0x0402 | 0, 1 |
| FP_INFO | 0x0403 | 2, 3 |
| FP_FRAME | 0x0404 | 0, 1 |
| FP_TEMPLATE | 0x0405 | 0, 1 |
| FP_CONTEXT | 0x0406 | 1 |
| FP_STATS | 0x0407 | 0 |
| FP_SEED | 0x0408 | 0 |
| FP_ENC_STATUS | 0x0409 | 0 |

Sensor details were read using FP_INFO version 3. FP_ENC_STATUS returned
`valid_flags=1`, `status=0`. Sensor `errors=1023` (`0x03ff`) means the dead-pixel
count is unknown; the separate no-IRQ, SPI communication, bad hardware-ID, and
initialization-failure bits were not set. This does not constitute a capture
test or prove the entire sensor is healthy.

## Protocol compatibility finding

The installed Linux header described FP_INFO versions 0/1, while this firmware
advertises only versions 2/3. The current ChromeOS header documents these newer
layouts. Capability negotiation is a requirement, not an optional optimization.

The temporary diagnostic's initial parser treated response-buffer padding as
extra formats. It was corrected to bound iteration by the advertised format
count. This now has a synthetic regression test; do not assume an ioctl's
reported buffer length describes only semantically valid records.

## Not tested

- Explicit controller-only reset and power-loss recovery (normal host reboot
  seed/context restoration is recorded below).
- Capture, enrollment, matching, or template export/import.
- Biometric quality, false acceptance/rejection rates, or spoof resistance.
- Biometric persistence after reboot, suspend recovery, or isolation between users.
- fprintd, PAM, or lock-screen integration.

The probe ran from `/tmp` and was not installed. No biometric data was collected.
That ad hoc script is not the implementation baseline for this repository.

## C diagnostic validation (2026-09-27)

Ran `pkexec /home/t/Work/cros-fingerprint/build/cros-fingerprint-diagnostic`
after the offline and ASan/UBSan suites passed. It exited 0 with schema version
1 JSON. The sandbox-only attempt could not see the node, as before.

The C implementation selected info v3 and reproduced vendor 541282374, product
9, model 543, sensor version 1, six capture formats, 160×160 at 8 bpp, template
size 5156, capacity 5, zero loaded templates, dirty bitmap 0, and format version
4. All six formats report image-data offset 400. Capture types 0 and 16 report
frame size 26260; types 4, 8, 12, and 20 report 25600. No image was requested.
Errors remained 1023 with zero fault flags and unknown dead-pixel count.
Encryption valid_flags=1 and status=0 establish that the seed is not set.
The only commands issued were capability queries, FP_INFO, and FP_ENC_STATUS.


## Seed/context experiment (2026-09-28)

The explicitly authorized seed/context smoke test succeeded and a fresh
read-only diagnostic confirmed the result. Encryption status changed from 0
to 9, with valid_flags=1 and seed_set=true. FP_CONTEXT v1 completed successfully.
The valid flag only supports interpreting seed bit 0; context completion is
established by the command response, not an assumed meaning of unsupported bits.
Template count remains zero, template format 4, errors=1023, fault_flags=0.

Real root-only seed/context records now exist outside the checkout under
/var/lib/cros-fingerprint-experiment. Verified directory modes 0700, record
modes 0600, owner UID 0, and record sizes 72 bytes without displaying contents.
See SEED-CONTEXT-EXPERIMENT.md for scope and recovery restrictions. No biometric
capture/enrollment/matching has taken place.


## Normal host reboot and restoration (2026-09-28)

After the user rebooted, the read-only diagnostic reported valid_flags=1,
status=0, seed_set=false, zero templates, and fault_flags=0. The existing
`--restore` action then succeeded using the saved private seed/context records.
A fresh diagnostic confirmed status=9, seed_set=true, zero templates, and
fault_flags=0. This establishes seed/context restoration after this normal host
reboot, not template persistence or biometric verification. See
SEED-CONTEXT-EXPERIMENT.md for the full execution record and remaining limits.

## Configured mode observation (2026-09-28)

The tested `cros-fingerprint-diagnostic --mode-status` queried command versions
and FP_MODE v0 with exact DONT_CHANGE. It returned mode 0/configured_idle=true.
No mode write occurred. This reports configuration only, not confirmed sensor
work completion, drained in-flight events, or a secure authentication state.

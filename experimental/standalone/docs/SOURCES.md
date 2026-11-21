# Research sources and prior art

Reviewed during the initial investigation, 2026-09-27. Source links may move;
pin implementation/reference revisions in milestone 0 before relying on them.
Do not interpret documentation links as installation instructions.

## Protocol and architecture

- [ChromeOS EC command definitions](https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/include/ec_commands.h):
  FP_INFO versions 2/3, template format, contexts, and encryption status.
- [ChromeOS fingerprint firmware documentation](https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/fingerprint/fingerprint.md):
  controller/sensor families; maps bloonchipper to STM32F412 and FPC1025.
- [ChromeOS biod architecture](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/README.md):
  enrollment, authentication, storage, and communication through `/dev/cros_fp`.
- [Linux cros_ec_chardev](https://github.com/torvalds/linux/blob/master/drivers/platform/chrome/cros_ec_chardev.c)
  and [cros_ec_dev](https://github.com/torvalds/linux/blob/master/drivers/mfd/cros_ec_dev.c):
  userspace transport and controller registration.
- [libfprint developer reference](https://fprint.freedesktop.org/libfprint-dev/):
  driver interfaces and testing/integration starting point.

## Existing implementations

### crfpmoc / libfprint

- [Upstream proposal, merge request 512](https://gitlab.freedesktop.org/libfprint/libfprint/-/merge_requests/512).
  Automated access was denied; current review/merge state was not established.
- [acd407/libfprint](https://github.com/acd407/libfprint), branch `feature/crfpmoc`.
  Inspected locally at revision
  `4df05918f4670f5eed684c1771d2183f2e2df279`, dated 2026-08-15.
- [Author's Redrix report](https://www.cnblogs.com/acd407/articles/19669407)
  reports successful fingerprint operation on their Redrix/CachyOS setup. This
  is a firsthand report, not proof that their code supports our firmware.

Specific source observations from the inspected fork:

- FP_INFO requests hardcode version 1, incompatible with the versions our
  controller advertises. Confirm all call sites when adapting the protocol.
- Template format is defined as version 4.
- `crfpmoc_init_keys` uses fixed seed and context constants in normal execution.
  The nearby header describes emulation-only use, but the implementation does
  not restrict that initialization to emulation. Review actual behavior rather
  than relying on comments. No claim of a specific exploit has been established.

Do not use the AUR package `libfprint-crfpmoc-git`. Its former maintainer reports
it was compromised after abandonment and warns against its use. The package
was neither installed nor executed in our investigation. Its current cleanup
status was not independently established. This report does not establish that
the separate GitHub driver source is malicious.

### rust-fp

- [ChocolateLoverRaj/rust-fp](https://github.com/ChocolateLoverRaj/rust-fp):
  existing Chromebook library, CLI, D-Bus service, and PAM module.
- [acd407/rust-fp](https://github.com/acd407/rust-fp): Redrix owner's fork.
- [Developer discussion including Redrix](https://forum.chrultrabook.com/t/in-search-of-contributors-to-a-linux-chromebook-fingerprint-pam-module/1819?page=4):
  firsthand reports about restoring templates and the need to set user context.

The upstream rust-fp implementation and its locked crosec dependency have now
had a targeted review; see DECISIONS.md for pinned revisions and findings. Its
dependency only supports FP_INFO v1, and it uses constant seed/context values.
The acd407 rust-fp fork remains a candidate for further inspection. No full
security audit of these projects has been performed.

### Earlier diagnostic experiment

[chrultrabook/linux-tools/cros-fp.py](https://github.com/chrultrabook/linux-tools/blob/main/cros-fp.py)
demonstrates ectool-based enrollment/matching. It uses a constant seed and runs
state-changing commands. It was read, not executed, and is not suitable as a
production login design.

## Research conclusion

The earlier broad statement that Chromebook fingerprints were unsupported was
too categorical. Community implementations and Redrix success reports exist.
The work should begin by comparing and adapting existing approaches to this
firmware, not assuming a new kernel driver or a complete rewrite is necessary.
No search can prove the absence of another working implementation.

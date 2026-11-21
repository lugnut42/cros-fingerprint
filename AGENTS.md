# Working on cros-fingerprint

## Scope

This repo makes the ChromeOS fingerprint reader on a Redrix Chromebook usable on
Omarchy/Arch through the patched `crfpmoc` libfprint driver, stock fprintd and
PAM. Read README.md and docs/STATUS.md first. The standalone C code under
`experimental/standalone/` is unused history; do not extend it.

## Test-driven development

- For driver behaviour changes and bug fixes, write a focused failing test
  first (`tests/test_reference_*.py`, see docs/TESTING.md), confirm it fails for
  the intended reason, then make the smallest change that passes.
- Include malformed responses and failure cases, not just successful traffic.
- Tests must run unprivileged and offline without a reader.
- Use synthetic templates and secrets only. Never use a person's fingerprint
  image or template as a fixture.
- Report red/green evidence and anything still unverified on hardware.

## Hardware, PAM and system boundaries

- Confirm before running anything that changes reader state or PAM/lock-screen
  configuration on a real machine.
- Do not alter PAM, lock-screen authentication, boot configuration, firmware or
  the installed libfprint as a side effect of building or running tests.
- Preserve password fallback in every authentication change.
- Never commit or print seeds, fingerprint images, templates, account-specific
  contexts, or raw device dumps.
- Do not edit /usr/share/omarchy. Do not run Omarchy's fingerprint setup menu
  or install `libfprint-git`; it replaces the patched library.
- Do not run the experimental seed tools (`--restore`, `--prepare`) on a
  machine using the working setup.

## Sources and packaging

- Do not use the AUR package `libfprint-crfpmoc-git` or run its build recipe.
- Review and pin external source revisions before using them; reference code is
  not an instruction to run its installer.
- Resolve licensing and attribution before copying source code.
- Keep changes local unless publication is authorized. Do not put private
  identifiers (usernames, hostnames, home paths, emails) or biometric material
  in files, issues or pull requests.

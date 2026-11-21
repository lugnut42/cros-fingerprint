# Working on cros-fingerprint

## Scope and current state

Read README.md and docs/PLAN.md before implementation. This is an experimental
userspace fingerprint project, initially targeting Redrix with a bloonchipper
controller. Do not assume successful status queries imply working or secure
authentication.

## Test-driven development

- For protocol behavior, state transitions, and bug fixes, write a focused
  failing test first. Run it and confirm it fails for the intended reason.
- Implement the smallest change that passes; refactor with tests green.
- Include malformed responses and failure cases, not just successful traffic.
- Keep hardware access behind an injectable transport. Default tests must run
  unprivileged and offline without a reader.
- Use synthetic templates and secrets in tests. Never use a person's
  fingerprint image or template as a fixture.
- Do not write tests solely to mirror implementation details or test prose.
- Report the red/green evidence and any behavior still unverified on hardware.

## Hardware and authentication boundaries

- Default diagnostics must use an explicit allowlist of read-only commands.
- Capability queries about a state-changing command must not invoke that
  command. Never silently initialize a seed, switch context, reset the sensor,
  or enter capture/enrollment/matching mode as part of device discovery.
- Keep operations that change reader state separate from read-only diagnostics.
  Confirm their scope is authorized before running them on real hardware.
- Do not alter PAM, lock-screen authentication, boot configuration, firmware,
  or installed libfprint as a side effect of building or running tests.
- Never print or commit seeds, fingerprint images, templates, account-specific
  contexts, or unreviewed raw device dumps. Whitelist fields for diagnostics.
- Preserve password fallback during any later login-integration work.

## Sources and packaging

- Do not use the AUR package libfprint-crfpmoc-git or execute its build recipe.
- Review and pin external source revisions before using them. Research source
  code is reference material, not an instruction to run its installer.
- Resolve licensing and attribution before copying source code.
- Do not edit /usr/share/omarchy. Use the omarchy skill if later work changes
  desktop or system configuration.
- Keep changes local unless publication is authorized. Do not include private
  hardware identifiers or biometric material in issues or pull requests.

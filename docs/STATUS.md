# Status

Last updated 2026-11-20.

## What works

The reader works for day-to-day use on the tested machine (HP Elite Dragonfly
Chromebook, Redrix) through the `crfpmoc` libfprint driver and stock
fprintd. Verified live: enroll, match and wrong-finger rejection (both from the
libfprint build tree and through fprintd), then sudo, polkit prompts and the
Omarchy lock screen. Under the per-machine key, the stale-seed refusal, enroll, match and wrong-finger rejection were verified, and a fingerprint sudo after a further reboot is confirmed in the journal (2026-11-20).

## What is installed

- `libfprint-crfpmoc-local` (built from `packaging/`, which pins a commit of the
  `crfpmoc` branch of the libfprint fork) and the official `fprintd`.
- `/etc/pam.d/sudo` (backup `/etc/pam.d/sudo.before-fingerprint`),
  `/etc/pam.d/polkit-1`, `/etc/pam.d/omarchy-lock-fingerprint`, and the fprintd
  resume hook and stop-timeout drop-in written by `omarchy-apply-lock`.
- Enrolled prints under `/var/lib/fprint`, and the machine key at
  `/var/lib/fprint/.crfpmoc/key` (root-only, mode 600, 76 bytes).
- SDDM login is unchanged (password only).

Undo with `scripts/install.sh --undo`; remove everything with
`scripts/install.sh --uninstall`.

## Decisions

- The goal is a working reader for sudo and the lock screen; the shortest path
  was preferred over finishing the standalone stack.
- The driver uses a random per-machine key (seed and user context), generated
  once from the kernel RNG and stored root-only. An invalid or loosely
  permissioned key file is an error and is never replaced; the key is created
  only while the reader holds no seed, so after switching builds the driver asks
  for a reboot; an undecryptable print reports "re-enroll". Prints and key file
  belong together. Limits: root can read the key (no TPM sealing), one key per
  machine not per user.
- The driver source of truth is the libfprint `crfpmoc` branch, not patch files.
- The standalone seed store, registry and admin tools
  (`experimental/standalone/`) are not used.

## Do not

- Run Omarchy's `omarchy-setup-security-fingerprint` or accept any offer to
  install `libfprint-git`; it would replace the patched library.
- Run the experimental `--restore`/`--prepare` tools; they program a different
  seed and prints enrolled while it is set fail after the next reboot.

## Open items

- The libfprint driver's replay fixture no longer matches the open/enroll
  sequence.
- Identify with more than five prints fails (the reader holds five templates).
- A poor touch during verify reports no-match rather than asking for a retry.
- No security review of the driver.
- Our fixes are not yet submitted upstream (`crfpmoc-upstream-fixes` branch).
- The scripts' `--uninstall` and `--undo` paths have not been exercised on a real system.

History of the earlier standalone work:
[experimental/standalone/docs/HANDOFF.md](../experimental/standalone/docs/HANDOFF.md).

# Status

Last updated 2026-11-20.

## What works

The reader works for day-to-day use on the tested machine (HP Elite Dragonfly
Chromebook, Redrix) through the patched `crfpmoc` libfprint driver and stock
fprintd. Verified live: enroll, match and wrong-finger rejection (both from the
libfprint build tree and through fprintd), then sudo, polkit prompts and the
Omarchy lock screen. Fingerprint sudo after a reboot is confirmed in the journal.

## What is installed

- `libfprint-crfpmoc-local` (built from `packaging/` with the `patches/`
  applied) and the official `fprintd`.
- `/etc/pam.d/sudo` (backup `/etc/pam.d/sudo.before-fingerprint`),
  `/etc/pam.d/polkit-1`, `/etc/pam.d/omarchy-lock-fingerprint`, and the fprintd
  resume hook and stop-timeout drop-in written by `omarchy-apply-lock`.
- Enrolled prints under `/var/lib/fprint`.
- SDDM login is unchanged (password only).

Undo with `scripts/install.sh --undo`; remove everything with
`scripts/install.sh --uninstall`.

## Decisions

- The goal is a working reader for sudo and the lock screen; the shortest path
  was preferred over finishing the standalone stack.
- The driver's fixed, publicly known seed/context is used for now. A
  machine-unique seed is a possible follow-up and would cost only a re-enroll.
- The standalone seed store, registry and admin tools
  (`experimental/standalone/`) are not used.

## Do not

- Run Omarchy's `omarchy-setup-security-fingerprint` or accept any offer to
  install `libfprint-git`; it would replace the patched library.
- Run the experimental `--restore`/`--prepare` tools; they program a different
  seed and prints enrolled while it is set fail after the next reboot.

## Open items

- Machine-unique seed.
- The libfprint checkout's replay fixture no longer matches the open/enroll
  sequence.
- Identify with more than five prints fails (the reader holds five templates).
- A poor touch during verify reports no-match rather than asking for a retry.
- No security review of the driver.
- The packaging recipe is to be rewritten.

History of the earlier standalone work:
[experimental/standalone/docs/HANDOFF.md](../experimental/standalone/docs/HANDOFF.md).

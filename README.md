# cros-fingerprint

Fingerprint login for the ChromeOS fingerprint reader (FPMCU) on an HP Elite
Dragonfly Chromebook ("Redrix") running Omarchy or another Arch-based Linux.

It works by packaging the `crfpmoc` driver for libfprint (from a libfprint branch), using stock
`fprintd`, and adding PAM lines so a fingerprint can approve `sudo`, polkit
prompts and (on Omarchy) the lock screen. Password fallback is kept everywhere.
This repo holds the packaging recipe, the install scripts and the driver tests;
the driver source itself lives on a libfprint branch (see Upstream).

## Supported hardware

Tested on exactly one machine: HP Elite Dragonfly Chromebook (Redrix), FPMCU
board `bloonchipper`, firmware offering FP_INFO v2/v3. Facts are in
[docs/HARDWARE.md](docs/HARDWARE.md). Other Chromebooks are untested; they may
work if they use the same kind of reader, or may not.

## Prerequisites

- Linux already running on the Chromebook, with `/dev/cros_fp` present.
- An Arch-based distribution with `pacman`, `sudo`, and `base-devel` (for
  `makepkg`). Omarchy is the primary target; plain Arch works but skips the
  lock-screen step.
- Your normal (non-root) user. The scripts call `sudo` themselves.

## Quick start

```sh
git clone <this repo> && cd cros-fingerprint
scripts/install.sh
```

The script walks through four stages, pausing before each: locate or build the
package, install it plus `fprintd`, enroll one finger and check a match and a
non-match, then enable fingerprint for sudo, polkit and the lock screen. The
sudo step makes you test in a second terminal first and rolls back if you say
it failed. Options: `--package FILE` to use a prebuilt package, `--undo`,
`--uninstall` (see `scripts/install.sh --help`).

## What it changes, and how to undo

- Installs `libfprint-crfpmoc-local` (replacing stock `libfprint`) and `fprintd`.
- Stores your enrolled print(s) under `/var/lib/fprint`, and a random
  per-machine key at `/var/lib/fprint/.crfpmoc/key` (root-only), created on first
  enroll or verify.
- Edits `/etc/pam.d/sudo` (backup at `/etc/pam.d/sudo.before-fingerprint`) and
  creates `/etc/pam.d/polkit-1`.
- On Omarchy, runs `omarchy-apply-lock`, which writes
  `/etc/pam.d/omarchy-lock-fingerprint`, an fprintd resume hook and a
  stop-timeout drop-in. The PAM lines match Omarchy's own, so its migrations
  and removal script recognise them.
- Does not touch SDDM/login, firmware or `/usr/share/omarchy`.

Undo: `scripts/install.sh --undo` puts sudo, polkit and the lock screen back to
password only. `scripts/install.sh --uninstall` also removes both packages and,
if you answer yes, your enrolled prints.

## Known limitations and security

- Templates are encrypted by the reader with a random per-machine key (seed and
  user context) the driver generates once from the kernel RNG. The key is
  readable by root (no TPM sealing) and is one key per machine, not per user.
- The driver has had **no security review**. A fingerprint can approve `sudo`,
  so treat this as convenience, not strong authentication.
- SDDM/login stays password-only.
- Reader firmware is not updated under Linux.
- The reader holds five templates; identifying across more than five fails, and
  a poor touch during verify reports no-match rather than a retry.

## Do not

- Do not run Omarchy's fingerprint setup menu (`omarchy-setup-security-fingerprint`)
  or accept `libfprint-git`: it replaces the patched library.
- Do not run the experimental seed tools in `experimental/standalone/`
  (`--restore`, `--prepare`): they program a different seed, and prints enrolled
  while it is set stop working after a reboot.
- Do not delete, edit or loosen the permissions of `/var/lib/fprint/.crfpmoc/key`:
  the driver treats an invalid key file as an error and never replaces it.

## Upstream

The `crfpmoc` driver is an open upstream merge request (libfprint !512),
maintained by a libfprint maintainer and not merged. The driver source for this
repo is the `crfpmoc` branch of `https://github.com/lugnut42/libfprint`:
upstream master, the !512 history (authorship preserved), three commits from the
acd407 downstream fork, and our fixes as separate commits. The fixes meant for
the maintainer are on the `crfpmoc-upstream-fixes` branch and have not been
submitted yet. Omarchy detection of this reader can only follow once upstream
libfprint includes the driver.

## Updating / rebuilding

`packaging/PKGBUILD` pins one commit of the `crfpmoc` branch. To move to a newer
commit, change `_commit` and `pkgver`, run `makepkg` in `packaging/`, then
`scripts/update.sh` to install it and re-check your enrolled finger. To build
from a local clone: `LIBFPRINT_REPO=file:///path/to/libfprint makepkg -f`. If you
are coming from an older fixed-seed build, use `scripts/migrate-to-machine-key.sh`
once (it deletes prints; reboot, then re-enroll).

## Backup

Enrolled prints and the key file belong together. Back up and restore all of
`/var/lib/fprint`, including the hidden `.crfpmoc` directory, or re-enroll. Losing
the key only costs a re-enroll.

## Layout

| Path | Contents |
| --- | --- |
| `scripts/` | `install.sh` (entry point), plus the steps it runs: `install-and-enroll.sh`, `enable-fingerprint-auth.sh`; `update.sh`, `migrate-to-machine-key.sh`, `smoke-test.sh` (build tree of a libfprint clone), `lib.sh` (shared helpers) |
| `packaging/` | PKGBUILD for `libfprint-crfpmoc-local`; pins a commit of the libfprint `crfpmoc` branch |
| `tests/` | offline driver tests (run against a libfprint clone), see [docs/TESTING.md](docs/TESTING.md) |
| `docs/` | [STATUS.md](docs/STATUS.md), [HARDWARE.md](docs/HARDWARE.md), [TESTING.md](docs/TESTING.md) |
| `experimental/standalone/` | earlier standalone C groundwork, unused; holds the read-only diagnostic |
| `AGENTS.md` | rules for contributors and coding agents |

## License

LGPL-2.1-or-later; see [LICENSE](LICENSE).

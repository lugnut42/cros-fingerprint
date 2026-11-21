#!/usr/bin/env bash
# Replace the installed libfprint package with a newer build and check that the
# already-enrolled finger still matches. PAM and enrolled prints are untouched.
#
# usage: update.sh [--package FILE.pkg.tar.zst]
# Without --package, the newest packaging/libfprint-crfpmoc-local-*.pkg.tar.zst
# is used. Keep the previous package file until the new one is proven: if the
# check fails, go back with `sudo pacman -U <previous file>`.
set -u

# shellcheck source=lib.sh
source "$(dirname "$(readlink -f "$0")")/lib.sh"

PKG=
while [[ $# -gt 0 ]]; do
  case $1 in
    --package) PKG=${2:?--package needs a file}; shift 2 ;;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -n $PKG ]] || PKG=$(fp_newest_package)

if [[ $EUID -eq 0 ]]; then
  echo "Run this as your normal user; it calls sudo itself." >&2
  exit 2
fi
[[ -n $PKG && -f $PKG ]] || { echo "Missing package: ${PKG:-(none found in $PACKAGING_DIR)}" >&2; exit 2; }
PKG=$(readlink -f "$PKG")
LOG="$HOME/fp-smoke/update.log"
mkdir -p "$(dirname "$LOG")"
exec > >(tee "$LOG") 2>&1

echo "Installed now: $(pacman -Q "$PKG_NAME" 2>/dev/null || echo "(not installed)")"
echo "Installing:    $PKG"
# Password only: do not depend on the reader while its library is being replaced.
echo "(If asked, you can use your finger or wait for the password prompt.)"
sudo -v || exit 2
sudo pacman -U --noconfirm "$PKG" || { echo "package install failed" >&2; exit 1; }
sudo systemctl restart fprintd
sleep 1

echo
echo "-- Enrolled prints --"
sudo fprintd-list "$USER" || { echo "fprintd cannot see the reader; see: journalctl -u fprintd -b" >&2; exit 1; }

echo
echo "-- Verify with your ENROLLED finger (expect verify-match) --"
pause "Ready?"
sudo fprintd-verify "$USER"
echo "exit code: $?"

echo
echo "-- Verify with a DIFFERENT finger (expect verify-no-match) --"
pause "Ready?"
sudo fprintd-verify "$USER"
echo "exit code: $?  (non-zero / verify-no-match is the expected result here)"

echo
echo "Installed now: $(pacman -Q "$PKG_NAME")"
echo "Done. Log saved to $LOG"

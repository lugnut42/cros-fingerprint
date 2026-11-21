#!/usr/bin/env bash
# Install the patched libfprint package plus stock fprintd, enroll one finger
# through fprintd, and check a match and a non-match.
# Does NOT touch PAM, sudo, or the lock screen.
#
# usage: install-and-enroll.sh [--package FILE.pkg.tar.zst]
# Without --package, the newest packaging/libfprint-crfpmoc-local-*.pkg.tar.zst
# is used. (scripts/install.sh can also build it for you.)
set -u

# shellcheck source=lib.sh
source "$(dirname "$(readlink -f "$0")")/lib.sh"

PKG=
while [[ $# -gt 0 ]]; do
  case $1 in
    --package) PKG=${2:?--package needs a file}; shift 2 ;;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -n $PKG ]] || PKG=$(fp_newest_package)
LOG="$HOME/fp-smoke/install.log"

if [[ $EUID -eq 0 ]]; then
  echo "Run this as your normal user; it calls sudo itself." >&2
  exit 2
fi
[[ -n $PKG && -f $PKG ]] || { echo "Missing package: ${PKG:-(none found in $PACKAGING_DIR)}" >&2; exit 2; }
PKG=$(readlink -f "$PKG")
mkdir -p "$(dirname "$LOG")"
exec > >(tee "$LOG") 2>&1
sudo -v || exit 2

echo "-- Install packages --"
echo "Package: $PKG"
sudo pacman -U --needed --noconfirm "$PKG" || { echo "libfprint package install failed" >&2; exit 1; }
sudo pacman -S --needed --noconfirm fprintd || { echo "fprintd install failed" >&2; exit 1; }
sudo systemctl restart fprintd
sleep 1
echo
echo "Devices fprintd sees:"
sudo fprintd-list "$USER" || { echo "fprintd cannot see the reader; see: journalctl -u fprintd -b" >&2; exit 1; }

echo
echo "-- Enroll your RIGHT INDEX finger --"
echo "Touch and lift repeatedly until it says enroll-completed."
pause "Ready?"
sudo fprintd-enroll -f right-index-finger "$USER" || { echo "enroll failed; see: journalctl -u fprintd -b" >&2; exit 1; }

echo
echo "-- Verify with your RIGHT INDEX finger (expect verify-match) --"
pause "Ready?"
sudo fprintd-verify -f right-index-finger "$USER"
rc=$?
echo "exit code: $rc"
if [[ $rc -ne 0 ]]; then
  read -r -p "The matching check did not report success. Continue anyway? [y/N] " answer
  [[ ${answer,,} == y* ]] || { echo "Stopping. See: journalctl -u fprintd -b" >&2; exit 1; }
fi

echo
echo "-- Verify with a DIFFERENT finger, e.g. left index (expect verify-no-match) --"
echo "Do not pick anything; just touch the sensor with the wrong finger."
pause "Ready?"
sudo fprintd-verify -f right-index-finger "$USER"
echo "exit code: $?  (non-zero / verify-no-match is the expected result here)"

echo
echo "Done. Log saved to $LOG"

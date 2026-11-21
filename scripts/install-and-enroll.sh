#!/usr/bin/env bash
# Install the patched libfprint package plus stock fprintd, enroll one finger
# through fprintd, and check a match and a non-match.
# Does NOT touch PAM, sudo, or the lock screen.
set -u

PKG=/home/t/Work/cros-fingerprint/packaging/libfprint-crfpmoc-local-1.94.10-1-x86_64.pkg.tar.zst
LOG="$HOME/fp-smoke/install.log"

if [[ $EUID -eq 0 ]]; then
  echo "Run this as your normal user; it calls sudo itself." >&2
  exit 2
fi
[[ -f $PKG ]] || { echo "Missing package: $PKG" >&2; exit 2; }
mkdir -p "$(dirname "$LOG")"
exec > >(tee "$LOG") 2>&1
sudo -v || exit 2

pause() { read -r -p $'\n'"$1 Press Enter to continue (Ctrl-C to stop)... " _; }

echo "== Step 1 of 4: install packages =="
sudo pacman -U --needed --noconfirm "$PKG" || { echo "libfprint package install failed" >&2; exit 1; }
sudo pacman -S --needed --noconfirm fprintd || { echo "fprintd install failed" >&2; exit 1; }
sudo systemctl restart fprintd
sleep 1
echo
echo "Devices fprintd sees:"
sudo fprintd-list "$USER" || { echo "fprintd cannot see the reader; see: journalctl -u fprintd -b" >&2; exit 1; }

echo
echo "== Step 2 of 4: enroll your RIGHT INDEX finger =="
echo "Touch and lift repeatedly until it says enroll-completed."
pause "Ready?"
sudo fprintd-enroll -f right-index-finger "$USER" || { echo "enroll failed; see: journalctl -u fprintd -b" >&2; exit 1; }

echo
echo "== Step 3 of 4: verify with your RIGHT INDEX finger (expect verify-match) =="
pause "Ready?"
sudo fprintd-verify -f right-index-finger "$USER"
echo "exit code: $?"

echo
echo "== Step 4 of 4: verify with a DIFFERENT finger, e.g. left index (expect verify-no-match) =="
echo "Do not pick anything; just touch the sensor with the wrong finger."
pause "Ready?"
sudo fprintd-verify -f right-index-finger "$USER"
echo "exit code: $?"

echo
echo "Done. Log saved to $LOG"

#!/usr/bin/env bash
# One-time move from a build that used the driver's fixed public seed to one
# that uses a random per-machine key (kept in /var/lib/fprint/.crfpmoc/key).
#
# Prints enrolled under the fixed seed cannot be decrypted with the new key,
# so this deletes them, installs the new package, and then you must REBOOT
# (the reader only forgets its old seed on reboot) and enroll again with
# scripts/install-and-enroll.sh. Your password keeps working throughout.
#
# usage: migrate-to-machine-key.sh [--package FILE.pkg.tar.zst]
set -u

# shellcheck source=lib.sh
source "$(dirname "$(readlink -f "$0")")/lib.sh"

PKG=
while [[ $# -gt 0 ]]; do
  case $1 in
    --package) PKG=${2:?--package needs a file}; shift 2 ;;
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
sudo -v || exit 2

if sudo test -e /var/lib/fprint/.crfpmoc/key; then
  echo "A machine key already exists; this machine has been migrated. Nothing to do." >&2
  exit 0
fi

echo "Installed now: $(pacman -Q "$PKG_NAME" 2>/dev/null || echo "(not installed)")"
echo "Will install:  $PKG"
echo
echo "Users with enrolled prints (all of them must be deleted):"
mapfile -t USERS < <(sudo find /var/lib/fprint -mindepth 1 -maxdepth 1 -type d -not -name '.*' -printf '%f\n' 2>/dev/null)
if [[ ${#USERS[@]} -eq 0 ]]; then
  echo "  (none)"
else
  printf '  %s\n' "${USERS[@]}"
fi
echo
read -r -p "Delete these prints and install the new package? [y/N] " answer
[[ ${answer,,} == y* ]] || { echo "Nothing changed."; exit 1; }

for u in "${USERS[@]}"; do
  sudo fprintd-delete "$u" || { echo "Could not delete prints for $u; stopping before installing." >&2; exit 1; }
done
sudo pacman -U --noconfirm "$PKG" || { echo "package install failed" >&2; exit 1; }
sudo systemctl stop fprintd

echo
echo "Installed now: $(pacman -Q "$PKG_NAME")"
echo
echo "Next:"
echo "  1. Reboot now. Log in and use sudo with your password; there is no print yet."
echo "  2. Run:  $SCRIPTS_DIR/install-and-enroll.sh"
echo "     It enrolls your finger under the new machine key and checks it."

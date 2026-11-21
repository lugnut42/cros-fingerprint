#!/usr/bin/env bash
# One entry point for the whole setup: build (or pick) the patched libfprint
# package, install it with fprintd, enroll a finger, then enable fingerprint
# for sudo, polkit prompts and (on Omarchy) the lock screen.
#
# Run as your normal user; it calls sudo where needed. Every stage pauses first.
# The steps themselves live in install-and-enroll.sh and enable-fingerprint-auth.sh.
set -u

# shellcheck source=lib.sh
source "$(dirname "$(readlink -f "$0")")/lib.sh"

usage() {
  cat <<'EOF'
usage: scripts/install.sh [--package FILE.pkg.tar.zst]
       scripts/install.sh --undo
       scripts/install.sh --uninstall

  (no option)   Walk through: locate or build the package, install it and
                fprintd, enroll a finger and check match / no-match, then
                enable fingerprint for sudo, polkit and the lock screen.
  --package F   Use this package file instead of the newest
                packaging/libfprint-crfpmoc-local-*.pkg.tar.zst. If neither
                exists, the package is built with makepkg in packaging/.
  --undo        Put sudo, polkit and the lock screen back to password only.
                Packages and enrolled prints are left alone.
  --uninstall   --undo, then remove fprintd and libfprint-crfpmoc-local, and
                (only if you answer yes when asked) delete enrolled prints.
  -h, --help    Show this text.
EOF
}

banner() { printf '\n==========================================\n== %s\n==========================================\n' "$1"; }

MODE=install
PKG=
while [[ $# -gt 0 ]]; do
  case $1 in
    --package) PKG=${2:?--package needs a file}; shift 2 ;;
    --undo) MODE=undo; shift ;;
    --uninstall) MODE=uninstall; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [[ $MODE != install && -n $PKG ]]; then
  echo "--package only applies to the install flow." >&2; exit 2
fi

if [[ $EUID -eq 0 ]]; then
  echo "Run this as your normal user; it calls sudo itself." >&2
  exit 2
fi

AUTH=$SCRIPTS_DIR/enable-fingerprint-auth.sh

if [[ $MODE == undo ]]; then
  banner "Undo: PAM and lock screen"
  exec "$AUTH" --undo
fi

if [[ $MODE == uninstall ]]; then
  banner "Uninstall 1 of 3: PAM and lock screen back to password only"
  pause "This restores password-only sudo/polkit/lock screen."
  "$AUTH" --undo || { echo "Undo failed; not removing packages." >&2; exit 1; }

  banner "Uninstall 2 of 3: enrolled fingerprints"
  read -r -p "Delete your enrolled fingerprints? This cannot be undone. [y/N] " answer
  if [[ ${answer,,} == y* ]]; then
    sudo fprintd-delete "$USER" ||
      echo "Could not delete prints through fprintd; look in /var/lib/fprint afterwards." >&2
  else
    echo "Keeping enrolled prints."
  fi

  banner "Uninstall 3 of 3: remove packages"
  pause "About to run: pacman -Rns fprintd $PKG_NAME"
  sudo pacman -Rns fprintd "$PKG_NAME" ||
    { echo "Package removal failed." >&2; exit 1; }
  echo
  echo "Removed. Stock libfprint is not reinstalled automatically; install it with"
  echo "pacman -S libfprint if something else needs it."
  exit 0
fi

# ---- install flow ----

banner "Step 1 of 4: locate the package"
if [[ -n $PKG ]]; then
  [[ -f $PKG ]] || { echo "No such package file: $PKG" >&2; exit 2; }
else
  PKG=$(fp_newest_package)
  if [[ -n $PKG ]]; then
    echo "Using existing build: $PKG"
  else
    echo "No built package found in $PACKAGING_DIR; building it with makepkg."
    # The recipe reads LIBFPRINT_SRC as the patched source checkout.
    if [[ -z ${LIBFPRINT_SRC:-} ]]; then
      export LIBFPRINT_SRC=$REPO_ROOT/.reference-cache/libfprint-checkout/acd407-libfprint-4df0591
    fi
    [[ -d $LIBFPRINT_SRC ]] ||
      { echo "Source checkout not found: $LIBFPRINT_SRC (set LIBFPRINT_SRC or use --package)." >&2; exit 2; }
    pause "makepkg may ask for your sudo password to install build dependencies."
    (cd "$PACKAGING_DIR" && makepkg -f -s) || { echo "makepkg failed." >&2; exit 1; }
    PKG=$(fp_newest_package)
    [[ -n $PKG ]] || { echo "makepkg finished but produced no package." >&2; exit 1; }
  fi
fi
PKG=$(readlink -f "$PKG")
echo "Package: $PKG"

banner "Step 2 of 4: install packages, enroll a finger, check match and no-match"
echo "Installs the package and fprintd (replacing the stock libfprint),"
echo "then enrolls one finger and checks it."
pause "Ready?"
"$SCRIPTS_DIR/install-and-enroll.sh" --package "$PKG" ||
  { echo "Install/enroll did not finish. Nothing about sudo, polkit or the lock screen was changed." >&2; exit 1; }

banner "Step 3 of 4: enable fingerprint for sudo, polkit and the lock screen"
echo "This edits /etc/pam.d/sudo (with a backup), creates /etc/pam.d/polkit-1,"
echo "and on Omarchy runs its lock-screen setup. Password login keeps working."
echo "You will be asked to test sudo in a second terminal before it goes further."
pause "Ready?"
"$AUTH" || { echo "Enabling was stopped or rolled back; see the messages above." >&2; exit 1; }

banner "Step 4 of 4: done"
echo "Undo any time:  $SCRIPTS_DIR/install.sh --undo"
echo "Remove it all:  $SCRIPTS_DIR/install.sh --uninstall"

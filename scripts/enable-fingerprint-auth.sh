#!/usr/bin/env bash
# Turn on fingerprint for sudo, polkit prompts, and the Omarchy lock screen.
# Uses the same PAM lines Omarchy's own fingerprint setup writes, so Omarchy's
# removal script and migrations recognise them. The login screen is untouched.
#
# The script stays root while you test, and restores the old sudo config if
# you say the test failed. Undo later with: enable-fingerprint-auth.sh --undo
set -u

SELF=$(readlink -f "$0")
BACKUP=/etc/pam.d/sudo.before-fingerprint
GATE='auth      [success=1 default=ignore] pam_exec.so quiet /usr/bin/omarchy-hw-laptop-closed'
FPRINT='auth      sufficient pam_fprintd.so'

if [[ $EUID -ne 0 ]]; then
  exec sudo OMARCHY_PATH=/usr/share/omarchy "$SELF" "$@"
fi
export OMARCHY_PATH=${OMARCHY_PATH:-/usr/share/omarchy}
USER_NAME=${SUDO_USER:?run via sudo from your own account}

undo() {
  echo "Restoring password-only authentication..."
  if [[ -f $BACKUP ]]; then
    cp -a "$BACKUP" /etc/pam.d/sudo
  else
    sed -i -e '/pam_fprintd\.so/d' -e '/omarchy-hw-laptop-closed/d' /etc/pam.d/sudo
  fi
  rm -f /etc/pam.d/polkit-1 /etc/pam.d/omarchy-lock-fingerprint \
        /usr/lib/systemd/system-sleep/fprintd-resume \
        /etc/systemd/system/fprintd.service.d/10-stop-timeout.conf
  rmdir --ignore-fail-on-non-empty /etc/systemd/system/fprintd.service.d 2>/dev/null
  systemctl daemon-reload
  echo "Done. sudo, polkit and the lock screen are back to password only."
}

if [[ ${1:-} == --undo ]]; then
  undo
  exit 0
fi

[[ -x /usr/lib/security/pam_fprintd.so || -f /usr/lib/security/pam_fprintd.so ]] ||
  { echo "pam_fprintd.so is not installed." >&2; exit 2; }
[[ -x /usr/bin/omarchy-hw-laptop-closed ]] ||
  { echo "/usr/bin/omarchy-hw-laptop-closed is missing." >&2; exit 2; }
/usr/bin/fprintd-list "$USER_NAME" | grep -qE '^[[:space:]]*-[[:space:]]*#[0-9]+:' ||
  { echo "No finger enrolled for $USER_NAME; run install-and-enroll.sh first." >&2; exit 2; }
if [[ -e /etc/pam.d/polkit-1 ]]; then
  echo "/etc/pam.d/polkit-1 already exists; not overwriting it. Stopping." >&2
  exit 2
fi
if grep -q pam_fprintd /etc/pam.d/sudo; then
  echo "/etc/pam.d/sudo already mentions pam_fprintd. Stopping." >&2
  exit 2
fi

echo "== Step 1 of 3: sudo =="
cp -a /etc/pam.d/sudo "$BACKUP"
{ head -n1 "$BACKUP" | grep -q '^#%PAM' && head -n1 "$BACKUP"
  echo "$GATE"
  echo "$FPRINT"
  if head -n1 "$BACKUP" | grep -q '^#%PAM'; then tail -n +2 "$BACKUP"; else cat "$BACKUP"; fi
} > /etc/pam.d/sudo.new
chmod --reference="$BACKUP" /etc/pam.d/sudo.new
mv /etc/pam.d/sudo.new /etc/pam.d/sudo

echo
echo "Now test, leaving THIS terminal open:"
echo "  1. Open a NEW terminal and run:   sudo -k; sudo true"
echo "     Touch the sensor with your enrolled finger. It should succeed with no password."
echo "  2. Run it again and touch with a WRONG finger (or wait)."
echo "     It should fall back to asking for your password, and the password should work."
read -r -p "Did both work? [y/N] " answer
if [[ ${answer,,} != y* ]]; then
  cp -a "$BACKUP" /etc/pam.d/sudo
  echo "Restored the original sudo config. Nothing else was changed."
  exit 1
fi

echo
echo "== Step 2 of 3: polkit prompts =="
cat > /etc/pam.d/polkit-1 <<EOF
$GATE
$FPRINT
auth      include system-auth

account   include system-auth
password  include system-auth
session   include system-auth
EOF
chmod 644 /etc/pam.d/polkit-1
echo "Created /etc/pam.d/polkit-1"

echo
echo "== Step 3 of 3: lock screen =="
/usr/share/omarchy/bin/omarchy-apply-lock ||
  echo "omarchy-apply-lock reported an error; the lock screen may still be password only." >&2

echo
echo "Finished. Try it:"
echo "  - Lock the screen (Super+Ctrl+L) and unlock with your finger; then again with your password."
echo "  - Trigger a polkit prompt, e.g.:  pkexec true"
echo "To undo everything:  $SELF --undo"

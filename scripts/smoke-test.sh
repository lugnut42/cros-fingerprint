#!/usr/bin/env bash
# Live smoke test of the patched crfpmoc driver from the build tree.
# Enrolls one finger, then verifies with the same finger and a different one.
# Nothing is installed; logs and the encrypted print go to ~/fp-smoke.
set -u

EX=/home/t/Work/cros-fingerprint/build-libfprint-reference/examples
OUT="$HOME/fp-smoke"

if [[ $EUID -eq 0 ]]; then
  echo "Run this as your normal user; it calls sudo itself." >&2
  exit 2
fi
for bin in enroll verify; do
  [[ -x $EX/$bin ]] || { echo "Missing $EX/$bin - build the driver first." >&2; exit 2; }
done
if systemctl is-active --quiet fprintd; then
  echo "fprintd is running and may hold the reader. Stop it first: sudo systemctl stop fprintd" >&2
  exit 2
fi

mkdir -p "$OUT" && cd "$OUT" || exit 2
sudo -v || exit 2

run() { # run <example> <logfile>
  sudo env G_MESSAGES_DEBUG=all FP_DRIVERS_ALLOWLIST=crfpmoc "$EX/$1" 2>&1 | tee "$2"
  return "${PIPESTATUS[0]}"
}

pause() { read -r -p $'\n'"$1 Press Enter to continue (Ctrl-C to stop)... " _; }

echo "== Step 1 of 3: enroll =="
echo "Pick a finger when asked, then touch the sensor repeatedly until it"
echo "finishes (five or more touches). Answer yes when it offers to save."
pause "Ready?"
run enroll enroll.log
echo "enroll exit code: $?"
if [[ ! -s test-storage.variant ]]; then
  echo
  echo "No saved print found, so enrollment did not complete. See $OUT/enroll.log" >&2
  exit 1
fi

echo
echo "== Step 2 of 3: verify with the SAME finger (should match) =="
pause "Ready?"
run verify verify-same.log
echo "verify exit code: $?"

echo
echo "== Step 3 of 3: verify with a DIFFERENT finger (should NOT match) =="
pause "Ready?"
run verify verify-other.log
echo "verify exit code: $?"

echo
echo "Done. Logs are in $OUT:"
ls -1 "$OUT"

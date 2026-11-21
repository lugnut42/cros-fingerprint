# Shared helpers, sourced by the other scripts in this directory. Not executable.
# shellcheck shell=bash

# REPO_ROOT is the directory above scripts/, wherever the repository lives.
SCRIPTS_DIR=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
REPO_ROOT=$(dirname "$SCRIPTS_DIR")
PACKAGING_DIR=$REPO_ROOT/packaging
PKG_NAME=libfprint-crfpmoc-local

pause() { read -r -p $'\n'"$1 Press Enter to continue (Ctrl-C to stop)... " _; }

# Print the newest locally built package under packaging/, or nothing.
fp_newest_package() {
  local f best=
  for f in "$PACKAGING_DIR/$PKG_NAME"-*.pkg.tar.zst; do
    [[ -f $f ]] || continue
    if [[ -z $best || $f -nt $best ]]; then best=$f; fi
  done
  [[ -n $best ]] && printf '%s\n' "$best"
}

# True when this looks like an Omarchy install with the pieces the lock-screen
# step and the lid-closed PAM gate need.
fp_have_omarchy() {
  [[ -x /usr/share/omarchy/bin/omarchy-apply-lock && -x /usr/bin/omarchy-hw-laptop-closed ]]
}

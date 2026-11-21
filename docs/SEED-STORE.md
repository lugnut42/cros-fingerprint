# Offline seed store

Implemented 2026-09-27 in src/seed.c, API include/cros_fp_seed.h. This component
has no device transport and is not linked into the diagnostic. A separate, explicitly invoked seed/context experiment CLI is now built;
see SEED-CONTEXT-EXPERIMENT.md. No real seed was generated during development.

## Contract

The caller supplies a securely opened directory descriptor and expected owner
UID (root/0 in deployment; the current test user in offline tests). The store
requires exact mode 0700 and matching owner. Path traversal and directory creation can now use cfp_state_directory; see
STATE-LIFECYCLE.md. ACL/environment policy, account lifecycle, and process dump
suppression remain caller responsibilities. Do not pass a
descriptor obtained through an untrusted path and assume this API checked it.

Creation is explicit and requires an empty directory. Load never creates.
Every call opens a separate directory file description for a nonblocking
exclusive flock. Cooperative callers therefore serialize even after fork.
The lock does not defend against a malicious same-UID/root process.

Record `seed`: eight bytes `CFPSEED1`, 32 seed bytes, and 32 SHA-256 bytes over
the preceding 40 bytes (72 bytes total). SHA-256 comes from OpenSSL EVP. This
unkeyed checksum detects accidental corruption, not malicious modification;
the seed remains plaintext under filesystem access controls. Never log records.
This experimental format has no account/reader generation envelope yet.
Context IDs use the same storage machinery in a separate directory, with file
name `context`, staging name `.context.pending`, and magic `CFPCTX01`. A renamed
context record is rejected by the seed loader.

Production creation calls getrandom. Short/failing randomness, all-zero, and
all-0xff values are refused. Tests inject synthetic randomness. Load checks
exact length, magic/version, checksum, regular-file type, mode 0600, owner,
and a single hard link. O_NOFOLLOW rejects symlinks; O_NONBLOCK prevents FIFO
opens from hanging before type validation. No caller output changes on error.
Internal record copies are cleared explicitly; callers must erase returned
seeds. This is not a full audit of library temporaries, memory locking, or dumps.

## Publication and recovery

1. Validate and lock the private directory; reject any existing entry.
2. Generate the seed in memory and encode the checksummed record.
3. Exclusively create `.seed.pending`, write completely, fsync, and close it.
4. Publish `seed` using linkat, which cannot replace an existing destination.
5. Remove the staging link and fsync the directory before returning success.

A failed write/sync/publication can leave `.seed.pending`; load returns
CFP_STORE_RECOVERY if that name exists. Creation refuses the nonempty directory.
The component never deletes questionable state or silently generates a new
seed. An administrative recovery workflow remains to be implemented.

If publication finished but the final directory sync failed, creation returns
an error with no seed output. A later load validates the published record and
syncs both file and directory before returning it. Filesystem failure/power loss
may still remove an uncommitted name; this module does not claim otherwise.
Once context/template records exist, their presence prevents automatic creation.
Deletion of the entire state directory is indistinguishable from a new install
without an external registry; ordinary load still never generates secrets.

The original acceptance scenario mentioned rename. This implementation uses
no-replace hard-link publication instead. Both file and directory syncing are
necessary; [fsync documentation](https://man7.org/linux/man-pages/man2/fsync.2.html)
describes the directory requirement, and [linkat documentation](https://man7.org/linux/man-pages/man2/link.2.html)
specifies refusal to overwrite an existing name. Power-loss behavior has not
been tested on the target filesystem. Do not equate injected failures with a
crash-consistency proof.

## Next work

Secure directory setup and the initial recovery policy are implemented offline.
Add a persistent generation/account registry and supervised operation executor.
Only then wire an explicitly scoped provisioning operation to the controller.
The read-only diagnostic must continue to have no seed-store side effects.


## Provisioning envelope (2026-09-28)

The shared record implementation now also supports a separate 64-byte generation
plus seed payload, with its own tag, filename, and APIs. Existing experiment
seed/context records keep their original formats and behavior. The outer store
validates its account registry against that generation; see PROVISIONED-STORE.md.
This host-side envelope does not identify the physical controller or change the
legacy hardware restore CLI. Existing seed tests still exercise the shared
publication and failure paths.

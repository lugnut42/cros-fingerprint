/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_STATE_H
#define CROS_FP_STATE_H
#include "cros_fp_seed.h"
#include <stdint.h>
enum cfp_directory_action { CFP_DIRECTORY_OPEN, CFP_DIRECTORY_CREATE };
/* Resolve a relative path beneath a trusted directory descriptor. Validate the
 * anchor and every traversed directory; no symlinks, dot components, or empty
 * components. Ancestors must be root/owner-owned and not group/other writable.
 * The leaf must be owner-owned mode 0700. CREATE creates only a missing leaf;
 * never repairs permissions or creates ancestors. Output unchanged on error.
 * Deployment anchor: a trusted fd for /, owner 0, path var/lib/cros-fingerprint.
 */
enum cfp_store_status cfp_state_directory(int anchor, const char *path,
    uid_t owner, enum cfp_directory_action action, int *directory);
enum cfp_session_phase {
    CFP_SESSION_BLOCKED, CFP_SESSION_RESTORE_PENDING, CFP_SESSION_SEEDED
};
struct cfp_seed_session { uint64_t generation; enum cfp_session_phase phase; };
/* Zero-initialize. Begin a fresh ownership epoch with durable validated storage
 * and fresh controller status. This is policy, not authorization or I/O.
 * Returns the new phase. Already-set or unknown seeds always block a fresh epoch.
 */
enum cfp_session_phase cfp_seed_session_begin(struct cfp_seed_session *session,
    enum cfp_store_status storage, int status_known, int seed_set);
/* Completion is accepted only for the pending generation. A stale completion
 * never changes current state. SEEDED says nothing about context or matching. */
void cfp_seed_session_complete(struct cfp_seed_session *session,
    uint64_t generation, int success);
void cfp_seed_session_invalidate(struct cfp_seed_session *session);
#endif

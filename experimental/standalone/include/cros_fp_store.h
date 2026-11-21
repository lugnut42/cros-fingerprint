/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_STORE_H
#define CROS_FP_STORE_H
#include "cros_fp_provision.h"
#include "cros_fp_registry.h"
#include "cros_fp_state.h"
/* Private-by-contract, noncopyable handle. Keep stable until close; no use after
 * close, concurrent threads, or raw registry bypass while this owner is live.
 * Descriptors and secrets belong to this handle. No automatic hardware access.
 */
struct cfp_store {
    int lock_fd, accounts_fd;
    uid_t owner;
    struct cfp_provision provision;
    struct cfp_seed_session session;
};
/* Trusted already-open 0700 root. create=1 explicitly provisions an empty root;
 * create=0 validates existing provision+accounts only, never repairs/creates.
 * Locks root until close, checks generation, returns a BLOCKED device session.
 * Out is unchanged on failure. Do not overwrite a live handle with open.
 * Optional injection ops must remain trusted; NULL uses OS calls.
 */
enum cfp_store_status cfp_store_open(int root,uid_t owner,int create,
    const struct cfp_seed_ops *seed_ops,const struct cfp_registry_ops *registry_ops,
    struct cfp_store *out);
/* REGISTER/LOOKUP/RETIRE only; generation always comes from provision.
 * Caller has already authorized account selection. RETIRE invalidates the host
 * session BEFORE attempting storage, even on failure. It is not device cleanup.
 */
enum cfp_store_status cfp_store_account(struct cfp_store *store,
    const struct cfp_registry_ops *ops,enum cfp_registry_action action,
    uint64_t uid,const char *name,const struct cfp_binding *expected,
    struct cfp_binding *out);
/* Invalidates session and wipes secret material. Stop/cancel all operations
 * before close; their session pointers must not outlive the handle. */
void cfp_store_close(struct cfp_store *store);
#endif

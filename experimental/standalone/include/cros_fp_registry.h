/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_REGISTRY_H
#define CROS_FP_REGISTRY_H
#include "cros_fp_binding.h"
#include "cros_fp_seed.h"
#define CFP_REGISTRY_MAX_ENTRIES 64
/* Trusted fault-injection seam; NULL selects OS calls. */
struct cfp_registry_ops {
    ssize_t (*random_bytes)(void *, size_t);
    ssize_t (*write_bytes)(int, const void *, size_t);
    int (*sync_fd)(int);
    int (*replace)(int, const char *, int, const char *);
};
enum cfp_registry_action {
    CFP_REGISTRY_INITIALIZE, CFP_REGISTRY_REGISTER,
    CFP_REGISTRY_LOOKUP, CFP_REGISTRY_RETIRE, CFP_REGISTRY_CHECK
};
struct cfp_registry_request {
    enum cfp_registry_action action;
    uint8_t reader_generation[32]; /* trusted provisioning identity */
    uint64_t uid;
    const char *name; /* 1..63 ASCII letters/digits/underscore/hyphen/dot */
    const struct cfp_binding *expected; /* RETIRE: exact current binding */
};
/* Caller supplies securely opened, dedicated 0700 directory (owner 0 in
 * deployment) and trusted account selection. This is NOT authorization.
 * INITIALIZE requires an empty directory. Other actions never create a registry.
 * REGISTER rejects active UID/name reuse; RETIRE retains a permanent tombstone.
 * CHECK validates the snapshot and generation, including an empty registry.
 * LOOKUP requires exact UID+name and only returns active entries. No automatic
 * account deletion detection, rename, device access, or template cleanup.
 * Mutations/reads serialize with a separate nonblocking directory flock.
 * Out is required for REGISTER/LOOKUP and remains unchanged on all failures.
 * .registry.pending blocks all use after interrupted writes; preserve it.
 * An IO result after publication is uncertain: reload/inspect, never assume the
 * old state or retry provisioning blindly. Caller must serialize retirement
 * with device operations and invalidate cached bindings/session state.
 */
enum cfp_store_status cfp_registry_apply(int directory, uid_t owner,
    const struct cfp_registry_ops *ops, const struct cfp_registry_request *request,
    struct cfp_binding *out);
#endif

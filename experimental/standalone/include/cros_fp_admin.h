/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_ADMIN_H
#define CROS_FP_ADMIN_H
#include "cros_fp_store.h"
#define CFP_PROTOTYPE_STATE "var/lib/cros-fingerprint-prototype"
/* Trusted build-time platform seam, never populated from user input in the
 * production executable. anchor must identify a trusted directory. Account
 * lookup returns 1 only for an existing OS name with exactly the requested UID.
 * NULL storage ops select system calls. owner is 0 in production.
 * CLI must exit after this call (one-shot supervisor contract).
 */
struct cfp_admin_platform {
    const char *anchor,*relative_path;
    uid_t owner;
    int (*account_exists)(uint64_t uid,const char *name);
    const struct cfp_seed_ops *seeds;
    const struct cfp_registry_ops *accounts;
    unsigned timeout_ms;
};
int cfp_admin_cli(int argc,char **argv,const struct cfp_admin_platform *platform);
#endif

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_SMOKE_H
#define CROS_FP_SMOKE_H
#include "cros_fp_diagnostic.h"
#include "cros_fp_seed.h"
struct cfp_smoke_clock {
    int64_t (*now_ms)(void *);
    int (*wait_ms)(void *, unsigned);
    void *context;
};
/* Seed/context-only experiment. No capture/template/PAM commands. Caller owns
 * both private directories and an exclusive, supervised transport. initialize
 * must be exactly 0 (load only) or 1 (explicit creation). No cleanup deletes
 * private state after errors; a partial initialization requires inspection.
 */
enum cfp_status cfp_smoke_run(const struct cfp_transport *transport,
    int seed_directory,int context_directory,uid_t owner,int initialize,
    const struct cfp_seed_ops *ops,const struct cfp_smoke_clock *clock,
    struct cfp_report *report);
#endif

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_PREPARE_H
#define CROS_FP_PREPARE_H
#include "cros_fp_store.h"
#include "cros_fp_smoke.h"
/* Explicit seed/context-only preparation for a trusted, previously registered
 * account. Caller owns the store and the device, suppresses dumps and runs in
 * a supervised worker. No registration, capture, templates, events or resets.
 * Requires target metadata, empty reader, known-unset seed, supported commands.
 * Deadline includes storage and all I/O; each blocking syscall still requires
 * parent supervision. Clock callbacks are trusted and monotonic. Cancellation
 * is session invalidation. On failure the session is blocked. Report contains
 * only whitelisted diagnostic metadata, never identity or seed bytes.
 */
enum cfp_status cfp_prepare_account(struct cfp_store *store,
    const struct cfp_transport *transport,uint64_t uid,const char *name,
    const struct cfp_registry_ops *registry_ops,const struct cfp_smoke_clock *clock,
    unsigned timeout_ms,struct cfp_report *report);
#endif

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_RESTORE_H
#define CROS_FP_RESTORE_H
#include "cros_fp.h"
#include "cros_fp_seed.h"
#include "cros_fp_state.h"
/* Blocking state-changing operation. Caller holds exclusive device ownership,
 * supervises execution, suppresses dumps, and supplies the same transport for
 * the entire session. It loads existing storage only; never provisions files.
 * Capability/status queries precede FP_SEED. Any failure leaves session blocked.
 * store_result reports storage failures separately. No bytes are logged.
 */
enum cfp_status cfp_restore_seed(const struct cfp_transport *transport,
    int directory, uid_t owner, const struct cfp_seed_ops *ops,
    struct cfp_seed_session *session, enum cfp_store_status *store_result);
/* Trusted composition entry point: caller already validated durable storage,
 * owns its lifetime, and must erase seed afterwards. Same device/supervision
 * obligations as above. Never use client-provided bytes or infer seed identity
 * from an already-set status. No storage or provisioning occurs here. */
enum cfp_status cfp_restore_validated_seed(const struct cfp_transport *transport,
    const uint8_t seed[32],struct cfp_seed_session *session);
#endif

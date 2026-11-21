/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_CONTEXT_H
#define CROS_FP_CONTEXT_H
#include "cros_fp.h"
#include "cros_fp_state.h"
enum cfp_context_phase { CFP_CONTEXT_IDLE, CFP_CONTEXT_START, CFP_CONTEXT_POLL,
    CFP_CONTEXT_DONE, CFP_CONTEXT_FAILED };
struct cfp_context_switch {
    enum cfp_context_phase phase;
    enum cfp_status result;
    struct cfp_seed_session *session;
    uint64_t generation, started_ms, last_ms, last_poll_ms, timeout_ms;
    uint8_t user_id[32];
};
/* Zero-initialize before first use. No I/O at begin. Caller must own the device,
 * verify FP_CONTEXT v1 support, and supply a live seeded session and a trusted
 * nonzero user ID. The session and transport must outlive the operation.
 * tick performs at most one exchange, with 100 ms minimum between polls.
 * Caller supplies monotonic time; blocking exchanges still need supervision.
 */
enum cfp_status cfp_context_begin(struct cfp_context_switch *operation,
    struct cfp_seed_session *session, const uint8_t user_id[32],
    uint64_t now_ms, uint64_t timeout_ms);
enum cfp_status cfp_context_tick(struct cfp_context_switch *operation,
    const struct cfp_transport *transport, uint64_t now_ms);
void cfp_context_cancel(struct cfp_context_switch *operation);
#endif

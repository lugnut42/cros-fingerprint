/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_DIAGNOSTIC_H
#define CROS_FP_DIAGNOSTIC_H
#include "cros_fp.h"
struct cfp_report {
    enum cfp_status status;
    int error_number;
    uint32_t mode;
    uint8_t info_version;
    struct cfp_sensor_info info;
    struct cfp_encryption_status encryption;
};
void cfp_collect(const struct cfp_transport *transport, struct cfp_report *report);
/* One-shot CLI supervisor. The caller must exit after this call: a worker
 * stuck in uninterruptible kernel sleep may outlive SIGKILL and be reparented
 * on exit. The parent never waits indefinitely for such a worker.
 * Returns 0 for a complete report, 124 for deadline, 125 for worker/IPC error.
 */
int cfp_supervise(void (*worker)(void *, struct cfp_report *), void *context,
    unsigned timeout_ms, struct cfp_report *report);
#endif

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_EVENTS_H
#define CROS_FP_EVENTS_H
#include "cros_fp.h"
#include <sys/types.h>
#include <sys/ioctl.h>
#define CFP_IOCTL_EVENT_MASK _IO(0xec, 2)
#define CFP_FINGERPRINT_EVENT_MASK (1UL << 5)
#define CFP_EVENT_DRAIN_MAX_READS 1024
struct cfp_event_ops {
    int (*get_flags)(int fd);
    int (*set_mask)(int fd,unsigned long request,unsigned long mask);
    ssize_t (*read_event)(int fd,void *buffer,size_t capacity);
};
struct cfp_events {
    int fd,ready,error_number;
    const struct cfp_event_ops *ops;
};
extern const struct cfp_event_ops cfp_system_event_ops;
/* Borrow a freshly opened, exclusively owned descriptor. Require O_NONBLOCK;
 * subscribe only to fingerprint events. No EC commands. Do not share/duplicate
 * this descriptor, alter its flags/mask, or close it while the handle is used.
 * Close the descriptor after any failure; this API never owns/closes it.
 * Run inside a supervised worker: a syscall can exceed a host-side time bound.
 */
enum cfp_status cfp_events_open(struct cfp_events *events,int fd,
    const struct cfp_event_ops *ops);
/* Discard complete fingerprint events until EAGAIN, within max_reads syscall
 * attempts (including the final EAGAIN). Success means this descriptor's queue
 * was empty at that instant, NOT that firmware stopped or future events are
 * fresh. Caller must separately prove mode/context cleanup before using results.
 * No retries on EINTR. Failure invalidates the handle. No payload is exposed.
 */
enum cfp_status cfp_events_drain(struct cfp_events *events,unsigned max_reads);
#endif

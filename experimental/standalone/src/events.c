/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_events.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
static int get_flags(int fd) { return fcntl(fd,F_GETFL); }
static int set_mask(int fd,unsigned long request,unsigned long mask)
{ return ioctl(fd,request,mask); }
const struct cfp_event_ops cfp_system_event_ops={get_flags,set_mask,read};
enum cfp_status cfp_events_open(struct cfp_events *e,int fd,const struct cfp_event_ops *ops)
{
    if(!e) return CFP_INVALID_ARGUMENT;
    *e=(struct cfp_events){.fd=fd,.ops=ops};
    if(fd<0 || !ops || !ops->get_flags || !ops->set_mask || !ops->read_event)
        return CFP_INVALID_ARGUMENT;
    int flags=ops->get_flags(fd);
    if(flags<0) { e->error_number=errno; return CFP_TRANSPORT_ERROR; }
    if(!(flags&O_NONBLOCK)) return CFP_INVALID_ARGUMENT;
    if(ops->set_mask(fd,CFP_IOCTL_EVENT_MASK,CFP_FINGERPRINT_EVENT_MASK)<0) {
        e->error_number=errno; return CFP_TRANSPORT_ERROR;
    }
    e->ready=1; return CFP_OK;
}
enum cfp_status cfp_events_drain(struct cfp_events *e,unsigned max_reads)
{
    if(!e) return CFP_INVALID_ARGUMENT;
    int ready=e->ready;
    e->ready=0; e->error_number=0;
    if(!ready || e->fd<0 || !e->ops || !e->ops->read_event ||
        !max_reads || max_reads>CFP_EVENT_DRAIN_MAX_READS) return CFP_INVALID_ARGUMENT;
    for(unsigned i=0;i<max_reads;i++) {
        /* Five bytes are expected. The extra byte detects overlong records:
         * the kernel discards the remainder of a record on a short read. */
        uint8_t event[6]={0};
        ssize_t n=e->ops->read_event(e->fd,event,sizeof(event));
        int error=errno;
        int valid=n==5 && event[0]==5;
        volatile uint8_t *bytes=event;
        for(size_t j=0;j<sizeof(event);j++) bytes[j]=0;
        if(n<0) {
            if(error==EAGAIN || error==EWOULDBLOCK) { e->ready=1; return CFP_OK; }
            e->error_number=error; return CFP_TRANSPORT_ERROR;
        }
        if(!valid) return CFP_MALFORMED_RESPONSE;
    }
    return CFP_LIMIT_EXCEEDED;
}

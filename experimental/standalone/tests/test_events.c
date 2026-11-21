/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_events.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int flags,flags_error,mask_error,read_error;
static unsigned flags_calls,mask_calls,read_calls,queued;
static ssize_t event_size;
static uint8_t event_type;
static int get_flags(int fd)
{ CHECK(fd==42); flags_calls++; errno=flags_error; return flags_error?-1:flags; }
static int set_mask(int fd,unsigned long request,unsigned long mask)
{
    CHECK(fd==42 && request==CFP_IOCTL_EVENT_MASK && mask==(1UL<<5));
    mask_calls++; errno=mask_error; return mask_error?-1:0;
}
static ssize_t read_event(int fd,void *p,size_t n)
{
    CHECK(fd==42 && n==6); read_calls++;
    if(read_error) { errno=read_error; return -1; }
    if(!queued) { errno=EAGAIN; return -1; }
    queued--; memset(p,0xa5,n); ((uint8_t *)p)[0]=event_type;
    return event_size;
}
static const struct cfp_event_ops ops={get_flags,set_mask,read_event};
static struct cfp_events events;
static void reset(void)
{
    flags=O_RDONLY|O_NONBLOCK; flags_error=mask_error=read_error=0;
    flags_calls=mask_calls=read_calls=queued=0; event_size=5; event_type=5;
    events=(struct cfp_events){0};
}
static void open_events(void)
{ CHECK(cfp_events_open(&events,42,&ops)==CFP_OK && events.ready); }
int main(void)
{
    reset(); open_events();
    CHECK(flags_calls==1 && mask_calls==1 && read_calls==0);
    CHECK(cfp_events_drain(&events,1)==CFP_OK && read_calls==1);
    reset(); open_events(); queued=3;
    CHECK(cfp_events_drain(&events,4)==CFP_OK && read_calls==4 && events.ready);
    reset(); open_events(); queued=CFP_EVENT_DRAIN_MAX_READS-1;
    CHECK(cfp_events_drain(&events,CFP_EVENT_DRAIN_MAX_READS)==CFP_OK);
    CHECK(read_calls==CFP_EVENT_DRAIN_MAX_READS);
    reset(); open_events(); queued=4;
    CHECK(cfp_events_drain(&events,4)==CFP_LIMIT_EXCEEDED && read_calls==4 && !events.ready);
    CHECK(cfp_events_drain(&events,4)==CFP_INVALID_ARGUMENT && read_calls==4);
    for(int n=0;n<=7;n++) if(n!=5) {
        reset(); open_events(); queued=1; event_size=n;
        CHECK(cfp_events_drain(&events,4)==CFP_MALFORMED_RESPONSE && read_calls==1 && !events.ready);
    }
    reset(); open_events(); queued=1; event_type=4;
    CHECK(cfp_events_drain(&events,4)==CFP_MALFORMED_RESPONSE && !events.ready);
    reset(); open_events(); read_error=EWOULDBLOCK;
    CHECK(cfp_events_drain(&events,1)==CFP_OK && events.ready && !events.error_number);
    int errors[]={EINTR,EIO,ENODEV,EBADF};
    for(size_t i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        reset(); open_events(); read_error=errors[i];
        CHECK(cfp_events_drain(&events,4)==CFP_TRANSPORT_ERROR);
        CHECK(events.error_number==errors[i] && read_calls==1 && !events.ready);
        reset(); flags_error=errors[i];
        CHECK(cfp_events_open(&events,42,&ops)==CFP_TRANSPORT_ERROR);
        CHECK(events.error_number==errors[i] && !mask_calls && !events.ready);
        reset(); mask_error=errors[i];
        CHECK(cfp_events_open(&events,42,&ops)==CFP_TRANSPORT_ERROR);
        CHECK(events.error_number==errors[i] && !read_calls && !events.ready);
    }
    reset(); flags=O_RDONLY;
    CHECK(cfp_events_open(&events,42,&ops)==CFP_INVALID_ARGUMENT && !mask_calls && !events.ready);
    reset();
    CHECK(cfp_events_open(NULL,42,&ops)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_events_open(&events,-1,&ops)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_events_open(&events,42,NULL)==CFP_INVALID_ARGUMENT);
    struct cfp_event_ops missing=ops; missing.read_event=NULL;
    CHECK(cfp_events_open(&events,42,&missing)==CFP_INVALID_ARGUMENT);
    CHECK(!flags_calls && !mask_calls && !read_calls);
    open_events();
    CHECK(cfp_events_drain(&events,0)==CFP_INVALID_ARGUMENT && !events.ready && !read_calls);
    open_events();
    CHECK(cfp_events_drain(&events,CFP_EVENT_DRAIN_MAX_READS+1)==CFP_INVALID_ARGUMENT && !read_calls);
    CHECK(cfp_events_drain(NULL,1)==CFP_INVALID_ARGUMENT);
    puts("Fingerprint event subscription/drain checks passed");
}

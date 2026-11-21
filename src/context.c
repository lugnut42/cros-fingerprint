/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_context.h"
#include <string.h>
static void erase(void *data,size_t size)
{ volatile uint8_t *p=data; while(size--) *p++=0; }
static enum cfp_status finish(struct cfp_context_switch *o,enum cfp_status result)
{
    erase(o->user_id,sizeof(o->user_id));
    o->result=result;
    o->phase=result==CFP_OK?CFP_CONTEXT_DONE:CFP_CONTEXT_FAILED;
    if(result!=CFP_OK && o->session && o->session->generation==o->generation)
        cfp_seed_session_invalidate(o->session);
    return result;
}
enum cfp_status cfp_context_begin(struct cfp_context_switch *o,
    struct cfp_seed_session *s,const uint8_t id[32],uint64_t now,uint64_t timeout)
{
    if(!o || !s || !id || !timeout || s->phase!=CFP_SESSION_SEEDED ||
        o->phase==CFP_CONTEXT_START || o->phase==CFP_CONTEXT_POLL)
        return CFP_INVALID_ARGUMENT;
    unsigned nonzero=0;
    for(unsigned i=0;i<32;i++) nonzero|=id[i];
    if(!nonzero) return CFP_INVALID_ARGUMENT;
    *o=(struct cfp_context_switch){.phase=CFP_CONTEXT_START,.result=CFP_PENDING,
        .session=s,.generation=s->generation,.started_ms=now,.last_ms=now,
        .last_poll_ms=now,.timeout_ms=timeout};
    memcpy(o->user_id,id,32);
    return CFP_PENDING;
}
enum cfp_status cfp_context_tick(struct cfp_context_switch *o,
    const struct cfp_transport *t,uint64_t now)
{
    if(!o || o->phase==CFP_CONTEXT_IDLE) return CFP_INVALID_ARGUMENT;
    if(o->phase==CFP_CONTEXT_FAILED) return o->result;
    if(!o->session || o->session->generation!=o->generation ||
        o->session->phase!=CFP_SESSION_SEEDED) return finish(o,CFP_CANCELLED);
    if(o->phase==CFP_CONTEXT_DONE) return o->result;
    if(!t || !t->exchange || now<o->last_ms) return finish(o,CFP_INVALID_ARGUMENT);
    o->last_ms=now;
    if(now-o->started_ms>=o->timeout_ms) return finish(o,CFP_TIMEOUT);
    if(o->phase==CFP_CONTEXT_POLL && now-o->last_poll_ms<100) return CFP_PENDING;
    uint8_t request[36]={0};
    request[0]=o->phase==CFP_CONTEXT_POLL?1:0;
    memcpy(request+4,o->user_id,32);
    size_t size=0; uint32_t result=0;
    /* State-changing transport, deliberately separate from readonly_exchange. */
    enum cfp_status status=t->exchange(t->context,0x406,1,request,sizeof(request),
        NULL,0,&size,&result);
    erase(request,sizeof(request));
    if(o->session->generation!=o->generation || o->session->phase!=CFP_SESSION_SEEDED)
        return finish(o,CFP_CANCELLED);
    if(status!=CFP_OK) return finish(o,status);
    if(result) {
        if(result==16 && o->phase==CFP_CONTEXT_POLL) {
            o->last_poll_ms=now; return CFP_PENDING;
        }
        return finish(o,CFP_CONTROLLER_ERROR);
    }
    if(size!=0) return finish(o,CFP_MALFORMED_RESPONSE);
    if(o->phase==CFP_CONTEXT_START) {
        o->phase=CFP_CONTEXT_POLL; o->last_poll_ms=now; return CFP_PENDING;
    }
    return finish(o,CFP_OK);
}
void cfp_context_cancel(struct cfp_context_switch *o)
{
    if(o && (o->phase==CFP_CONTEXT_START || o->phase==CFP_CONTEXT_POLL))
        (void)finish(o,CFP_CANCELLED);
}

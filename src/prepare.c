/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_prepare.h"
#include "cros_fp_restore.h"
#include "cros_fp_context.h"
struct guard {
    const struct cfp_transport *transport;
    const struct cfp_smoke_clock *clock;
    struct cfp_seed_session *session;
    int64_t start,last;
    unsigned timeout;
};
static enum cfp_status time_check(struct guard *g)
{
    int64_t now=g->clock->now_ms(g->clock->context);
    if(now<0 || now<g->last) return CFP_TRANSPORT_ERROR;
    g->last=now;
    return now-g->start>=g->timeout?CFP_TIMEOUT:CFP_OK;
}
static enum cfp_status exchange(void *context,uint16_t cmd,uint8_t ver,
    const uint8_t *request,size_t n,uint8_t *reply,size_t cap,size_t *size,uint32_t *result)
{
    struct guard *g=context;
    enum cfp_status status=time_check(g);
    if(status!=CFP_OK) return status;
    if(cmd!=8 && cmd!=0x402 && cmd!=0x403 && cmd!=0x409 && cmd!=0x408 && cmd!=0x406)
        return CFP_COMMAND_DENIED;
    uint64_t generation=g->session->generation;
    status=g->transport->exchange(g->transport->context,cmd,ver,request,n,reply,cap,size,result);
    if(g->session->generation!=generation) return CFP_CANCELLED;
    enum cfp_status timed=time_check(g);
    if(timed!=CFP_OK) return timed;
    if(status==CFP_OK && *size>cap) return CFP_MALFORMED_RESPONSE;
    return status;
}
enum cfp_status cfp_prepare_account(struct cfp_store *s,const struct cfp_transport *t,
    uint64_t uid,const char *name,const struct cfp_registry_ops *ops,
    const struct cfp_smoke_clock *clock,unsigned timeout,struct cfp_report *report)
{
    if(report) *report=(struct cfp_report){.status=CFP_INVALID_ARGUMENT};
    if(!s || !t || !t->exchange || !clock || !clock->now_ms || !clock->wait_ms ||
        !report || !timeout || timeout>5000 || s->lock_fd<0 || s->accounts_fd<0) {
        if(s) cfp_seed_session_invalidate(&s->session);
        return CFP_INVALID_ARGUMENT;
    }
    cfp_seed_session_invalidate(&s->session);
    struct cfp_binding binding={0};
    struct cfp_context_switch operation={0};
    struct guard guard={.transport=t,.clock=clock,.session=&s->session,.timeout=timeout};
    guard.start=guard.last=clock->now_ms(clock->context);
    enum cfp_status status=CFP_TRANSPORT_ERROR;
    if(guard.start<0) goto done;
    uint64_t epoch=s->session.generation;
    status=CFP_STORAGE_ERROR;
    if(cfp_store_account(s,ops,CFP_REGISTRY_LOOKUP,uid,name,NULL,&binding)!=CFP_STORE_OK) goto done;
    if(s->session.generation!=epoch) { status=CFP_CANCELLED; goto done; }
    status=time_check(&guard);
    if(status!=CFP_OK) goto done;
    struct cfp_transport checked={exchange,&guard};
    cfp_collect(&checked,report); status=report->status;
    if(status!=CFP_OK) goto done;
    if(report->info.vendor!=541282374 || report->info.product!=9 || report->info.model!=543 ||
        report->info.template_version!=4 || report->info.template_valid || report->info.fault_flags ||
        !report->encryption.seed_status_known || report->encryption.seed_set) {
        status=CFP_RECOVERY_REQUIRED; goto done;
    }
    uint32_t mode=0;
    status=cfp_query_mode(&checked,&mode);
    if(status!=CFP_OK) goto done;
    if(mode!=0) { status=CFP_RECOVERY_REQUIRED; goto done; }
    uint8_t request[2]={6,4},reply[4]={0}; size_t size=0; uint32_t result=0;
    status=exchange(&guard,8,1,request,2,reply,4,&size,&result);
    if(status!=CFP_OK) goto done;
    if(result) { status=CFP_CONTROLLER_ERROR; goto done; }
    if(size!=4) { status=CFP_MALFORMED_RESPONSE; goto done; }
    if(!(reply[0]&2)) { status=CFP_UNSUPPORTED_VERSION; goto done; }
    status=cfp_restore_validated_seed(&checked,s->provision.seed,&s->session);
    if(status!=CFP_OK) goto done;
    epoch=s->session.generation;
    status=cfp_context_begin(&operation,&s->session,binding.context_id,(uint64_t)guard.last,2000);
    while(status==CFP_PENDING) {
        status=time_check(&guard);
        if(status!=CFP_OK) goto done;
        status=cfp_context_tick(&operation,&checked,(uint64_t)guard.last);
        enum cfp_status timed=time_check(&guard);
        if(timed!=CFP_OK) { status=timed; goto done; }
        if((uint64_t)guard.last-operation.started_ms>=2000) { status=CFP_TIMEOUT; goto done; }
        if(status==CFP_PENDING && clock->wait_ms(clock->context,100)!=0) {
            status=CFP_TRANSPORT_ERROR; goto done;
        }
    }
    if(status!=CFP_OK) goto done;
    cfp_collect(&checked,report); status=report->status;
    if(status!=CFP_OK) goto done;
    status=cfp_query_mode(&checked,&mode);
    if(status!=CFP_OK) goto done;
    if(mode!=0) { status=CFP_RECOVERY_REQUIRED; goto done; }
    if(s->session.generation!=epoch || s->session.phase!=CFP_SESSION_SEEDED) {
        status=CFP_CANCELLED; goto done;
    }
    if(!report->encryption.seed_status_known || !report->encryption.seed_set ||
        report->info.template_valid || report->info.template_dirty || report->info.fault_flags ||
        report->info.vendor!=541282374 || report->info.product!=9 || report->info.model!=543 ||
        report->info.template_version!=4) status=CFP_RECOVERY_REQUIRED;
done:
    cfp_context_cancel(&operation);
    if(status!=CFP_OK) cfp_seed_session_invalidate(&s->session);
    cfp_seed_erase(&binding,sizeof(binding)); report->status=status; return status;
}

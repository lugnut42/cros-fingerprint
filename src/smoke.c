/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_smoke.h"
#include "cros_fp_restore.h"
#include "cros_fp_context.h"
static enum cfp_status context_supported(const struct cfp_transport *t)
{
    uint8_t req[2]={6,4}, response[4]={0}; size_t size=0; uint32_t result=0;
    enum cfp_status status=cfp_readonly_exchange(t,8,1,req,2,response,4,&size,&result);
    if(status!=CFP_OK) return status;
    if(result) return CFP_CONTROLLER_ERROR;
    if(size!=4) return CFP_MALFORMED_RESPONSE;
    return response[0]&2?CFP_OK:CFP_UNSUPPORTED_VERSION;
}
enum cfp_status cfp_smoke_run(const struct cfp_transport *t,
    int seed_dir,int context_dir,uid_t owner,int initialize,
    const struct cfp_seed_ops *ops,const struct cfp_smoke_clock *clock,
    struct cfp_report *report)
{
    if(!t || !t->exchange || !clock || !clock->now_ms || !clock->wait_ms ||
        !report || (initialize!=0 && initialize!=1)) return CFP_INVALID_ARGUMENT;
    cfp_collect(t,report);
    if(report->status!=CFP_OK) return report->status;
    enum cfp_status status=CFP_RECOVERY_REQUIRED;
    uint8_t context[32]={0}, seed[32]={0};
    struct cfp_seed_session session={0};
    struct cfp_context_switch operation={0};
    enum cfp_store_status stored;
    if(report->info.template_valid || report->info.template_version!=4 ||
        !report->encryption.seed_status_known || report->encryption.seed_set) goto done;
    status=context_supported(t);
    if(status!=CFP_OK) goto done;
    status=CFP_STORAGE_ERROR;
    if(initialize) {
        stored=cfp_seed_create(seed_dir,owner,ops,seed);
        cfp_seed_erase(seed,sizeof(seed));
        if(stored!=CFP_STORE_OK) goto done;
        if(cfp_context_id_create(context_dir,owner,ops,context)!=CFP_STORE_OK) goto done;
    }
    /* Validate both records before any state-changing device command. */
    if(cfp_context_id_load(context_dir,owner,ops,context)!=CFP_STORE_OK) goto done;
    status=cfp_restore_seed(t,seed_dir,owner,ops,&session,&stored);
    if(status!=CFP_OK) goto done;
    int64_t started=clock->now_ms(clock->context);
    if(started<0) { status=CFP_TRANSPORT_ERROR; goto done; }
    status=cfp_context_begin(&operation,&session,context,(uint64_t)started,2000);
    while(status==CFP_PENDING) {
        int64_t before=clock->now_ms(clock->context);
        if(before<started) { status=CFP_TRANSPORT_ERROR; break; }
        status=cfp_context_tick(&operation,t,(uint64_t)before);
        int64_t after=clock->now_ms(clock->context);
        if(after<before) { status=CFP_TRANSPORT_ERROR; break; }
        if(after-started>=2000) { status=CFP_TIMEOUT; break; }
        if(status==CFP_PENDING && clock->wait_ms(clock->context,100)!=0) {
            status=CFP_TRANSPORT_ERROR; break;
        }
    }
    if(status!=CFP_OK) goto done;
    cfp_collect(t,report);
    status=report->status;
    if(status==CFP_OK && (report->info.template_valid ||
        !report->encryption.seed_status_known || !report->encryption.seed_set))
        status=CFP_RECOVERY_REQUIRED;
done:
    cfp_context_cancel(&operation);
    cfp_seed_session_invalidate(&session);
    cfp_seed_erase(context,sizeof(context)); cfp_seed_erase(seed,sizeof(seed));
    report->status=status;
    return status;
}

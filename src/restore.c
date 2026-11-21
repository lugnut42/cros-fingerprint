/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_restore.h"
#include <string.h>
static enum cfp_status query(const struct cfp_transport *t,uint16_t cmd,
    uint8_t version,const uint8_t *request,size_t n,uint8_t *reply,size_t capacity)
{
    size_t size=0; uint32_t result=0;
    enum cfp_status status=cfp_readonly_exchange(t,cmd,version,request,n,
        reply,capacity,&size,&result);
    if(status!=CFP_OK) return status;
    if(result) return CFP_CONTROLLER_ERROR;
    return size==capacity?CFP_OK:CFP_MALFORMED_RESPONSE;
}
static enum cfp_status supported(const struct cfp_transport *t,uint16_t command)
{
    uint8_t request[2]={(uint8_t)command,(uint8_t)(command>>8)}, reply[4]={0};
    enum cfp_status status=query(t,8,1,request,2,reply,4);
    if(status!=CFP_OK) return status;
    return reply[0]&1?CFP_OK:CFP_UNSUPPORTED_VERSION;
}
static enum cfp_status encryption(const struct cfp_transport *t,
    struct cfp_encryption_status *out)
{
    uint8_t reply[8]={0};
    enum cfp_status status=query(t,0x409,0,NULL,0,reply,8);
    if(status!=CFP_OK) return status;
    return cfp_decode_encryption(reply,8,out);
}
enum cfp_status cfp_restore_validated_seed(const struct cfp_transport *t,
    const uint8_t seed[32],struct cfp_seed_session *s)
{
    if(s) cfp_seed_session_invalidate(s);
    if(!t || !t->exchange || !s || !seed) return CFP_INVALID_ARGUMENT;
    uint64_t generation=s->generation;
    uint8_t request[36]={0};
    enum cfp_status status=supported(t,0x409);
    if(s->generation!=generation) { status=CFP_CANCELLED; goto done; }
    if(status!=CFP_OK) goto done;
    status=supported(t,0x408);
    if(s->generation!=generation) { status=CFP_CANCELLED; goto done; }
    if(status!=CFP_OK) goto done;
    struct cfp_encryption_status state;
    status=encryption(t,&state);
    if(s->generation!=generation) { status=CFP_CANCELLED; goto done; }
    if(status!=CFP_OK) goto done;
    if(cfp_seed_session_begin(s,CFP_STORE_OK,state.seed_status_known,state.seed_set)
        !=CFP_SESSION_RESTORE_PENDING) { status=CFP_RECOVERY_REQUIRED; goto done; }
    generation=s->generation;
    request[0]=4; memcpy(request+4,seed,32);
    size_t size=0; uint32_t result=0;
    status=t->exchange(t->context,0x408,0,request,sizeof(request),NULL,0,&size,&result);
    cfp_seed_erase(request,sizeof(request));
    if(s->generation!=generation || s->phase!=CFP_SESSION_RESTORE_PENDING) {
        status=CFP_CANCELLED; goto done;
    }
    if(s->generation!=generation) { status=CFP_CANCELLED; goto done; }
    if(status!=CFP_OK) goto done;
    if(result) { status=CFP_CONTROLLER_ERROR; goto done; }
    if(size) { status=CFP_MALFORMED_RESPONSE; goto done; }
    status=encryption(t,&state);
    if(s->generation!=generation) { status=CFP_CANCELLED; goto done; }
    if(status!=CFP_OK) goto done;
    if(s->generation!=generation || s->phase!=CFP_SESSION_RESTORE_PENDING) {
        status=CFP_CANCELLED; goto done;
    }
    if(!state.seed_status_known || !state.seed_set) {
        status=CFP_RECOVERY_REQUIRED; goto done;
    }
    cfp_seed_session_complete(s,generation,1);
done:
    cfp_seed_erase(request,sizeof(request));
    if(status!=CFP_OK && s->generation==generation) cfp_seed_session_invalidate(s);
    return status;
}

/* Storage wrapper for the legacy experiment, retaining its record format. */
enum cfp_status cfp_restore_seed(const struct cfp_transport *t,
    int dir,uid_t owner,const struct cfp_seed_ops *ops,
    struct cfp_seed_session *s,enum cfp_store_status *store_result)
{
    if(s) cfp_seed_session_invalidate(s);
    if(!t || !t->exchange || !s || !store_result) return CFP_INVALID_ARGUMENT;
    uint64_t generation=s->generation;
    uint8_t seed[32]={0};
    *store_result=cfp_seed_load(dir,owner,ops,seed);
    enum cfp_status status=CFP_STORAGE_ERROR;
    if(s->generation!=generation) status=CFP_CANCELLED;
    else if(*store_result==CFP_STORE_OK) status=cfp_restore_validated_seed(t,seed,s);
    cfp_seed_erase(seed,sizeof(seed)); return status;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"
static uint32_t number(const uint8_t p[4])
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static enum cfp_status query(const struct cfp_transport *t,uint16_t cmd,uint8_t version,
    const uint8_t *request,size_t n,uint32_t *out)
{
    uint8_t reply[4]={0}; size_t size=0; uint32_t result=0;
    enum cfp_status status=cfp_readonly_exchange(t,cmd,version,request,n,reply,4,&size,&result);
    if(status!=CFP_OK) return status;
    if(result) return CFP_CONTROLLER_ERROR;
    if(size!=4) return CFP_MALFORMED_RESPONSE;
    *out=number(reply); return CFP_OK;
}
enum cfp_status cfp_query_mode(const struct cfp_transport *t,uint32_t *mode)
{
    if(!t || !t->exchange || !mode) return CFP_INVALID_ARGUMENT;
    const uint8_t command[2]={2,4},unchanged[4]={0,0,0,0x80};
    uint32_t mask;
    enum cfp_status status=query(t,8,1,command,2,&mask);
    if(status!=CFP_OK) return status;
    if(!(mask&1)) return CFP_UNSUPPORTED_VERSION;
    return query(t,0x402,0,unchanged,4,mode);
}

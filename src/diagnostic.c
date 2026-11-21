/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_diagnostic.h"
static enum cfp_status query(const struct cfp_transport *t, uint16_t command,
    uint8_t version, const uint8_t *request, size_t request_size,
    uint8_t *response, size_t capacity, size_t *size)
{
    uint32_t result=0;
    enum cfp_status status=cfp_readonly_exchange(t,command,version,request,
        request_size,response,capacity,size,&result);
    if(status!=CFP_OK) return status;
    if(result) return CFP_CONTROLLER_ERROR;
    if(*size>capacity) return CFP_MALFORMED_RESPONSE;
    return CFP_OK;
}
void cfp_collect(const struct cfp_transport *transport, struct cfp_report *report)
{
    *report=(struct cfp_report){0};
    report->status=cfp_negotiate_info(transport,&report->info_version);
    if(report->status!=CFP_OK) return;
    uint8_t response[36 + 20 * CFP_MAX_CAPTURE_FORMATS]={0};
    size_t size=0;
    report->status=query(transport,0x403,report->info_version,NULL,0,
        response,sizeof(response),&size);
    if(report->status!=CFP_OK) return;
    report->status=cfp_decode_info(report->info_version,response,size,&report->info);
    if(report->status!=CFP_OK) return;
    const uint8_t request[]={9,4};
    report->status=query(transport,8,1,request,sizeof(request),response,4,&size);
    if(report->status!=CFP_OK) return;
    if(size!=4) { report->status=CFP_MALFORMED_RESPONSE; return; }
    if(!(response[0]&1)) { report->status=CFP_UNSUPPORTED_VERSION; return; }
    report->status=query(transport,0x409,0,NULL,0,response,8,&size);
    if(report->status!=CFP_OK) return;
    report->status=cfp_decode_encryption(response,size,&report->encryption);
}

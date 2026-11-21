/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned calls,stage;
static size_t length=4;
static uint32_t ec_result,mask=3,value;
static enum cfp_status failure=CFP_OK;
static enum cfp_status exchange(void *p,uint16_t c,uint8_t v,const uint8_t *r,
    size_t n,uint8_t *out,size_t cap,size_t *size,uint32_t *result)
{
    (void)p; calls++; CHECK(cap==4);
    uint32_t reply;
    if(calls==1) { CHECK(c==8 && v==1 && n==2 && r[0]==2 && r[1]==4); reply=mask; }
    else { CHECK(calls==2 && c==0x402 && v==0 && n==4 && !memcmp(r,"\0\0\0\200",4)); reply=value; }
    for(unsigned i=0;i<4;i++) out[i]=(uint8_t)(reply>>(8*i));
    *size=calls==stage?length:4; *result=calls==stage?ec_result:0;
    return calls==stage?failure:CFP_OK;
}
int main(void)
{
    struct cfp_transport t={exchange,NULL}; uint32_t out=99;
    CHECK(cfp_query_mode(&t,&out)==CFP_OK && out==0 && calls==2);
    calls=0; value=0xdeadbeef;
    CHECK(cfp_query_mode(&t,&out)==CFP_OK && out==value && calls==2);
    for(unsigned k=0;k<2;k++) {
        calls=0; mask=k?2:0; out=99;
        CHECK(cfp_query_mode(&t,&out)==CFP_UNSUPPORTED_VERSION && out==99 && calls==1);
    }
    mask=3;
    for(stage=1;stage<=2;stage++) {
        for(length=0;length<=5;length++) if(length!=4) {
            calls=0;out=99;
            CHECK(cfp_query_mode(&t,&out)==CFP_MALFORMED_RESPONSE && out==99 && calls==stage);
        }
        length=4; calls=0; out=99; ec_result=3;
        CHECK(cfp_query_mode(&t,&out)==CFP_CONTROLLER_ERROR && out==99 && calls==stage);
        ec_result=0; calls=0; failure=CFP_TRANSPORT_ERROR;
        CHECK(cfp_query_mode(&t,&out)==CFP_TRANSPORT_ERROR && out==99 && calls==stage);
        failure=CFP_OK;
    }
    calls=0;
    CHECK(cfp_query_mode(NULL,&out)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_query_mode(&t,NULL)==CFP_INVALID_ARGUMENT && !calls);
    puts("Read-only mode query checks passed");
}

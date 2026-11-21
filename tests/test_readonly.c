/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned calls;
static enum cfp_status fake(void *context,uint16_t command,uint8_t version,
    const uint8_t *request,size_t request_size,uint8_t *response,size_t capacity,
    size_t *size,uint32_t *result)
{
    (void)context; (void)command; (void)version; (void)request; (void)request_size;
    (void)response; (void)capacity; (void)size; (void)result;
    calls++; return CFP_TRANSPORT_ERROR;
}
int main(void)
{
    struct cfp_transport t={fake,NULL};
    uint8_t request[]={0x08,0x04}, response[256];
    size_t size=0; uint32_t result=0;
    /* Querying a seed command's versions must be allowed without executing it. */
    CHECK(cfp_readonly_exchange(&t,8,1,request,2,response,256,&size,&result)==CFP_TRANSPORT_ERROR);
    CHECK(calls==1);
    CHECK(cfp_readonly_exchange(&t,0x403,2,NULL,0,response,256,&size,&result)==CFP_TRANSPORT_ERROR);
    CHECK(cfp_readonly_exchange(&t,0x403,3,NULL,0,response,256,&size,&result)==CFP_TRANSPORT_ERROR);
    CHECK(cfp_readonly_exchange(&t,0x409,0,NULL,0,response,256,&size,&result)==CFP_TRANSPORT_ERROR);
    CHECK(calls==4);
    for(unsigned cmd=0;cmd<=65535;cmd++) {
        if(cmd==0x409) continue;
        CHECK(cfp_readonly_exchange(&t,cmd,0,NULL,0,response,256,&size,&result)==CFP_COMMAND_DENIED);
    }
    CHECK(cfp_readonly_exchange(&t,8,1,request,1,response,256,&size,&result)==CFP_COMMAND_DENIED);
    CHECK(cfp_readonly_exchange(&t,0x403,3,request,2,response,256,&size,&result)==CFP_COMMAND_DENIED);
    CHECK(cfp_readonly_exchange(&t,0x409,1,NULL,0,response,256,&size,&result)==CFP_COMMAND_DENIED);
    CHECK(cfp_readonly_exchange(&t,8,1,NULL,2,response,256,&size,&result)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_readonly_exchange(NULL,8,1,request,2,response,256,&size,&result)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_readonly_exchange(&t,8,1,request,2,NULL,256,&size,&result)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_readonly_exchange(&t,8,1,request,2,response,256,NULL,&result)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_readonly_exchange(&t,8,1,request,2,response,256,&size,NULL)==CFP_INVALID_ARGUMENT);
    CHECK(calls==4);
    uint8_t mode[4]={0,0,0,0x80};
    CHECK(cfp_readonly_exchange(&t,0x402,0,mode,4,response,256,&size,&result)==CFP_TRANSPORT_ERROR);
    CHECK(calls==5);
    for(unsigned bit=0;bit<32;bit++) {
        uint32_t value=1u<<bit;
        if(bit==31) continue;
        for(unsigned i=0;i<4;i++) mode[i]=(uint8_t)(value>>(8*i));
        CHECK(cfp_readonly_exchange(&t,0x402,0,mode,4,response,256,&size,&result)==CFP_COMMAND_DENIED);
        mode[3]|=0x80;
        CHECK(cfp_readonly_exchange(&t,0x402,0,mode,4,response,256,&size,&result)==CFP_COMMAND_DENIED);
    }
    for(unsigned i=0;i<4;i++) mode[i]=0;
    CHECK(cfp_readonly_exchange(&t,0x402,0,mode,4,response,256,&size,&result)==CFP_COMMAND_DENIED);
    mode[3]=0x80;
    CHECK(cfp_readonly_exchange(&t,0x402,1,mode,4,response,256,&size,&result)==CFP_COMMAND_DENIED);
    CHECK(calls==5);
    puts("Read-only boundary checks passed"); return 0;
}

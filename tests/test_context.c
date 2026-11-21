/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned calls;
static uint32_t controller;
static size_t length;
static enum cfp_status transport_status;
static uint8_t id[32];
static struct cfp_seed_session *invalidate_during_exchange;
static enum cfp_status exchange(void *p,uint16_t cmd,uint8_t version,
    const uint8_t *request,size_t n,uint8_t *response,size_t cap,
    size_t *size,uint32_t *result)
{
    (void)p; (void)response;
    CHECK(cmd==0x406 && version==1 && n==36 && cap==0);
    CHECK(request[0]==(calls?1:0));
    CHECK(request[1]==0 && request[2]==0 && request[3]==0);
    CHECK(!memcmp(request+4,id,32));
    calls++;
    if(invalidate_during_exchange) cfp_seed_session_invalidate(invalidate_during_exchange);
    *size=length; *result=controller; return transport_status;
}
static struct cfp_transport t={exchange,NULL};
static void setup(struct cfp_context_switch *o,struct cfp_seed_session *s)
{
    memset(o,0,sizeof(*o)); memset(s,0,sizeof(*s)); memset(id,0x42,32);
    invalidate_during_exchange=NULL;
    calls=0; controller=0; length=0; transport_status=CFP_OK;
    CHECK(cfp_seed_session_begin(s,CFP_STORE_OK,1,0)==CFP_SESSION_RESTORE_PENDING);
    cfp_seed_session_complete(s,s->generation,1);
    CHECK(cfp_context_begin(o,s,id,1000,500)==CFP_PENDING && calls==0);
}
static void cleared(const struct cfp_context_switch *o)
{ for(unsigned i=0;i<32;i++) CHECK(o->user_id[i]==0); }
static void sequence(void)
{
    struct cfp_context_switch o; struct cfp_seed_session s; setup(&o,&s);
    CHECK(cfp_context_tick(&o,&t,1000)==CFP_PENDING && calls==1);
    CHECK(cfp_context_tick(&o,&t,1099)==CFP_PENDING && calls==1);
    controller=16;
    CHECK(cfp_context_tick(&o,&t,1100)==CFP_PENDING && calls==2);
    CHECK(cfp_context_tick(&o,&t,1150)==CFP_PENDING && calls==2);
    controller=0;
    CHECK(cfp_context_tick(&o,&t,1200)==CFP_OK && calls==3);
    CHECK(o.phase==CFP_CONTEXT_DONE && s.phase==CFP_SESSION_SEEDED); cleared(&o);
    CHECK(cfp_context_tick(&o,&t,1300)==CFP_OK && calls==3);
    cfp_seed_session_invalidate(&s);
    CHECK(cfp_context_tick(&o,&t,1400)==CFP_CANCELLED && calls==3);
}
static void failures(void)
{
    struct cfp_context_switch o; struct cfp_seed_session s; setup(&o,&s);
    CHECK(cfp_context_tick(&o,&t,1500)==CFP_TIMEOUT && calls==0);
    CHECK(s.phase==CFP_SESSION_BLOCKED); cleared(&o);
    setup(&o,&s); CHECK(cfp_context_tick(&o,&t,1000)==CFP_PENDING);
    cfp_context_cancel(&o);
    CHECK(cfp_context_tick(&o,&t,1100)==CFP_CANCELLED && calls==1);
    CHECK(s.phase==CFP_SESSION_BLOCKED); cleared(&o);
    setup(&o,&s); cfp_seed_session_invalidate(&s);
    CHECK(cfp_context_tick(&o,&t,1100)==CFP_CANCELLED && calls==0);
    setup(&o,&s); controller=16;
    CHECK(cfp_context_tick(&o,&t,1000)==CFP_CONTROLLER_ERROR && calls==1);
    setup(&o,&s); transport_status=CFP_TRANSPORT_ERROR;
    CHECK(cfp_context_tick(&o,&t,1000)==CFP_TRANSPORT_ERROR && calls==1);
    CHECK(s.phase==CFP_SESSION_BLOCKED);
    setup(&o,&s); CHECK(cfp_context_tick(&o,&t,1000)==CFP_PENDING);
    controller=16;
    CHECK(cfp_context_tick(&o,&t,1100)==CFP_PENDING);
    CHECK(cfp_context_tick(&o,&t,1500)==CFP_TIMEOUT && calls==2);
    setup(&o,&s);
    CHECK(cfp_seed_session_begin(&s,CFP_STORE_OK,1,0)==CFP_SESSION_RESTORE_PENDING);
    uint64_t newer=s.generation;
    CHECK(cfp_context_tick(&o,&t,1100)==CFP_CANCELLED && calls==0);
    CHECK(s.generation==newer && s.phase==CFP_SESSION_RESTORE_PENDING);
    setup(&o,&s); CHECK(cfp_context_tick(&o,&t,1000)==CFP_PENDING);
    invalidate_during_exchange=&s;
    CHECK(cfp_context_tick(&o,&t,1100)==CFP_CANCELLED);
    CHECK(s.phase==CFP_SESSION_BLOCKED); cleared(&o);
    setup(&o,&s); length=1;
    CHECK(cfp_context_tick(&o,&t,1000)==CFP_MALFORMED_RESPONSE);
    setup(&o,&s); CHECK(cfp_context_tick(&o,&t,999)==CFP_INVALID_ARGUMENT && calls==0);
    setup(&o,&s);
    CHECK(cfp_context_begin(&o,&s,id,1000,500)==CFP_INVALID_ARGUMENT);
    cfp_context_cancel(&o);
    CHECK(cfp_context_begin(&o,&s,id,1000,500)==CFP_INVALID_ARGUMENT);
}
int main(int argc,char **argv)
{
    if(argc==1 || !strcmp(argv[1],"sequence")) sequence();
    if(argc==1 || !strcmp(argv[1],"failures")) failures();
    puts("Context-switch checks passed");
}

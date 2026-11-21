/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_restore.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned calls, seed_writes;
static int scenario;
static ssize_t rng(void *p,size_t n) { memset(p,0x6b,n); return n; }
static struct cfp_seed_ops ops={rng,write,fsync,linkat};
static enum cfp_status fake(void *ctx,uint16_t cmd,uint8_t version,
    const uint8_t *req,size_t n,uint8_t *response,size_t cap,size_t *size,uint32_t *result)
{
    unsigned step=calls++;
    if(scenario==8 && step==0) cfp_seed_session_invalidate(ctx);
    *size=cap; *result=0; if(cap) memset(response,0,cap);
    if(step<2) {
        CHECK(cmd==8 && version==1 && n==2 && cap==4);
        CHECK(req[0]==(step==0?9:8) && req[1]==4);
        response[0]=scenario==1?0:1;
    } else if(step==2 || step==4) {
        CHECK(cmd==0x409 && version==0 && n==0 && cap==8);
        response[0]=scenario==3?0:1;
        response[4]=(step==4 && scenario!=5) || scenario==2;
    } else {
        CHECK(step==3 && cmd==0x408 && version==0 && n==36 && cap==0);
        CHECK(req[0]==4 && req[1]==0 && req[2]==0 && req[3]==0);
        for(unsigned i=4;i<36;i++) CHECK(req[i]==0x6b);
        seed_writes++;
        if(scenario==4) *result=3;
        if(scenario==6) return CFP_TRANSPORT_ERROR;
        if(scenario==7) cfp_seed_session_invalidate(ctx);
    }
    return CFP_OK;
}
int main(void)
{
    char path[]="/tmp/cfp-restore-test-XXXXXX"; CHECK(mkdtemp(path));
    int fd=open(path,O_RDONLY|O_DIRECTORY); CHECK(fd>=0);
    struct cfp_transport t={fake,NULL}; struct cfp_seed_session s={0};
    t.context=&s;
    enum cfp_store_status stored; unsigned char seed[32];
    CHECK(cfp_restore_seed(&t,fd,geteuid(),&ops,&s,&stored)==CFP_STORAGE_ERROR);
    CHECK(stored==CFP_STORE_MISSING && calls==0 && s.phase==CFP_SESSION_BLOCKED);
    CHECK(cfp_seed_create(fd,geteuid(),&ops,seed)==CFP_STORE_OK); cfp_seed_erase(seed,32);
    CHECK(cfp_restore_seed(&t,fd,geteuid(),&ops,&s,&stored)==CFP_OK);
    CHECK(calls==5 && seed_writes==1 && s.phase==CFP_SESSION_SEEDED);
    for(scenario=1;scenario<=8;scenario++) {
        calls=0; seed_writes=0;
        enum cfp_status expected=scenario==1?CFP_UNSUPPORTED_VERSION:
            scenario==4?CFP_CONTROLLER_ERROR:scenario==6?CFP_TRANSPORT_ERROR:scenario>=7?CFP_CANCELLED:CFP_RECOVERY_REQUIRED;
        CHECK(cfp_restore_seed(&t,fd,geteuid(),&ops,&s,&stored)==expected);
        CHECK(s.phase==CFP_SESSION_BLOCKED && seed_writes==(scenario>=4 && scenario<=7?1u:0u));
    }
    CHECK(unlinkat(fd,"seed",0)==0); close(fd); CHECK(rmdir(path)==0);
    puts("Seed restoration checks passed");
}

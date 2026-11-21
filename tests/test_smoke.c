/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_smoke.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned random_calls,writes,polls;
static int seeded,templates,unsupported,late;
static int64_t now;
static ssize_t rng(void *p,size_t n) { random_calls++; memset(p,0x30+random_calls,n); return n; }
static struct cfp_seed_ops ops={rng,write,fsync,linkat};
static int64_t time_now(void *p) { (void)p; return now; }
static int wait_ms(void *p,unsigned ms) { (void)p; now+=ms; return 0; }
static struct cfp_smoke_clock clock_ops={time_now,wait_ms,NULL};
static enum cfp_status fake(void *p,uint16_t cmd,uint8_t ver,const uint8_t *req,
    size_t n,uint8_t *out,size_t cap,size_t *size,uint32_t *result)
{
    (void)p; *result=0; *size=0; if(cap) memset(out,0,cap);
    if(cmd==8) {
        CHECK(ver==1 && n==2 && cap==4 && req[1]==4); *size=4;
        out[0]=req[0]==3?12:req[0]==6?(unsupported?0:2):1;
    } else if(cmd==0x403) {
        CHECK(ver==3 && cap>=36); *size=36;
        out[20]=0x24; out[21]=0x14; out[24]=5; out[26]=templates; out[32]=4;
    } else if(cmd==0x409) {
        CHECK(cap==8); *size=8; out[0]=1; out[4]=seeded;
    } else if(cmd==0x408) {
        CHECK(n==36 && random_calls==2); writes++; seeded=1;
        for(unsigned i=4;i<36;i++) CHECK(req[i]==0x31);
    } else {
        CHECK(cmd==0x406 && ver==1 && n==36 && seeded);
        for(unsigned i=4;i<36;i++) CHECK(req[i]==0x32);
        writes++;
        if(req[0]==1) { polls++; if(late) now+=3000; }
        else CHECK(req[0]==0);
    }
    return CFP_OK;
}
int main(void)
{
    char path[]="/tmp/cfp-smoke-test-XXXXXX"; CHECK(mkdtemp(path));
    int root=open(path,O_RDONLY|O_DIRECTORY); CHECK(root>=0);
    CHECK(mkdirat(root,"seed",0700)==0 && mkdirat(root,"context",0700)==0);
    int seed_dir=openat(root,"seed",O_RDONLY|O_DIRECTORY);
    int context_dir=openat(root,"context",O_RDONLY|O_DIRECTORY);
    CHECK(seed_dir>=0 && context_dir>=0);
    struct cfp_transport t={fake,NULL}; struct cfp_report report;
    templates=1;
    CHECK(cfp_smoke_run(&t,seed_dir,context_dir,geteuid(),1,&ops,&clock_ops,&report)==CFP_RECOVERY_REQUIRED);
    CHECK(writes==0 && random_calls==0);
    templates=0; unsupported=1;
    CHECK(cfp_smoke_run(&t,seed_dir,context_dir,geteuid(),1,&ops,&clock_ops,&report)==CFP_UNSUPPORTED_VERSION);
    CHECK(writes==0 && random_calls==0);
    unsupported=0;
    CHECK(cfp_smoke_run(&t,seed_dir,context_dir,geteuid(),0,&ops,&clock_ops,&report)==CFP_STORAGE_ERROR);
    CHECK(writes==0 && random_calls==0);
    CHECK(cfp_smoke_run(&t,seed_dir,context_dir,geteuid(),1,&ops,&clock_ops,&report)==CFP_OK);
    CHECK(report.encryption.seed_set && writes==3 && polls==1 && random_calls==2);
    writes=0;
    CHECK(cfp_smoke_run(&t,seed_dir,context_dir,geteuid(),0,&ops,&clock_ops,&report)==CFP_RECOVERY_REQUIRED);
    CHECK(writes==0);
    seeded=0; late=1;
    CHECK(cfp_smoke_run(&t,seed_dir,context_dir,geteuid(),0,&ops,&clock_ops,&report)==CFP_TIMEOUT);
    CHECK(random_calls==2);
    CHECK(unlinkat(seed_dir,"seed",0)==0 && unlinkat(context_dir,"context",0)==0);
    close(seed_dir); close(context_dir);
    CHECK(unlinkat(root,"seed",AT_REMOVEDIR)==0 && unlinkat(root,"context",AT_REMOVEDIR)==0);
    close(root); CHECK(rmdir(path)==0);
    puts("Seed/context smoke workflow checks passed");
}

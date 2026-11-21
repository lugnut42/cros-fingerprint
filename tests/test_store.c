/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_store.h"
#include "cros_fp_context.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned calls,fail;
static ssize_t rng(void *p,size_t n)
{
    CHECK(n==64); calls++; memset(p,(int)calls,32); memset((char *)p+32,(int)calls+64,32); return 64;
}
static int replace(int a,const char *b,int c,const char *d)
{ return fail?-1:renameat(a,b,c,d); }
static const struct cfp_seed_ops seeds={rng,write,fsync,linkat};
static const struct cfp_registry_ops accounts={rng,write,fsync,replace};
static void cleanup(int fd,const char *path)
{
    int p=openat(fd,"provision",O_RDONLY|O_DIRECTORY);
    if(p>=0) { unlinkat(p,"provision",0); unlinkat(p,".provision.pending",0); close(p); unlinkat(fd,"provision",AT_REMOVEDIR); }
    p=openat(fd,"accounts",O_RDONLY|O_DIRECTORY);
    if(p>=0) { unlinkat(p,"registry",0); unlinkat(p,".registry.pending",0); close(p); unlinkat(fd,"accounts",AT_REMOVEDIR); }
    close(fd); CHECK(rmdir(path)==0);
}
static void seeded(struct cfp_store *s)
{
    CHECK(cfp_seed_session_begin(&s->session,CFP_STORE_OK,1,0)==CFP_SESSION_RESTORE_PENDING);
    cfp_seed_session_complete(&s->session,s->session.generation,1);
}
static unsigned device_calls;
static enum cfp_status exchange(void *p,uint16_t c,uint8_t v,const uint8_t *r,
    size_t n,uint8_t *o,size_t cap,size_t *size,uint32_t *result)
{
    (void)p;(void)c;(void)v;(void)r;(void)n;(void)o;(void)cap;
    device_calls++; *size=0; *result=0; return CFP_OK;
}

static void mixed_and_missing(void)
{
    char a[]="/tmp/cfp-store-a-XXXXXX",b[]="/tmp/cfp-store-b-XXXXXX";
    CHECK(mkdtemp(a) && mkdtemp(b));
    int fa=open(a,O_RDONLY|O_DIRECTORY),fb=open(b,O_RDONLY|O_DIRECTORY);
    CHECK(fa>=0 && fb>=0);
    struct cfp_store sa,sb,out,before; memset(&before,0xa5,sizeof(before)); out=before;
    CHECK(cfp_store_open(fa,geteuid(),1,&seeds,&accounts,&sa)==CFP_STORE_OK);
    CHECK(cfp_store_open(fb,geteuid(),1,&seeds,&accounts,&sb)==CFP_STORE_OK);
    cfp_store_close(&sa); cfp_store_close(&sb);
    /* Simulate storage mixing without exposing any record bytes. */
    CHECK(renameat(fa,"provision",fa,"swap")==0);
    CHECK(renameat(fb,"provision",fa,"provision")==0);
    CHECK(renameat(fa,"swap",fb,"provision")==0);
    CHECK(cfp_store_open(fa,geteuid(),0,&seeds,&accounts,&out)==CFP_STORE_RECOVERY);
    CHECK(!memcmp(&out,&before,sizeof(out)));
    CHECK(cfp_store_open(fb,geteuid(),0,&seeds,&accounts,&out)==CFP_STORE_RECOVERY);
    CHECK(renameat(fa,"provision",fa,"swap")==0);
    CHECK(renameat(fb,"provision",fa,"provision")==0);
    CHECK(renameat(fa,"swap",fb,"provision")==0);
    CHECK(cfp_store_open(fa,geteuid(),0,&seeds,&accounts,&sa)==CFP_STORE_OK);
    cfp_store_close(&sa);
    int p=openat(fa,"provision",O_RDONLY|O_DIRECTORY); CHECK(p>=0);
    CHECK(unlinkat(p,"provision",0)==0); close(p);
    unsigned prior=calls;
    CHECK(cfp_store_open(fa,geteuid(),0,&seeds,&accounts,&out)==CFP_STORE_MISSING);
    CHECK(cfp_store_open(fa,geteuid(),1,&seeds,&accounts,&out)==CFP_STORE_EXISTS);
    CHECK(calls==prior && !memcmp(&out,&before,sizeof(out)));
    cleanup(fa,a); cleanup(fb,b);
}
static void incomplete_create(void)
{
    char path[]="/tmp/cfp-store-incomplete-XXXXXX"; CHECK(mkdtemp(path));
    int fd=open(path,O_RDONLY|O_DIRECTORY); CHECK(fd>=0);
    struct cfp_store out,before; memset(&before,0xa5,sizeof(before)); out=before;
    fail=1;
    CHECK(cfp_store_open(fd,geteuid(),1,&seeds,&accounts,&out)==CFP_STORE_IO);
    CHECK(!memcmp(&out,&before,sizeof(out)));
    fail=0; unsigned prior=calls;
    CHECK(cfp_store_open(fd,geteuid(),0,&seeds,&accounts,&out)==CFP_STORE_RECOVERY);
    CHECK(cfp_store_open(fd,geteuid(),1,&seeds,&accounts,&out)==CFP_STORE_EXISTS);
    CHECK(calls==prior);
    cleanup(fd,path);
}

int main(void)
{
    char path[]="/tmp/cfp-store-XXXXXX"; CHECK(mkdtemp(path));
    int fd=open(path,O_RDONLY|O_DIRECTORY); CHECK(fd>=0);
    struct cfp_store s,other,before; memset(&before,0xa5,sizeof(before)); other=before;
    CHECK(cfp_store_open(fd,geteuid(),0,&seeds,&accounts,&other)==CFP_STORE_MISSING);
    CHECK(!memcmp(&other,&before,sizeof(other)) && calls==0);
    CHECK(cfp_store_open(fd,geteuid(),1,&seeds,&accounts,&s)==CFP_STORE_OK);
    CHECK(s.session.phase==CFP_SESSION_BLOCKED);
    CHECK(cfp_store_open(fd,geteuid(),0,&seeds,&accounts,&other)==CFP_STORE_BUSY);
    struct cfp_binding a,b;
    CHECK(cfp_store_account(&s,&accounts,CFP_REGISTRY_REGISTER,1000,"alice",NULL,&a)==CFP_STORE_OK);
    CHECK(!memcmp(a.reader_generation,s.provision.generation,32));
    struct cfp_provision saved=s.provision;
    cfp_store_close(&s);
    CHECK(cfp_store_open(fd,geteuid(),0,&seeds,&accounts,&s)==CFP_STORE_OK);
    CHECK(!memcmp(&saved,&s.provision,sizeof(saved)));
    CHECK(cfp_store_account(&s,&accounts,CFP_REGISTRY_LOOKUP,1000,"alice",NULL,&b)==CFP_STORE_OK);
    CHECK(!memcmp(&a,&b,sizeof(a)));
    struct cfp_context_switch op={0}; struct cfp_transport t={exchange,NULL};
    seeded(&s);
    CHECK(cfp_context_begin(&op,&s.session,a.context_id,0,2000)==CFP_PENDING);
    CHECK(cfp_context_tick(&op,&t,0)==CFP_PENDING && device_calls==1);
    CHECK(cfp_store_account(&s,&accounts,CFP_REGISTRY_RETIRE,1000,"alice",&a,NULL)==CFP_STORE_OK);
    CHECK(s.session.phase==CFP_SESSION_BLOCKED);
    CHECK(cfp_context_tick(&op,&t,100)==CFP_CANCELLED && device_calls==1);
    CHECK(cfp_store_account(&s,&accounts,CFP_REGISTRY_LOOKUP,1000,"alice",NULL,&b)==CFP_STORE_MISSING);
    CHECK(cfp_store_account(&s,&accounts,CFP_REGISTRY_REGISTER,1000,"alice",NULL,&b)==CFP_STORE_OK);
    seeded(&s); fail=1;
    CHECK(cfp_store_account(&s,&accounts,CFP_REGISTRY_RETIRE,1000,"alice",&b,NULL)==CFP_STORE_IO);
    CHECK(s.session.phase==CFP_SESSION_BLOCKED); fail=0; cfp_store_close(&s);
    CHECK(cfp_store_open(fd,geteuid(),0,&seeds,&accounts,&other)==CFP_STORE_RECOVERY);
    CHECK(!memcmp(&other,&before,sizeof(other)));
    CHECK(cfp_store_open(fd,geteuid(),1,&seeds,&accounts,&other)==CFP_STORE_EXISTS);
    cleanup(fd,path);
    mixed_and_missing(); incomplete_create();
    puts("Provisioned store ownership checks passed"); return 0;
}

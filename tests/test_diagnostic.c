/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned calls;
static int fail_at=-1;
static enum cfp_status fake(void *context,uint16_t command,uint8_t version,
    const uint8_t *request,size_t request_size,uint8_t *response,size_t capacity,
    size_t *size,uint32_t *result)
{
    (void)context;
    unsigned step=calls++;
    memset(response,0,capacity); *result=0;
    if((int)step==fail_at) { *result=3; *size=0; return CFP_OK; }
    if(step==0 || step==2) {
        CHECK(command==8 && version==1 && request_size==2 && capacity==4);
        CHECK(request[0]==(step==0?3:9) && request[1]==4);
        response[0]=step==0?12:1; *size=4;
    } else if(step==1) {
        CHECK(command==0x403 && version==3 && request_size==0 && capacity>=36);
        response[8]=0x1f; response[9]=2; response[18]=0xff; response[19]=3;
        *size=36;
    } else {
        CHECK(step==3 && command==0x409 && version==0 && request_size==0 && capacity==8);
        response[0]=1; *size=8;
    }
    return CFP_OK;
}
static void ready(void *context,struct cfp_report *r)
{ (void)context; r->info.model=543; r->status=CFP_OK; }
static void stuck(void *context,struct cfp_report *r)
{ (void)context; (void)r; for(;;) pause(); }
static void crash(void *context,struct cfp_report *r)
{ (void)context; (void)r; _exit(9); }
int main(int argc,char **argv)
{
    struct cfp_report r;
    if(argc==1 || strcmp(argv[1],"collect")==0) {
        struct cfp_transport t={fake,NULL};
        cfp_collect(&t,&r);
        CHECK(r.status==CFP_OK && calls==4 && r.info_version==3 && r.info.model==543);
        CHECK(r.encryption.seed_status_known && !r.encryption.seed_set);
        for(fail_at=0;fail_at<4;fail_at++) {
            calls=0; cfp_collect(&t,&r);
            CHECK(r.status==CFP_CONTROLLER_ERROR && calls==(unsigned)fail_at+1);
        }
    }
    if(argc==1 || strcmp(argv[1],"supervisor")==0) {
        CHECK(cfp_supervise(ready,NULL,1000,&r)==0 && r.info.model==543);
        struct timespec start,end;
        CHECK(clock_gettime(CLOCK_MONOTONIC,&start)==0);
        CHECK(cfp_supervise(stuck,NULL,80,&r)==124);
        CHECK(clock_gettime(CLOCK_MONOTONIC,&end)==0);
        double elapsed=(end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)/1e9;
        CHECK(elapsed>=0.06 && elapsed<1.0);
        CHECK(cfp_supervise(crash,NULL,1000,&r)==125);
    }
    puts("Diagnostic checks passed");
}

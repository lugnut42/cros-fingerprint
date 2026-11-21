/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_linux.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void *packet;
void __real_free(void *);
void __wrap_free(void *p)
{
    if(p && p==packet) {
        const unsigned char *bytes=p;
        for(size_t i=0;i<sizeof(struct cfp_linux_command)+4;i++) CHECK(bytes[i]==0);
        packet=NULL;
    }
    __real_free(p);
}
static int returned=4, saved_errno, calls;
static uint32_t ec_result;
static int fake(int fd,unsigned long request,void *argument)
{
    struct cfp_linux_command *c=argument;
    calls++; packet=argument;
    CHECK(fd==42 && request==CFP_IOCTL_COMMAND);
    CHECK(c->command==8 && c->version==1 && c->outsize==2 && c->insize==4);
    CHECK(c->data[0]==3 && c->data[1]==4 && c->data[2]==0 && c->data[3]==0);
    memset(c->data,0xcc,4); c->result=ec_result;
    errno=saved_errno; return returned;
}
int main(void)
{
    struct cfp_linux linux_device={42,0,fake};
    uint8_t req[]={3,4}, reply[4]={0}; size_t size=99; uint32_t result=99;
    CHECK(cfp_linux_exchange(&linux_device,8,1,req,2,reply,4,&size,&result)==CFP_OK);
    CHECK(size==4 && result==0 && reply[0]==0xcc && calls==1);
    returned=2; memset(reply,0,4);
    CHECK(cfp_linux_exchange(&linux_device,8,1,req,2,reply,4,&size,&result)==CFP_OK);
    CHECK(size==2 && reply[1]==0xcc && reply[2]==0);
    returned=5; size=99; result=99;
    CHECK(cfp_linux_exchange(&linux_device,8,1,req,2,reply,4,&size,&result)==CFP_MALFORMED_RESPONSE);
    CHECK(size==99 && result==99);
    returned=0; ec_result=3;
    CHECK(cfp_linux_exchange(&linux_device,8,1,req,2,reply,4,&size,&result)==CFP_OK);
    CHECK(size==0 && result==3);
    int errors[]={EACCES,ENOENT,EIO,EINTR,ETIMEDOUT};
    for(size_t i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        returned=-1; saved_errno=errors[i]; int before=calls;
        size=99; result=99;
        CHECK(cfp_linux_exchange(&linux_device,8,1,req,2,reply,4,&size,&result)==CFP_TRANSPORT_ERROR);
        CHECK(linux_device.error_number==errors[i] && calls==before+1);
        CHECK(size==99 && result==99);
    }
    int before=calls;
    CHECK(cfp_linux_exchange(&linux_device,8,1,req,2,reply,SIZE_MAX,&size,&result)==CFP_LIMIT_EXCEEDED);
    CHECK(cfp_linux_exchange(&linux_device,8,1,NULL,2,reply,4,&size,&result)==CFP_INVALID_ARGUMENT);
    CHECK(calls==before);
    puts("Linux ioctl checks passed");
}

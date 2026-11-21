/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_provision.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned failure;
static ssize_t rng(void *p,size_t n)
{
    CHECK(n==64); memset(p,0x31,32); memset((char *)p+32,0x42,32);
    if(failure==1) return 32;
    if(failure==2) memset(p,0,32);
    if(failure==3) memset((char *)p+32,0xff,32);
    if(failure==4) memset(p,0x31,64);
    return 64;
}
static const struct cfp_seed_ops ops={rng,write,fsync,linkat};
int main(void)
{
    for(failure=0;failure<5;failure++) {
        char path[]="/tmp/cfp-provision-XXXXXX"; CHECK(mkdtemp(path));
        int fd=open(path,O_RDONLY|O_DIRECTORY); CHECK(fd>=0);
        struct cfp_provision p,before; memset(&before,0xa5,sizeof(before)); p=before;
        CHECK(cfp_provision_load(fd,geteuid(),&ops,&p)==CFP_STORE_MISSING);
        CHECK(!memcmp(&p,&before,sizeof(p)));
        CHECK(cfp_provision_create(fd,geteuid(),&ops,&p)==(failure?CFP_STORE_RANDOM:CFP_STORE_OK));
        if(failure) CHECK(!memcmp(&p,&before,sizeof(p)));
        else {
            CHECK(p.generation[0]==0x31 && p.seed[0]==0x42); before=p;
            memset(&p,0,sizeof(p));
            CHECK(cfp_provision_load(fd,geteuid(),&ops,&p)==CFP_STORE_OK);
            CHECK(!memcmp(&p,&before,sizeof(p)));
            CHECK(cfp_provision_create(fd,geteuid(),&ops,&p)==CFP_STORE_EXISTS);
            unsigned char original[104],bad[104];
            int file=openat(fd,"provision",O_RDWR); CHECK(file>=0);
            CHECK(read(file,original,sizeof(original))==104);
            for(unsigned i=0;i<104;i++) {
                memcpy(bad,original,104); bad[i]^=1;
                CHECK(pwrite(file,bad,104,0)==104);
                CHECK(cfp_provision_load(fd,geteuid(),&ops,&p)==CFP_STORE_CORRUPT);
            }
            CHECK(pwrite(file,original,104,0)==104);
            for(unsigned i=0;i<104;i++) {
                CHECK(ftruncate(file,i)==0);
                CHECK(cfp_provision_load(fd,geteuid(),&ops,&p)==CFP_STORE_CORRUPT);
                CHECK(pwrite(file,original,104,0)==104);
            }
            close(file);
            CHECK(renameat(fd,"provision",fd,"seed")==0);
            unsigned char seed[32];
            CHECK(cfp_seed_load(fd,geteuid(),&ops,seed)==CFP_STORE_CORRUPT);
            CHECK(unlinkat(fd,"seed",0)==0);
        }
        close(fd); CHECK(rmdir(path)==0);
    }
    puts("Provisioning record checks passed"); return 0;
}

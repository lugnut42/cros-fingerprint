/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_registry.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned random_calls, sync_calls, failure;
static ssize_t random_fake(void *p,size_t n)
{
    CHECK(n==64); random_calls++;
    for(size_t i=0;i<n;i++) ((unsigned char *)p)[i]=(unsigned char)(random_calls*2+i/32);
    if(failure==1) return -1;
    if(failure==2) return 12;
    if(failure==7) memset(p,0,n);
    if(failure==8) memset(p,0xff,n);
    if(failure==9) memset(p,0x42,n);
    return (ssize_t)n;
}
static ssize_t write_fake(int fd,const void *p,size_t n)
{
    if(failure==3) { errno=ENOSPC; return -1; }
    return write(fd,p,n>17?17:n);
}
static int sync_fake(int fd)
{
    sync_calls++;
    if((failure==4 && sync_calls==3) || (failure==6 && sync_calls==4)) {
        errno=EIO; return -1;
    }
    return fsync(fd);
}
static int replace_fake(int a,const char *b,int c,const char *d)
{
    if(failure==5) { errno=EIO; return -1; }
    return renameat(a,b,c,d);
}
static const struct cfp_registry_ops ops={random_fake,write_fake,sync_fake,replace_fake};
static char path[128];
static struct cfp_registry_request req;
static int fresh(void)
{
    strcpy(path,"/tmp/cfp-registry-test-XXXXXX"); CHECK(mkdtemp(path));
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC); CHECK(fd>=0);
    failure=random_calls=sync_calls=0;
    memset(&req,0,sizeof(req)); memset(req.reader_generation,0x71,32);
    req.name="alice"; req.uid=1000; return fd;
}
static enum cfp_store_status apply(int fd,enum cfp_registry_action action,
    struct cfp_binding *out)
{
    req.action=action; return cfp_registry_apply(fd,geteuid(),&ops,&req,out);
}
static void cleanup(int fd)
{
    unlinkat(fd,"registry",0); unlinkat(fd,".registry.pending",0);
    unlinkat(fd,"alias",0); close(fd); CHECK(rmdir(path)==0);
}
static void lifecycle(void)
{
    int fd=fresh(); struct cfp_binding a,b,out,before;
    memset(&before,0xa5,sizeof(before)); out=before;
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_MISSING);
    CHECK(!memcmp(&out,&before,sizeof(out)) && random_calls==0);
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
    CHECK(apply(fd,CFP_REGISTRY_CHECK,NULL)==CFP_STORE_OK);
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_EXISTS);
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&a)==CFP_STORE_OK);
    CHECK(a.schema==1 && a.template_format==4);
    CHECK(memcmp(a.account_id,a.context_id,32));
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_EXISTS);
    req.name="bob"; CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_EXISTS);
    req.uid=1001; req.name="alice";
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_EXISTS);
    req.name="bob"; CHECK(apply(fd,CFP_REGISTRY_REGISTER,&b)==CFP_STORE_OK);
    CHECK(memcmp(a.context_id,b.context_id,32));
    CHECK(memcmp(a.account_id,b.account_id,32));
    /* Fresh file description and child process restore exactly the same ID. */
    pid_t child=fork(); CHECK(child>=0);
    if(!child) {
        int reopened=open(path,O_RDONLY|O_DIRECTORY); CHECK(reopened>=0);
        CHECK(apply(reopened,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_OK);
        CHECK(!memcmp(&out,&b,sizeof(out))); close(reopened); _exit(0);
    }
    int status; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    req.name="alice"; req.uid=1000; req.expected=&b;
    CHECK(apply(fd,CFP_REGISTRY_RETIRE,NULL)==CFP_STORE_RECOVERY);
    req.expected=&a; CHECK(apply(fd,CFP_REGISTRY_RETIRE,NULL)==CFP_STORE_OK);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_MISSING);
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_OK);
    CHECK(memcmp(a.account_id,out.account_id,32) && memcmp(a.context_id,out.context_id,32));
    CHECK(apply(fd,CFP_REGISTRY_RETIRE,NULL)==CFP_STORE_RECOVERY); /* stale retirement */
    struct cfp_bound_print old={.binding=a,.finger=1,.blob=(const uint8_t *)"x",.blob_size=1};
    CHECK(cfp_validate_gallery(&out,&old,1,64,5)!=CFP_OK);
    req.name="bob"; req.uid=1001; CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_OK);
    CHECK(!memcmp(&out,&b,sizeof(out)));
    req.reader_generation[31]^=1;
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_RECOVERY);
    cleanup(fd);
}
static void failures(void)
{
    for(unsigned f=1;f<=9;f++) {
        int fd=fresh(); struct cfp_binding out,before;
        CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
        memset(&before,0xa5,sizeof(before)); out=before;
        failure=f; sync_calls=0;
        CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==
            ((f<=2 || f>=7)?CFP_STORE_RANDOM:CFP_STORE_IO));
        CHECK(!memcmp(&out,&before,sizeof(out)));
        failure=0;
        enum cfp_store_status loaded=apply(fd,CFP_REGISTRY_LOOKUP,&out);
        CHECK(loaded==((f<=2 || f>=7)?CFP_STORE_MISSING:
            f==6?CFP_STORE_OK:CFP_STORE_RECOVERY));
        cleanup(fd);
    }
}

static size_t snapshot(int fd,unsigned char data[10000])
{
    int file=openat(fd,"registry",O_RDONLY); CHECK(file>=0);
    ssize_t n=read(file,data,10000); CHECK(n>0); close(file); return (size_t)n;
}
static void overwrite(int fd,const unsigned char *data,size_t n)
{
    int file=openat(fd,"registry",O_WRONLY|O_TRUNC); CHECK(file>=0);
    CHECK(write(file,data,n)==(ssize_t)n); CHECK(close(file)==0);
}
static void checksum(unsigned char *data,size_t n)
{
    unsigned size=0;
    CHECK(EVP_Digest(data,n-32,data+n-32,&size,EVP_sha256(),NULL)==1 && size==32);
}
static void corruption(void)
{
    int fd=fresh(); struct cfp_binding out,before;
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_OK);
    unsigned char original[10000],bad[10000]; size_t n=snapshot(fd,original);
    memset(&before,0xa5,sizeof(before));
    /* Every truncation, a trailing byte, and every changed byte. */
    for(size_t i=0;i<n;i++) {
        overwrite(fd,original,i); out=before;
        CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_CORRUPT);
        CHECK(!memcmp(&out,&before,sizeof(out)));
    }
    memcpy(bad,original,n); bad[n]=0; overwrite(fd,bad,n+1);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_CORRUPT);
    for(size_t i=0;i<n;i++) {
        memcpy(bad,original,n); bad[i]^=1; overwrite(fd,bad,n); out=before;
        CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_CORRUPT);
        CHECK(!memcmp(&out,&before,sizeof(out)));
    }
    /* Valid checksums do not excuse malformed semantic fields. */
    for(unsigned f=0;f<8;f++) {
        memcpy(bad,original,n);
        switch(f) {
        case 0: bad[7]='2'; break; /* unknown format */
        case 1: memset(bad+8,0,32); break;
        case 2: bad[40]=65; break;
        case 3: memset(bad+44+8,'x',64); break; /* no name terminator */
        case 4: bad[44+8+6]=1; break; /* noncanonical name padding */
        case 5: memset(bad+44+72,0,32); break;
        case 6: bad[44+136]=2; break;
        case 7: bad[44+137]=1; break;
        }
        checksum(bad,n); overwrite(fd,bad,n);
        CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_CORRUPT);
    }
    overwrite(fd,original,n); CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_OK);
    req.name="bob"; req.uid=1001;
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_OK);
    n=snapshot(fd,original);
    for(unsigned f=0;f<4;f++) {
        memcpy(bad,original,n);
        if(f==0) memcpy(bad+44+144,bad+44,8); /* duplicate active UID */
        if(f==1) memcpy(bad+44+144+8,bad+44+8,64); /* duplicate name */
        if(f==2) memcpy(bad+44+144+72,bad+44+72,32); /* duplicate account */
        if(f==3) memcpy(bad+44+144+104,bad+44+104,32); /* duplicate context */
        checksum(bad,n); overwrite(fd,bad,n);
        CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_CORRUPT);
    }
    cleanup(fd);
}
static void unsafe_state(void)
{
    int fd=fresh(); struct cfp_binding out;
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
    CHECK(fchmod(fd,0755)==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_UNSAFE);
    CHECK(fchmod(fd,0700)==0);
    req.action=CFP_REGISTRY_LOOKUP;
    CHECK(cfp_registry_apply(fd,geteuid()+1,&ops,&req,&out)==CFP_STORE_UNSAFE);
    CHECK(fchmodat(fd,"registry",0644,0)==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_UNSAFE);
    CHECK(fchmodat(fd,"registry",0600,0)==0);
    CHECK(linkat(fd,"registry",fd,"alias",0)==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_UNSAFE);
    CHECK(unlinkat(fd,"registry",0)==0);
    CHECK(symlinkat("alias",fd,"registry")==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_UNSAFE);
    CHECK(unlinkat(fd,"registry",0)==0);
    CHECK(mkfifoat(fd,"registry",0600)==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_UNSAFE);
    CHECK(unlinkat(fd,"registry",0)==0);
    CHECK(mkdirat(fd,"registry",0700)==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_UNSAFE);
    CHECK(unlinkat(fd,"registry",AT_REMOVEDIR)==0);
    CHECK(renameat(fd,"alias",fd,"registry")==0);
    CHECK(symlinkat("absent",fd,".registry.pending")==0);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_RECOVERY);
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_EXISTS);
    cleanup(fd);
}
static void retirement_faults(void)
{
    for(unsigned f=3;f<=6;f++) {
        int fd=fresh(); struct cfp_binding a,out;
        unsigned char old[10000],now[10000];
        CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
        CHECK(apply(fd,CFP_REGISTRY_REGISTER,&a)==CFP_STORE_OK);
        size_t n=snapshot(fd,old);
        req.expected=&a; failure=f; sync_calls=0;
        CHECK(apply(fd,CFP_REGISTRY_RETIRE,NULL)==CFP_STORE_IO);
        failure=0;
        CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==
            (f==6?CFP_STORE_MISSING:CFP_STORE_RECOVERY));
        if(f!=6) { CHECK(snapshot(fd,now)==n); CHECK(!memcmp(old,now,n)); }
        cleanup(fd);
    }
}
static void collisions_and_limit(void)
{
    int fd=fresh(); struct cfp_binding a,out;
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&a)==CFP_STORE_OK);
    req.expected=&a; CHECK(apply(fd,CFP_REGISTRY_RETIRE,NULL)==CFP_STORE_OK);
    random_calls=0; /* repeat old IDs, including a retired context */
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_RANDOM);
    for(unsigned i=1;i<CFP_REGISTRY_MAX_ENTRIES;i++) {
        CHECK(apply(fd,CFP_REGISTRY_REGISTER,&a)==CFP_STORE_OK);
        req.expected=&a; CHECK(apply(fd,CFP_REGISTRY_RETIRE,NULL)==CFP_STORE_OK);
    }
    unsigned calls=random_calls;
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_RECOVERY);
    CHECK(random_calls==calls); /* never recycle tombstones */
    cleanup(fd);
}
static void concurrency(void)
{
    int fd=fresh(); struct cfp_binding out;
    CHECK(apply(fd,CFP_REGISTRY_INITIALIZE,NULL)==CFP_STORE_OK);
    int held=openat(fd,".",O_RDONLY|O_DIRECTORY); CHECK(held>=0);
    CHECK(flock(held,LOCK_EX|LOCK_NB)==0);
    CHECK(apply(fd,CFP_REGISTRY_REGISTER,&out)==CFP_STORE_BUSY);
    CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_BUSY);
    CHECK(random_calls==0); close(held);
    int start[2]; CHECK(pipe(start)==0); pid_t children[2];
    for(unsigned i=0;i<2;i++) {
        children[i]=fork(); CHECK(children[i]>=0);
        if(!children[i]) {
            close(start[1]); char byte;
            CHECK(read(start[0],&byte,1)==1); close(start[0]);
            _exit((int)apply(fd,CFP_REGISTRY_REGISTER,&out));
        }
    }
    close(start[0]); CHECK(write(start[1],"xx",2)==2); close(start[1]);
    unsigned winners=0;
    for(unsigned i=0;i<2;i++) {
        int status; CHECK(waitpid(children[i],&status,0)==children[i] && WIFEXITED(status));
        int result=WEXITSTATUS(status);
        CHECK(result==CFP_STORE_OK || result==CFP_STORE_BUSY || result==CFP_STORE_EXISTS);
        if(result==CFP_STORE_OK) winners++;
    }
    CHECK(winners==1); CHECK(apply(fd,CFP_REGISTRY_LOOKUP,&out)==CFP_STORE_OK);
    cleanup(fd);
}

int main(void)
{
    lifecycle(); failures(); corruption(); unsafe_state(); retirement_faults();
    collisions_and_limit(); concurrency(); puts("Persistent account registry checks passed"); return 0;
}

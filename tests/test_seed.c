/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_seed.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int random_calls, failure, sync_calls;
static ssize_t random_fake(void *p,size_t n)
{
    CHECK(n==32); random_calls++; memset(p,0x5a,n);
    if(failure==1) return -1;
    if(failure==2) return 12;
    return (ssize_t)n;
}
static ssize_t write_fake(int fd,const void *p,size_t n)
{
    if(failure==3) { errno=ENOSPC; return -1; }
    return write(fd,p,n>7?7:n);
}
static int sync_fake(int fd)
{
    sync_calls++;
    if((failure==4 && sync_calls==1) || (failure==6 && sync_calls==2)) {
        errno=EIO; return -1;
    }
    return fsync(fd);
}
static int publish_fake(int a,const char *b,int c,const char *d,int flags)
{
    if(failure==5) { errno=EIO; return -1; }
    return linkat(a,b,c,d,flags);
}
static struct cfp_seed_ops ops={random_fake,write_fake,sync_fake,publish_fake};
static char path[128];
static int fresh(void)
{
    strcpy(path,"/tmp/cfp-seed-test-XXXXXX"); CHECK(mkdtemp(path));
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC); CHECK(fd>=0);
    failure=0; sync_calls=0; random_calls=0; return fd;
}
static void cleanup(int fd)
{
    unlinkat(fd,"seed",0); unlinkat(fd,".seed.pending",0);
    unlinkat(fd,"context",0); unlinkat(fd,"alias",0);
    close(fd); CHECK(rmdir(path)==0);
}
static void basic(void)
{
    int fd=fresh(); unsigned char out[32], before[32]; memset(before,0xa5,32); memcpy(out,before,32);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_MISSING);
    CHECK(!memcmp(out,before,32) && random_calls==0);
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_OK);
    CHECK(random_calls==1 && sync_calls>=2);
    for(unsigned i=0;i<32;i++) CHECK(out[i]==0x5a);
    memset(out,0,32);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_OK && out[0]==0x5a);
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_EXISTS && random_calls==1);
    CHECK(unlinkat(fd,"seed",0)==0);
    int record=openat(fd,"context",O_CREAT|O_EXCL|O_WRONLY,0600); CHECK(record>=0); close(record);
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_EXISTS && random_calls==1);
    cleanup(fd);
}
static void faults(void)
{
    for(int which=1;which<=6;which++) {
        int fd=fresh(); failure=which;
        unsigned char out[32], before[32]; memset(before,0xa5,32); memcpy(out,before,32);
        CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==(which<=2?CFP_STORE_RANDOM:CFP_STORE_IO));
        CHECK(!memcmp(out,before,32));
        if(which>=3) CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_EXISTS);
        cleanup(fd);
    }
}
static void unsafe(void)
{
    int fd=fresh(); unsigned char out[32]; memset(out,0xa5,32);
    CHECK(fchmod(fd,0755)==0);
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_UNSAFE && random_calls==0);
    CHECK(fchmod(fd,0700)==0);
    CHECK(cfp_seed_create(fd,geteuid()+1,&ops,out)==CFP_STORE_UNSAFE);
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_OK);
    int file=openat(fd,"seed",O_RDWR); CHECK(file>=0);
    CHECK(fchmod(file,0644)==0);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_UNSAFE);
    CHECK(fchmod(file,0600)==0);
    unsigned char byte=0;
    CHECK(pwrite(file,&byte,1,12)==1);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_CORRUPT);
    CHECK(ftruncate(file,12)==0);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_CORRUPT);
    close(file); CHECK(unlinkat(fd,"seed",0)==0);
    CHECK(symlinkat("/dev/zero",fd,"seed")==0);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_UNSAFE);
    cleanup(fd);
}
static void records(void)
{
    int fd=fresh(); unsigned char out[32], before[32], record[72];
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_OK);
    int file=openat(fd,"seed",O_RDWR); CHECK(file>=0);
    CHECK(pread(file,record,72,0)==72);
    memset(before,0xa5,32);
    for(size_t i=0;i<72;i++) {
        unsigned char changed=record[i]^1;
        CHECK(pwrite(file,&changed,1,(off_t)i)==1);
        memcpy(out,before,32);
        CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_CORRUPT);
        CHECK(!memcmp(out,before,32));
        CHECK(pwrite(file,record+i,1,(off_t)i)==1);
    }
    CHECK(pwrite(file,"x",1,72)==1);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_CORRUPT);
    for(off_t length=0;length<72;length++) {
        CHECK(ftruncate(file,length)==0);
        CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_CORRUPT);
        CHECK(!memcmp(out,before,32));
    }
    CHECK(pwrite(file,record,72,0)==72);
    CHECK(linkat(fd,"seed",fd,"alias",0)==0);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_UNSAFE);
    CHECK(unlinkat(fd,"alias",0)==0);
    failure=4; sync_calls=0;
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_IO);
    CHECK(!memcmp(out,before,32));
    close(file); cleanup(fd);

    fd=fresh(); failure=4;
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_IO);
    failure=0;
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_RECOVERY);
    cleanup(fd);
    fd=fresh(); failure=6;
    CHECK(cfp_seed_create(fd,geteuid(),&ops,out)==CFP_STORE_IO);
    failure=0;
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_OK);
    cleanup(fd);
    fd=fresh(); CHECK(mkfifoat(fd,"seed",0600)==0);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_UNSAFE);
    cleanup(fd);
}
static void context_record(void)
{
    int fd=fresh(); unsigned char out[32];
    CHECK(cfp_context_id_create(fd,geteuid(),&ops,out)==CFP_STORE_OK);
    CHECK(cfp_context_id_load(fd,geteuid(),&ops,out)==CFP_STORE_OK && out[0]==0x5a);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_MISSING);
    CHECK(renameat(fd,"context",fd,"seed")==0);
    CHECK(cfp_seed_load(fd,geteuid(),&ops,out)==CFP_STORE_CORRUPT);
    cleanup(fd);
}
static void concurrent(void)
{
    int fd=fresh(), gate[2]; CHECK(pipe(gate)==0);
    pid_t children[2];
    for(int i=0;i<2;i++) {
        children[i]=fork(); CHECK(children[i]>=0);
        if(children[i]==0) {
            close(gate[1]); char go; CHECK(read(gate[0],&go,1)==1);
            unsigned char seed[32];
            enum cfp_store_status s=cfp_seed_create(fd,geteuid(),&ops,seed);
            _exit(s==CFP_STORE_OK?0:(s==CFP_STORE_EXISTS || s==CFP_STORE_BUSY)?10:20);
        }
    }
    close(gate[0]); CHECK(write(gate[1],"xx",2)==2); close(gate[1]);
    int successes=0;
    for(int i=0;i<2;i++) {
        int status; CHECK(waitpid(children[i],&status,0)==children[i] && WIFEXITED(status));
        CHECK(WEXITSTATUS(status)==0 || WEXITSTATUS(status)==10);
        successes+=WEXITSTATUS(status)==0;
    }
    CHECK(successes==1); cleanup(fd);
}
int main(int argc,char **argv)
{
    if(argc==1 || !strcmp(argv[1],"basic")) basic();
    if(argc==1 || !strcmp(argv[1],"faults")) faults();
    if(argc==1 || !strcmp(argv[1],"unsafe")) unsafe();
    if(argc==1 || !strcmp(argv[1],"records")) records();
    if(argc==1 || !strcmp(argv[1],"context_record")) context_record();
    if(argc==1 || !strcmp(argv[1],"concurrent")) concurrent();
    puts("Seed-store checks passed");
}

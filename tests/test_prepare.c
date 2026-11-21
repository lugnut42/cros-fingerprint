/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_prepare.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct cfp_store store;
static struct cfp_binding account;
static unsigned calls,writes,seed_writes,fail_at,late_at,cancel_at,malformed_at,unsupported;
static int seeded,templates,busy,wrong_target;
static uint32_t mode,post_mode;
static unsigned hang_at;
static int root_fd, notification_fd, device_fd;
static int64_t now;
static ssize_t rng(void *p,size_t n)
{ CHECK(n==64); memset(p,0x33,32); memset((char *)p+32,0x44,32); return 64; }
static const struct cfp_seed_ops seeds={rng,write,fsync,linkat};
static const struct cfp_registry_ops accounts={rng,write,fsync,renameat};
static int64_t time_now(void *p) { (void)p; return now; }
static int wait_ms(void *p,unsigned ms) { (void)p; now+=ms; return 0; }
static const struct cfp_smoke_clock clock_ops={time_now,wait_ms,NULL};
static void number(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;i++) { p[i]=(uint8_t)n; n>>=8; } }
static enum cfp_status exchange(void *p,uint16_t cmd,uint8_t ver,const uint8_t *req,
    size_t n,uint8_t *out,size_t cap,size_t *size,uint32_t *result)
{
    (void)p; calls++; *size=0; *result=0; if(cap) memset(out,0,cap);
    if(calls==hang_at) {
        /* Independent opens must contend while the worker owns both locks. */
        struct cfp_store contender;
        CHECK(cfp_store_open(root_fd,geteuid(),0,NULL,NULL,&contender)==CFP_STORE_BUSY);
        int other=openat(root_fd,"fake-device",O_RDONLY|O_CLOEXEC);
        CHECK(other>=0);
        CHECK(flock(other,LOCK_EX|LOCK_NB)<0 && errno==EWOULDBLOCK);
        close(other);
        pid_t self=getpid();
        CHECK(write(notification_fd,&self,sizeof(self))==(ssize_t)sizeof(self));
        for(;;) pause();
    }
    if(calls==cancel_at) cfp_seed_session_invalidate(&store.session);
    if(calls==late_at) now+=6000;
    if(calls==fail_at) return CFP_TRANSPORT_ERROR;
    if(cmd==8) {
        CHECK(ver==1 && n==2 && cap==4 && req[1]==4);
        *size=4; out[0]=req[0]==3?12:req[0]==6?2:1;
        if(req[0]==unsupported) out[0]=0;
    } else if(cmd==0x403) {
        CHECK(ver==3 && cap>=36); *size=36;
        number(out,541282374); number(out+4,9); number(out+8,wrong_target?544:543);
        number(out+20,5156); out[24]=5; out[26]=(uint8_t)templates; number(out+32,4);
    } else if(cmd==0x402) {
        CHECK(ver==0 && n==4 && cap==4 && !memcmp(req,"\0\0\0\200",4));
        *size=4; number(out,seeded?post_mode:mode);
    } else if(cmd==0x409) {
        CHECK(ver==0 && n==0 && cap==8); *size=8; out[0]=1; out[4]=(uint8_t)seeded;
    } else if(cmd==0x408) {
        CHECK(ver==0 && n==36 && cap==0 && req[0]==4);
        CHECK(!memcmp(req+4,store.provision.seed,32)); seeded=1; writes++; seed_writes++;
    } else {
        CHECK(cmd==0x406 && ver==1 && n==36 && cap==0 && seeded);
        CHECK(!memcmp(req+4,account.context_id,32)); writes++;
        if(req[0]==1 && busy) *result=16;
        else CHECK(req[0]==0 || req[0]==1);
    }
    if(calls==malformed_at) *size=cap+1;
    return CFP_OK;
}
static void reset(void)
{
    calls=writes=seed_writes=fail_at=late_at=cancel_at=malformed_at=unsupported=0;
    seeded=templates=busy=wrong_target=0; mode=post_mode=0; now=0;
    cfp_seed_session_invalidate(&store.session);
}
/* Exercise the real supervisor and preparation workflow without a reader.
 * The parent closes its store before fork, matching CLI ownership semantics. */
static void hanging_worker(void *unused,struct cfp_report *report)
{
    (void)unused;
    CHECK(cfp_store_open(root_fd,geteuid(),0,NULL,NULL,&store)==CFP_STORE_OK);
    device_fd=openat(root_fd,"fake-device",O_RDONLY|O_CLOEXEC);
    CHECK(device_fd>=0 && flock(device_fd,LOCK_EX|LOCK_NB)==0);
    struct cfp_transport transport={exchange,NULL};
    cfp_prepare_account(&store,&transport,1000,"alice",NULL,&clock_ops,5000,report);
    close(device_fd);
    cfp_store_close(&store);
}
static void supervisor_checks(int fd,unsigned stages)
{
    cfp_store_close(&store);
    root_fd=fd;
    int fake=openat(fd,"fake-device",O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);
    CHECK(fake>=0); close(fake);
    for(unsigned stage=1;stage<=stages;stage++) {
        reset(); hang_at=stage;
        int notifications[2]; CHECK(pipe2(notifications,O_CLOEXEC|O_NONBLOCK)==0);
        notification_fd=notifications[1];
        struct cfp_report report={.status=CFP_RECOVERY_REQUIRED};
        int outcome=cfp_supervise(hanging_worker,NULL,250,&report);
        close(notifications[1]);
        pid_t child;
        CHECK(read(notifications[0],&child,sizeof(child))==(ssize_t)sizeof(child));
        close(notifications[0]);
        /* SIGKILL/reaping is asynchronous; bound the wait and clean up even if
         * a regression stops the supervisor from terminating its worker. */
        int exited=0;
        for(unsigned i=0;i<1000;i++) {
            pid_t result=waitpid(child,NULL,WNOHANG);
            if(result==child || (result<0 && errno==ECHILD)) { exited=1; break; }
            struct timespec delay={.tv_nsec=1000000}; nanosleep(&delay,NULL);
        }
        if(!exited) { kill(child,SIGKILL); waitpid(child,NULL,0); }
        CHECK(exited);
        CHECK(outcome==124 && report.status==CFP_RECOVERY_REQUIRED);
        CHECK(cfp_store_open(fd,geteuid(),0,NULL,NULL,&store)==CFP_STORE_OK);
        CHECK(store.session.phase==CFP_SESSION_BLOCKED);
        cfp_store_close(&store);
        fake=openat(fd,"fake-device",O_RDONLY|O_CLOEXEC);
        CHECK(fake>=0 && flock(fake,LOCK_EX|LOCK_NB)==0); close(fake);
    }
    hang_at=0;
    CHECK(unlinkat(fd,"fake-device",0)==0);
    CHECK(cfp_store_open(fd,geteuid(),0,NULL,NULL,&store)==CFP_STORE_OK);
}
int main(void)
{
    char path[]="/tmp/cfp-prepare-XXXXXX"; CHECK(mkdtemp(path));
    int fd=open(path,O_RDONLY|O_DIRECTORY); CHECK(fd>=0);
    CHECK(cfp_store_open(fd,geteuid(),1,&seeds,&accounts,&store)==CFP_STORE_OK);
    CHECK(cfp_store_account(&store,&accounts,CFP_REGISTRY_REGISTER,1000,"alice",NULL,&account)==CFP_STORE_OK);
    struct cfp_transport t={exchange,NULL}; struct cfp_report report;
    reset();
    CHECK(cfp_prepare_account(&store,&t,999,"absent",&accounts,&clock_ops,5000,&report)==CFP_STORAGE_ERROR);
    CHECK(calls==0);
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_OK);
    CHECK(seed_writes==1 && writes==3 && store.session.phase==CFP_SESSION_SEEDED);
    unsigned total=calls;
    for(unsigned stage=1;stage<=total;stage++) for(unsigned kind=0;kind<4;kind++) {
        reset();
        if(kind==0) fail_at=stage;
        if(kind==1) late_at=stage;
        if(kind==2) cancel_at=stage;
        if(kind==3) malformed_at=stage;
        enum cfp_status status=cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report);
        CHECK(status!=CFP_OK && calls==stage && seed_writes<=1);
        CHECK(store.session.phase==CFP_SESSION_BLOCKED && report.status==status);
    }
    reset(); mode=16;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_RECOVERY_REQUIRED && writes==0);
    reset(); post_mode=128;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_RECOVERY_REQUIRED);
    CHECK(store.session.phase==CFP_SESSION_BLOCKED);
    reset(); seeded=1;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_RECOVERY_REQUIRED && writes==0);
    reset(); templates=1;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_RECOVERY_REQUIRED && writes==0);
    reset(); wrong_target=1;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_RECOVERY_REQUIRED && writes==0);
    reset(); unsupported=6;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_UNSUPPORTED_VERSION && writes==0);
    reset(); busy=1;
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_TIMEOUT);
    CHECK(now<=2100 && store.session.phase==CFP_SESSION_BLOCKED);
    supervisor_checks(fd,total);
    reset();
    CHECK(cfp_store_account(&store,&accounts,CFP_REGISTRY_RETIRE,1000,"alice",&account,NULL)==CFP_STORE_OK);
    CHECK(cfp_prepare_account(&store,&t,1000,"alice",&accounts,&clock_ops,5000,&report)==CFP_STORAGE_ERROR && calls==0);
    cfp_store_close(&store);
    int dir=openat(fd,"provision",O_RDONLY|O_DIRECTORY); CHECK(dir>=0);
    CHECK(unlinkat(dir,"provision",0)==0); close(dir); CHECK(unlinkat(fd,"provision",AT_REMOVEDIR)==0);
    dir=openat(fd,"accounts",O_RDONLY|O_DIRECTORY); CHECK(dir>=0);
    CHECK(unlinkat(dir,"registry",0)==0); close(dir); CHECK(unlinkat(fd,"accounts",AT_REMOVEDIR)==0);
    close(fd); CHECK(rmdir(path)==0);
    puts("Account preparation workflow checks passed"); return 0;
}

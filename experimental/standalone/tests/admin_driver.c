/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_admin.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
/* Only this separately linked test executable reads these environment values.
 * All records are synthetic and all paths belong to a temporary test root. */
static unsigned tag=1;
static int fault(const char *s)
{ const char *f=getenv("CFP_TEST_FAULT"); return f && !strcmp(f,s); }
static ssize_t random_bytes(void *p,size_t n)
{
    struct rlimit limit;
    if(prctl(PR_GET_DUMPABLE)!=0 || getrlimit(RLIMIT_CORE,&limit)<0 ||
        limit.rlim_cur!=0 || limit.rlim_max!=0) return -1;
    if(fault("random")) { errno=EIO; return -1; }
    if(n!=64) return -1;
    memset(p,(int)tag,32); memset((char *)p+32,(int)tag+64,32); tag++; return 64;
}
static ssize_t write_bytes(int fd,const void *p,size_t n)
{
    if(fault("write-hang")) for(;;) pause();
    if(fault("write")) { errno=EIO; return -1; }
    return write(fd,p,n);
}
static int sync_fd(int fd)
{ if(fault("sync")) { errno=EIO; return -1; } return fsync(fd); }
static int account_exists(uint64_t uid,const char *name)
{
    if(fault("lookup-hang")) for(;;) pause();
    tag=uid==1000?2:3;
    return (uid==1000 && !strcmp(name,"alice")) || (uid==1001 && !strcmp(name,"bob"));
}
int main(int argc,char **argv)
{
    const struct cfp_seed_ops seeds={random_bytes,write_bytes,sync_fd,linkat};
    const struct cfp_registry_ops accounts={random_bytes,write_bytes,sync_fd,renameat};
    const struct cfp_admin_platform platform={
        .anchor=getenv("CFP_TEST_ROOT"),.relative_path="state",
        .owner=fault("unauthorized")?geteuid()+1:geteuid(),
        .account_exists=account_exists,.seeds=&seeds,.accounts=&accounts,
        .timeout_ms=(fault("lookup-hang") || fault("write-hang"))?100:5000
    };
    return cfp_admin_cli(argc,argv,&platform);
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_state.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void directories(void)
{
    char path[]="/tmp/cfp-state-test-XXXXXX"; CHECK(mkdtemp(path));
    int anchor=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC), out=12345;
    CHECK(anchor>=0); uid_t owner=geteuid();
    CHECK(cfp_state_directory(anchor,"state",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_MISSING);
    CHECK(out==12345);
    CHECK(cfp_state_directory(anchor,"state",owner,CFP_DIRECTORY_CREATE,&out)==CFP_STORE_OK);
    struct stat st; CHECK(fstat(out,&st)==0 && (st.st_mode&07777)==0700 && st.st_uid==owner);
    CHECK(fcntl(out,F_GETFD)&FD_CLOEXEC); close(out); out=12345;
    CHECK(cfp_state_directory(anchor,"state",owner,CFP_DIRECTORY_CREATE,&out)==CFP_STORE_EXISTS && out==12345);
    CHECK(cfp_state_directory(anchor,"state",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_OK); close(out);
    CHECK(mkdirat(anchor,"parent",0755)==0);
    CHECK(cfp_state_directory(anchor,"parent/child",owner,CFP_DIRECTORY_CREATE,&out)==CFP_STORE_OK); close(out);
    int parent=openat(anchor,"parent",O_RDONLY|O_DIRECTORY); CHECK(parent>=0);
    CHECK(fchmod(parent,0777)==0); out=12345;
    CHECK(cfp_state_directory(anchor,"parent/child",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_UNSAFE && out==12345);
    CHECK(fchmod(parent,0755)==0);
    CHECK(symlinkat("parent",anchor,"alias")==0);
    CHECK(cfp_state_directory(anchor,"alias/child",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_UNSAFE);
    CHECK(symlinkat("state",anchor,"leaf-alias")==0);
    CHECK(cfp_state_directory(anchor,"leaf-alias",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_UNSAFE);
    const char *bad[]={"", "/state", "../state", "parent/../state", "./state", "parent//child", "state/"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        CHECK(cfp_state_directory(anchor,bad[i],owner,CFP_DIRECTORY_CREATE,&out)==CFP_STORE_UNSAFE);
    CHECK(cfp_state_directory(anchor,"missing/child",owner,CFP_DIRECTORY_CREATE,&out)==CFP_STORE_MISSING);
    CHECK(cfp_state_directory(anchor,"state",owner+1,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_UNSAFE);
    int leaf=openat(anchor,"state",O_RDONLY|O_DIRECTORY); CHECK(leaf>=0);
    CHECK(fchmod(leaf,0755)==0);
    CHECK(cfp_state_directory(anchor,"state",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_UNSAFE);
    CHECK(fchmod(leaf,0700)==0); close(leaf);
    CHECK(fchmod(anchor,0777)==0);
    CHECK(cfp_state_directory(anchor,"state",owner,CFP_DIRECTORY_OPEN,&out)==CFP_STORE_UNSAFE);
    CHECK(fchmod(anchor,0700)==0);
    CHECK(unlinkat(parent,"child",AT_REMOVEDIR)==0); close(parent);
    CHECK(unlinkat(anchor,"parent",AT_REMOVEDIR)==0);
    CHECK(unlinkat(anchor,"state",AT_REMOVEDIR)==0);
    CHECK(unlinkat(anchor,"alias",0)==0); CHECK(unlinkat(anchor,"leaf-alias",0)==0);
    close(anchor); CHECK(rmdir(path)==0);
}
static void recovery(void)
{
    struct cfp_seed_session s={0};
    CHECK(cfp_seed_session_begin(&s,CFP_STORE_OK,1,0)==CFP_SESSION_RESTORE_PENDING);
    uint64_t first=s.generation; CHECK(first!=0);
    cfp_seed_session_complete(&s,first,1); CHECK(s.phase==CFP_SESSION_SEEDED);
    cfp_seed_session_invalidate(&s); CHECK(s.phase==CFP_SESSION_BLOCKED && s.generation!=first);
    cfp_seed_session_complete(&s,first,1); CHECK(s.phase==CFP_SESSION_BLOCKED);
    CHECK(cfp_seed_session_begin(&s,CFP_STORE_OK,1,1)==CFP_SESSION_BLOCKED);
    CHECK(cfp_seed_session_begin(&s,CFP_STORE_OK,0,0)==CFP_SESSION_BLOCKED);
    for(int store=CFP_STORE_MISSING;store<=CFP_STORE_RECOVERY;store++)
        CHECK(cfp_seed_session_begin(&s,store,1,0)==CFP_SESSION_BLOCKED);
    CHECK(cfp_seed_session_begin(&s,CFP_STORE_OK,1,0)==CFP_SESSION_RESTORE_PENDING);
    cfp_seed_session_complete(&s,first,1); CHECK(s.phase==CFP_SESSION_RESTORE_PENDING);
    cfp_seed_session_complete(&s,s.generation,0); CHECK(s.phase==CFP_SESSION_BLOCKED);
    cfp_seed_session_complete(&s,s.generation,1); CHECK(s.phase==CFP_SESSION_BLOCKED);
    s.generation=UINT64_MAX;
    CHECK(cfp_seed_session_begin(&s,CFP_STORE_OK,1,0)==CFP_SESSION_BLOCKED);
    CHECK(s.generation==UINT64_MAX);
}
int main(int argc,char **argv)
{
    if(argc==1 || !strcmp(argv[1],"directories")) directories();
    if(argc==1 || !strcmp(argv[1],"recovery")) recovery();
    puts("State-directory and recovery checks passed");
}

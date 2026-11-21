/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_admin.h"
#include "cros_fp_diagnostic.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
enum action { PROVISION, REGISTER, CHECK_STORE, CHECK_ACCOUNT };
struct request {
    enum action action;
    uint64_t uid;
    const char *name;
    const struct cfp_admin_platform *platform;
};
static const char *action_name(enum action action)
{
    switch(action) {
    case PROVISION: return "provision";
    case REGISTER: return "register";
    case CHECK_STORE: return "check";
    case CHECK_ACCOUNT: return "check-account";
    }
    return "invalid";
}
static int parse(int argc,char **argv,struct request *q)
{
    if(argc==2 && !strcmp(argv[1],"--provision")) { q->action=PROVISION; return 1; }
    if(argc==2 && !strcmp(argv[1],"--check")) { q->action=CHECK_STORE; return 1; }
    if(argc!=4 || (strcmp(argv[1],"--register") && strcmp(argv[1],"--check-account"))) return 0;
    q->action=!strcmp(argv[1],"--register")?REGISTER:CHECK_ACCOUNT;
    if(!*argv[2]) return 0;
    for(const char *p=argv[2];*p;p++) {
        if(*p<'0' || *p>'9' || q->uid>(UINT64_MAX-(unsigned)(*p-'0'))/10) return 0;
        q->uid=q->uid*10+(unsigned)(*p-'0');
    }
    size_t length=strnlen(argv[3],64);
    if(!length || length==64) return 0;
    for(size_t i=0;i<length;i++) {
        char c=argv[3][i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    q->name=argv[3]; return 1;
}
static void worker(void *context,struct cfp_report *report)
{
    const struct request *q=context;
    const struct cfp_admin_platform *p=q->platform;
    int anchor=-1,root=-1,opened=0;
    struct cfp_store store;
    struct cfp_binding binding={0};
    enum cfp_store_status status=CFP_STORE_UNSAFE;
    /* Lookup is inside the deadline and precedes all filesystem changes.
     * Name/UID matching does not detect OS account deletion/recreation. */
    if((q->action==REGISTER || q->action==CHECK_ACCOUNT) &&
        p->account_exists(q->uid,q->name)!=1) goto done;
    anchor=open(p->anchor,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(anchor<0) goto done;
    status=cfp_state_directory(anchor,p->relative_path,p->owner,
        q->action==PROVISION?CFP_DIRECTORY_CREATE:CFP_DIRECTORY_OPEN,&root);
    if(status!=CFP_STORE_OK) goto done;
    status=cfp_store_open(root,p->owner,q->action==PROVISION,p->seeds,p->accounts,&store);
    if(status!=CFP_STORE_OK) goto done;
    opened=1;
    if(q->action==REGISTER || q->action==CHECK_ACCOUNT)
        status=cfp_store_account(&store,p->accounts,
            q->action==REGISTER?CFP_REGISTRY_REGISTER:CFP_REGISTRY_LOOKUP,
            q->uid,q->name,NULL,&binding);
 done:
    cfp_seed_erase(&binding,sizeof(binding));
    if(opened) cfp_store_close(&store);
    if(root>=0) close(root);
    if(anchor>=0) close(anchor);
    report->status=status==CFP_STORE_OK?CFP_OK:CFP_STORAGE_ERROR;
    /* Internal report carries only the whitelisted storage status, no records. */
    report->error_number=(int)status;
}
int cfp_admin_cli(int argc,char **argv,const struct cfp_admin_platform *platform)
{
    if(argc==2 && !strcmp(argv[1],"--plan")) {
        int failed=puts("{\"state_directory\":\"/" CFP_PROTOTYPE_STATE "\","
            "\"actions\":[\"provision\",\"register\",\"check\",\"check-account\"],"
            "\"hardware_access\":false,\"deadline_ms\":5000}")<0;
        return failed || fflush(stdout)==EOF;
    }
    struct request q={.platform=platform};
    if(!parse(argc,argv,&q)) {
        fputs("Usage: cros-fingerprint-admin --plan | --provision | --check | --register UID NAME | --check-account UID NAME\n",stderr);
        return 2;
    }
    if(!platform || !platform->anchor || !platform->relative_path ||
        !platform->account_exists || !platform->timeout_ms || platform->timeout_ms>5000) {
        fputs("Invalid administrator platform configuration\n",stderr); return 1;
    }
    if(geteuid()!=platform->owner) {
        fputs("Administrative storage operations require an explicit administrator invocation\n",stderr); return 1;
    }
    struct rlimit limit={0,0};
    if(setrlimit(RLIMIT_CORE,&limit)<0 || prctl(PR_SET_DUMPABLE,0)<0) {
        fputs("Cannot suppress process dumps\n",stderr); return 1;
    }
    umask(0077);
    struct cfp_report report={0};
    int result=cfp_supervise(worker,&q,platform->timeout_ms,&report);
    if(result) {
        fputs("Administrative worker failed or timed out; preserve state and inspect before retrying\n",stderr);
        return result;
    }
    if(report.status!=CFP_OK) {
        fprintf(stderr,"Administrative operation failed (storage status %d); preserve state and inspect before retrying\n",report.error_number);
        return 1;
    }
    int failed=printf("{\"action\":\"%s\",\"status\":\"ok\",\"hardware_access\":false}\n",action_name(q.action))<0;
    return failed || fflush(stdout)==EOF;
}

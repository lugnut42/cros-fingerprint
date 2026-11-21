/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_prepare.h"
#include "cros_fp_linux.h"
#include "cros_fp_json.h"
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
struct account_request { uint64_t uid; const char *name; };
static int64_t now_ms(void *p)
{
    (void)p; struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)<0) return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int wait_ms(void *p,unsigned ms)
{
    (void)p; struct timespec t={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};
    return nanosleep(&t,NULL);
}
static void worker(void *p,struct cfp_report *report)
{
    const struct account_request *q=p;
    int anchor=-1,root=-1,device=-1,opened=0;
    struct cfp_store store;
    report->status=CFP_STORAGE_ERROR;
    /* NSS lookup is inside the parent deadline. Root still authorizes selection;
     * matching current UID/name does not detect account deletion/recreation. */
    struct passwd *account=getpwnam(q->name);
    if(!account || (uint64_t)account->pw_uid!=q->uid) goto done;
    anchor=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(anchor<0) goto done;
    if(cfp_state_directory(anchor,"var/lib/cros-fingerprint-prototype",0,
        CFP_DIRECTORY_OPEN,&root)!=CFP_STORE_OK) goto done;
    if(cfp_store_open(root,0,0,NULL,NULL,&store)!=CFP_STORE_OK) goto done;
    opened=1;
    /* Check registration before touching even the device descriptor. */
    struct cfp_binding binding={0};
    enum cfp_store_status registered=cfp_store_account(&store,NULL,CFP_REGISTRY_LOOKUP,
        q->uid,q->name,NULL,&binding);
    cfp_seed_erase(&binding,sizeof(binding));
    if(registered!=CFP_STORE_OK) goto done;
    report->status=CFP_TRANSPORT_ERROR;
    device=open("/dev/cros_fp",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(device<0) { report->error_number=errno; goto done; }
    struct stat st;
    if(fstat(device,&st)<0 || !S_ISCHR(st.st_mode) || st.st_uid!=0) goto done;
    if(flock(device,LOCK_EX|LOCK_NB)<0) { report->error_number=errno; goto done; }
    struct cfp_linux backend={device,0,cfp_system_ioctl};
    struct cfp_transport transport={cfp_linux_exchange,&backend};
    struct cfp_smoke_clock clock={now_ms,wait_ms,NULL};
    cfp_prepare_account(&store,&transport,q->uid,q->name,NULL,&clock,5000,report);
    if(backend.error_number) report->error_number=backend.error_number;
done:
    if(device>=0) close(device);
    if(opened) cfp_store_close(&store);
    if(root>=0) close(root);
    if(anchor>=0) close(anchor);
}
static int parse(int argc,char **argv,struct account_request *q)
{
    if(argc!=4 || strcmp(argv[1],"--prepare") || !*argv[2]) return 0;
    uint64_t uid=0;
    for(const char *p=argv[2];*p;p++) {
        if(*p<'0' || *p>'9' || uid>(UINT64_MAX-(unsigned)(*p-'0'))/10) return 0;
        uid=uid*10+(unsigned)(*p-'0');
    }
    size_t length=strnlen(argv[3],64);
    if(!length || length==64) return 0;
    for(size_t i=0;i<length;i++) {
        char c=argv[3][i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    q->uid=uid; q->name=argv[3]; return 1;
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--plan")) {
        puts("{\"device\":\"/dev/cros_fp\",\"state_directory\":\"/var/lib/cros-fingerprint-prototype\","
            "\"state_changing_commands\":[\"FP_SEED\",\"FP_CONTEXT\"],"
            "\"create_state\":false,\"capture\":false,\"deadline_ms\":5000}");
        return 0;
    }
    struct account_request q={0};
    if(!parse(argc,argv,&q)) {
        fputs("Usage: cros-fingerprint-prepare --plan | --prepare UID NAME\n",stderr); return 2;
    }
    if(geteuid()!=0) { fputs("Account preparation requires an explicit administrator invocation\n",stderr); return 1; }
    struct rlimit limit={0,0};
    if(setrlimit(RLIMIT_CORE,&limit)<0 || prctl(PR_SET_DUMPABLE,0)<0) {
        fputs("Cannot suppress process dumps\n",stderr); return 1;
    }
    umask(0077);
    struct cfp_report report={0};
    int result=cfp_supervise(worker,&q,5000,&report);
    if(result) { fputs("Worker failed or timed out; inspect reader state before recovery\n",stderr); return result; }
    if(report.status!=CFP_OK) {
        fprintf(stderr,"Account preparation failed (status %u); no automatic recovery\n",(unsigned)report.status);
        return 1;
    }
    return cfp_print_json(stdout,&report) || fflush(stdout)==EOF;
}

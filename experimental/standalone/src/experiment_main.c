/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_smoke.h"
#include "cros_fp_linux.h"
#include "cros_fp_state.h"
#include "cros_fp_json.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void *context)
{
    (void)context; struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)<0) return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int wait_ms(void *context,unsigned ms)
{
    (void)context;
    struct timespec t={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};
    return nanosleep(&t,NULL);
}
static void worker(void *context,struct cfp_report *report)
{
    int initialize=*(int *)context;
    int device=-1,anchor=-1,state=-1,seed_dir=-1,context_dir=-1;
    report->status=CFP_TRANSPORT_ERROR;
    device=open("/dev/cros_fp",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(device<0) { report->error_number=errno; goto done; }
    struct stat st;
    if(fstat(device,&st)<0 || !S_ISCHR(st.st_mode) || st.st_uid!=0) goto done;
    if(flock(device,LOCK_EX|LOCK_NB)<0) { report->error_number=errno; goto done; }
    struct cfp_linux backend={device,0,cfp_system_ioctl};
    struct cfp_transport transport={cfp_linux_exchange,&backend};
    cfp_collect(&transport,report);
    if(report->status!=CFP_OK) goto done;
    if(report->info.model!=543 || report->info.vendor!=541282374 ||
        report->info.product!=9 || report->info.template_valid ||
        report->info.template_version!=4 || !report->encryption.seed_status_known ||
        report->encryption.seed_set) {
        report->status=CFP_RECOVERY_REQUIRED; goto done;
    }
    report->status=CFP_STORAGE_ERROR;
    anchor=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(anchor<0) goto done;
    enum cfp_directory_action action=initialize?CFP_DIRECTORY_CREATE:CFP_DIRECTORY_OPEN;
    if(cfp_state_directory(anchor,"var/lib/cros-fingerprint-experiment",0,action,&state)
        !=CFP_STORE_OK) goto done;
    if(flock(state,LOCK_EX|LOCK_NB)<0) goto done;
    if(cfp_state_directory(state,"seed",0,action,&seed_dir)!=CFP_STORE_OK) goto done;
    if(cfp_state_directory(state,"context",0,action,&context_dir)!=CFP_STORE_OK) goto done;
    struct cfp_smoke_clock clock={now_ms,wait_ms,NULL};
    report->status=cfp_smoke_run(&transport,seed_dir,context_dir,0,initialize,
        NULL,&clock,report);
    if(backend.error_number) report->error_number=backend.error_number;
done:
    if(context_dir>=0) close(context_dir);
    if(seed_dir>=0) close(seed_dir);
    if(state>=0) close(state);
    if(anchor>=0) close(anchor);
    if(device>=0) close(device);
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--plan")) {
        puts("{\"device\":\"/dev/cros_fp\","
            "\"state_directory\":\"/var/lib/cros-fingerprint-experiment\","
            "\"state_changing_commands\":[\"FP_SEED\",\"FP_CONTEXT\"],"
            "\"capture\":false,\"enroll\":false,\"deadline_ms\":5000}");
        return 0;
    }
    if(argc!=2 || (strcmp(argv[1],"--initialize") && strcmp(argv[1],"--restore"))) {
        fputs("Usage: cros-fingerprint-seed-context-test --plan|--initialize|--restore\n",stderr);
        return 2;
    }
    if(geteuid()!=0) { fputs("This explicit hardware experiment requires root\n",stderr); return 1; }
    struct rlimit limit={0,0};
    if(setrlimit(RLIMIT_CORE,&limit)<0 || prctl(PR_SET_DUMPABLE,0)<0) {
        fputs("Cannot suppress process dumps\n",stderr); return 1;
    }
    umask(0077);
    int initialize=!strcmp(argv[1],"--initialize");
    struct cfp_report report={0};
    int outcome=cfp_supervise(worker,&initialize,5000,&report);
    if(outcome) {
        fputs("Experiment worker failed or timed out; inspect saved state before recovery\n",stderr);
        return outcome;
    }
    if(report.status!=CFP_OK) {
        fprintf(stderr,"Experiment failed (status %u); no automatic reset or state deletion\n",
            (unsigned)report.status);
        if(report.error_number) fprintf(stderr,"Device error: %s\n",strerror(report.error_number));
        return 1;
    }
    return cfp_print_json(stdout,&report) || fflush(stdout)==EOF;
}

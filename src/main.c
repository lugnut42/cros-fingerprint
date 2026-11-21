/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_linux.h"
#include "cros_fp_json.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct device_request { const char *path; int mode_only; };
static void collect_device(void *context,struct cfp_report *report)
{
    const struct device_request *request=context;
    int fd=open(request->path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0) {
        report->status=CFP_TRANSPORT_ERROR; report->error_number=errno; return;
    }
    struct stat st;
    if(fstat(fd,&st)<0) {
        report->status=CFP_TRANSPORT_ERROR; report->error_number=errno;
    } else if(!S_ISCHR(st.st_mode)) {
        report->status=CFP_INVALID_ARGUMENT;
    } else {
        struct cfp_linux device={fd,0,cfp_system_ioctl};
        struct cfp_transport transport={cfp_linux_exchange,&device};
        if(request->mode_only) report->status=cfp_query_mode(&transport,&report->mode);
        else cfp_collect(&transport,report);
        report->error_number=device.error_number;
    }
    close(fd);
}
int main(int argc,char **argv)
{
    struct device_request request={.path="/dev/cros_fp"};
    if(argc==2 && strcmp(argv[1],"--help")==0) {
        puts("Usage: cros-fingerprint-diagnostic [--mode-status] [--device PATH]\n"
             "Read-only metadata queries; five-second process deadline.");
        return 0;
    }
    int device_arg=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--mode-status") && !request.mode_only) request.mode_only=1;
        else if(!strcmp(argv[i],"--device") && !device_arg && i+1<argc) {
            request.path=argv[++i]; device_arg=1;
        } else {
            fputs("Usage: cros-fingerprint-diagnostic [--mode-status] [--device PATH]\n",stderr); return 2;
        }
    }
    struct cfp_report report={0};
    int outcome=cfp_supervise(collect_device,&request,5000,&report);
    if(outcome) {
        fputs(outcome==124?"Diagnostic deadline exceeded\n":"Diagnostic worker failed\n",stderr);
        return outcome;
    }
    if(report.status!=CFP_OK) {
        if(report.error_number) fprintf(stderr,"Device access/ioctl failed: %s\n",strerror(report.error_number));
        else if(report.status==CFP_INVALID_ARGUMENT) fputs("Expected a character device\n",stderr);
        else fprintf(stderr,"Diagnostic protocol failure (status %u)\n",(unsigned)report.status);
        return 1;
    }
    if(request.mode_only) {
        int failed=printf("{\"schema_version\":1,\"mode\":%" PRIu32 ",\"configured_idle\":%s}\n",
            report.mode,report.mode==0?"true":"false")<0;
        return failed || fflush(stdout)==EOF;
    }
    return cfp_print_json(stdout,&report) || fflush(stdout)==EOF;
}

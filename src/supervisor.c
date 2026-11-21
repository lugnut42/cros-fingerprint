/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_diagnostic.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t milliseconds(void)
{
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)<0) return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
int cfp_supervise(void (*worker)(void *, struct cfp_report *), void *context,
    unsigned timeout_ms, struct cfp_report *report)
{
    if(!worker || !report || !timeout_ms || timeout_ms>INT_MAX) return 125;
    int64_t start=milliseconds();
    if(start<0) return 125;
    int fds[2];
    if(pipe(fds)<0) return 125;
    pid_t child=fork();
    if(child<0) { close(fds[0]); close(fds[1]); return 125; }
    if(child==0) {
        close(fds[0]);
        struct cfp_report local={0};
        worker(context,&local);
        /* One fixed-size record, comfortably within Linux PIPE_BUF. */
        _Static_assert(sizeof(local)<=4096,"atomic report write");
        ssize_t n;
        do { n=write(fds[1],&local,sizeof(local)); } while(n<0 && errno==EINTR);
        close(fds[1]);
        _exit(n==(ssize_t)sizeof(local)?0:1);
    }
    close(fds[1]);
    struct cfp_report local={0};
    size_t received=0;
    int outcome=125, reaped=0;
    for(;;) {
        int64_t now=milliseconds();
        if(now<0) break;
        int64_t left=start+(int64_t)timeout_ms-now;
        if(left<=0) { outcome=124; break; }
        if(received==sizeof(local)) {
            int status=0;
            pid_t done=waitpid(child,&status,WNOHANG);
            if(done==child) {
                reaped=1;
                if(WIFEXITED(status) && WEXITSTATUS(status)==0) {
                    *report=local; outcome=0;
                }
                break;
            }
            if(done<0 && errno!=EINTR) break;
            (void)poll(NULL,0,left<5?(int)left:5);
            continue;
        }
        struct pollfd p={.fd=fds[0],.events=POLLIN};
        int ready=poll(&p,1,(int)left);
        if(ready<0) { if(errno==EINTR) continue; break; }
        if(ready==0) { outcome=124; break; }
        ssize_t n=read(fds[0],(uint8_t *)&local+received,sizeof(local)-received);
        if(n<0) { if(errno==EINTR) continue; break; }
        if(n==0) break;
        received+=(size_t)n;
    }
    close(fds[0]);
    if(!reaped) {
        (void)kill(child,SIGKILL);
        /* Never block the CLI waiting for an uninterruptible device call. */
        (void)waitpid(child,NULL,WNOHANG);
    }
    return outcome;
}

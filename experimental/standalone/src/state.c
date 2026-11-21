/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_state.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int safe_directory(int fd,uid_t owner,int leaf)
{
    struct stat st;
    if(fstat(fd,&st)<0 || !S_ISDIR(st.st_mode)) return 0;
    if(leaf) return st.st_uid==owner && (st.st_mode&07777)==0700;
    return (st.st_uid==0 || st.st_uid==owner) && !(st.st_mode&0022);
}
enum cfp_store_status cfp_state_directory(int anchor,const char *path,
    uid_t owner,enum cfp_directory_action action,int *directory)
{
    if(!path || !directory || !*path || *path=='/' || strlen(path)>4096 ||
        (action!=CFP_DIRECTORY_OPEN && action!=CFP_DIRECTORY_CREATE))
        return CFP_STORE_UNSAFE;
    /* Validate all components before creating anything. */
    const char *start=path;
    for(const char *p=path;;p++) {
        if(*p=='/' || !*p) {
            size_t n=(size_t)(p-start);
            if(!n || n>255 || (n==1 && *start=='.') ||
                (n==2 && start[0]=='.' && start[1]=='.')) return CFP_STORE_UNSAFE;
            if(!*p) break;
            start=p+1;
        }
    }
    int parent=openat(anchor,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(parent<0) return CFP_STORE_UNSAFE;
    enum cfp_store_status result=CFP_STORE_UNSAFE;
    if(!safe_directory(parent,owner,0)) { close(parent); return result; }
    char *copy=strdup(path);
    if(!copy) { close(parent); return CFP_STORE_IO; }
    char *part=copy;
    for(;;) {
        char *slash=strchr(part,'/');
        int leaf=slash==NULL, created=0;
        if(slash) *slash=0;
        if(leaf && action==CFP_DIRECTORY_CREATE) {
            if(mkdirat(parent,part,0700)<0) {
                result=errno==EEXIST?CFP_STORE_EXISTS:CFP_STORE_IO; break;
            }
            created=1;
        }
        int next=openat(parent,part,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(next<0) {
            result=errno==ENOENT?CFP_STORE_MISSING:
                (errno==ELOOP || errno==ENOTDIR)?CFP_STORE_UNSAFE:CFP_STORE_IO;
            break;
        }
        if(!safe_directory(next,owner,leaf)) { close(next); break; }
        if(leaf) {
            if(created && (fsync(next)<0 || fsync(parent)<0)) {
                close(next); result=CFP_STORE_IO; break;
            }
            *directory=next; result=CFP_STORE_OK; break;
        }
        close(parent); parent=next; part=slash+1;
    }
    free(copy); close(parent); return result;
}
void cfp_seed_session_invalidate(struct cfp_seed_session *s)
{
    if(!s) return;
    s->phase=CFP_SESSION_BLOCKED;
    if(s->generation!=UINT64_MAX) s->generation++;
}
enum cfp_session_phase cfp_seed_session_begin(struct cfp_seed_session *s,
    enum cfp_store_status storage,int known,int set)
{
    if(!s) return CFP_SESSION_BLOCKED;
    cfp_seed_session_invalidate(s);
    if(s->generation!=UINT64_MAX && storage==CFP_STORE_OK && known==1 && set==0)
        s->phase=CFP_SESSION_RESTORE_PENDING;
    return s->phase;
}
void cfp_seed_session_complete(struct cfp_seed_session *s,uint64_t generation,int success)
{
    if(!s || s->phase!=CFP_SESSION_RESTORE_PENDING || generation!=s->generation)
        return;
    s->phase=success==1?CFP_SESSION_SEEDED:CFP_SESSION_BLOCKED;
}

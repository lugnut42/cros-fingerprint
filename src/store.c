/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_store.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static enum cfp_store_status empty(int fd)
{
    int scan=openat(fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(scan<0) return CFP_STORE_IO;
    DIR *d=fdopendir(scan);
    if(!d) { close(scan); return CFP_STORE_IO; }
    enum cfp_store_status result=CFP_STORE_OK;
    struct dirent *e; errno=0;
    while((e=readdir(d))) {
        if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")) {
            result=CFP_STORE_EXISTS; break;
        }
    }
    if(result==CFP_STORE_OK && errno) result=CFP_STORE_IO;
    closedir(d); return result;
}
void cfp_store_close(struct cfp_store *s)
{
    if(!s) return;
    cfp_seed_session_invalidate(&s->session);
    cfp_seed_erase(&s->provision,sizeof(s->provision));
    if(s->accounts_fd>=0) close(s->accounts_fd);
    if(s->lock_fd>=0) close(s->lock_fd);
    s->accounts_fd=s->lock_fd=-1;
}
enum cfp_store_status cfp_store_open(int root,uid_t owner,int create,
    const struct cfp_seed_ops *seed_ops,const struct cfp_registry_ops *registry_ops,
    struct cfp_store *out)
{
    if(!out || (create!=0 && create!=1)) return CFP_STORE_UNSAFE;
    struct cfp_store s={.lock_fd=-1,.accounts_fd=-1,.owner=owner};
    s.lock_fd=openat(root,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(s.lock_fd<0) return CFP_STORE_UNSAFE;
    enum cfp_store_status result=CFP_STORE_UNSAFE;
    struct stat st; int provision_fd=-1;
    if(fstat(s.lock_fd,&st)<0 || st.st_uid!=owner || (st.st_mode&07777)!=0700) goto done;
    if(flock(s.lock_fd,LOCK_EX|LOCK_NB)<0) {
        result=errno==EWOULDBLOCK?CFP_STORE_BUSY:CFP_STORE_IO; goto done;
    }
    if(create) { result=empty(s.lock_fd); if(result!=CFP_STORE_OK) goto done; }
    enum cfp_directory_action action=create?CFP_DIRECTORY_CREATE:CFP_DIRECTORY_OPEN;
    result=cfp_state_directory(s.lock_fd,"provision",owner,action,&provision_fd);
    if(result!=CFP_STORE_OK) goto done;
    result=create?cfp_provision_create(provision_fd,owner,seed_ops,&s.provision):
        cfp_provision_load(provision_fd,owner,seed_ops,&s.provision);
    if(result!=CFP_STORE_OK) goto done;
    result=cfp_state_directory(s.lock_fd,"accounts",owner,action,&s.accounts_fd);
    if(result!=CFP_STORE_OK) goto done;
    struct cfp_registry_request q={.action=create?CFP_REGISTRY_INITIALIZE:CFP_REGISTRY_CHECK};
    memcpy(q.reader_generation,s.provision.generation,32);
    result=cfp_registry_apply(s.accounts_fd,owner,registry_ops,&q,NULL);
    cfp_seed_erase(&q,sizeof(q));
    if(result==CFP_STORE_OK) {
        *out=s;
        cfp_seed_erase(&s.provision,sizeof(s.provision));
        close(provision_fd); return CFP_STORE_OK;
    }
done:
    if(provision_fd>=0) close(provision_fd);
    cfp_store_close(&s); return result;
}
enum cfp_store_status cfp_store_account(struct cfp_store *s,
    const struct cfp_registry_ops *ops,enum cfp_registry_action action,
    uint64_t uid,const char *name,const struct cfp_binding *expected,struct cfp_binding *out)
{
    if(!s || s->lock_fd<0 || s->accounts_fd<0 ||
        (action!=CFP_REGISTRY_REGISTER && action!=CFP_REGISTRY_LOOKUP && action!=CFP_REGISTRY_RETIRE))
        return CFP_STORE_UNSAFE;
    if(action==CFP_REGISTRY_RETIRE) cfp_seed_session_invalidate(&s->session);
    struct cfp_registry_request q={.action=action,.uid=uid,.name=name,.expected=expected};
    memcpy(q.reader_generation,s->provision.generation,32);
    enum cfp_store_status result=cfp_registry_apply(s->accounts_fd,s->owner,ops,&q,out);
    cfp_seed_erase(&q,sizeof(q)); return result;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_registry.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#define HEADER_SIZE 44u
#define ENTRY_SIZE 144u
#define MAX_RECORD (HEADER_SIZE+ENTRY_SIZE*CFP_REGISTRY_MAX_ENTRIES+32u)
static const unsigned char magic[8]={'C','F','P','R','E','G','0','1'};
struct entry {
    uint64_t uid;
    char name[64];
    unsigned char account[32], context[32];
    unsigned active;
};
struct registry {
    unsigned char generation[32];
    unsigned count;
    struct entry entries[CFP_REGISTRY_MAX_ENTRIES];
};
static ssize_t system_random(void *p,size_t n) { return getrandom(p,n,0); }
static const struct cfp_registry_ops defaults={system_random,write,fsync,renameat};
static int valid_id(const unsigned char *p)
{
    unsigned nonzero=0, nonff=0;
    for(unsigned i=0;i<32;i++) { nonzero|=p[i]; nonff|=p[i]^255u; }
    return nonzero && nonff;
}
static int valid_name(const char *p)
{
    if(!p) return 0;
    size_t n=strnlen(p,64);
    if(!n || n==64) return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)p[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    return 1;
}
static void put_number(unsigned char *p,uint64_t value,unsigned n)
{ for(unsigned i=0;i<n;i++) { p[i]=(unsigned char)value; value>>=8; } }
static uint64_t get_number(const unsigned char *p,unsigned n)
{
    uint64_t value=0;
    for(unsigned i=0;i<n;i++) value|=(uint64_t)p[i]<<(8*i);
    return value;
}
static int digest(const unsigned char *p,size_t n,unsigned char out[32])
{
    unsigned size=0;
    return EVP_Digest(p,n,out,&size,EVP_sha256(),NULL)==1 && size==32;
}
static int unique_ids(const struct registry *r,const struct entry *e,unsigned prior)
{
    if(!valid_id(e->account) || !valid_id(e->context) ||
        !memcmp(e->account,e->context,32)) return 0;
    for(unsigned i=0;i<prior;i++) {
        const struct entry *p=&r->entries[i];
        if(!memcmp(e->account,p->account,32) || !memcmp(e->context,p->context,32) ||
            !memcmp(e->account,p->context,32) || !memcmp(e->context,p->account,32)) return 0;
    }
    return 1;
}
static int decode(const unsigned char *data,size_t n,struct registry *r)
{
    unsigned char sum[32];
    if(n<HEADER_SIZE+32 || memcmp(data,magic,8) || !valid_id(data+8)) return 0;
    uint64_t count=get_number(data+40,4);
    if(count>CFP_REGISTRY_MAX_ENTRIES || n!=HEADER_SIZE+count*ENTRY_SIZE+32) return 0;
    if(!digest(data,n-32,sum)) return 0;
    int checksum_ok=!memcmp(sum,data+n-32,32);
    cfp_seed_erase(sum,sizeof(sum));
    if(!checksum_ok) return 0;
    memcpy(r->generation,data+8,32); r->count=(unsigned)count;
    for(unsigned i=0;i<r->count;i++) {
        const unsigned char *p=data+HEADER_SIZE+i*ENTRY_SIZE;
        struct entry *e=&r->entries[i];
        e->uid=get_number(p,8); memcpy(e->name,p+8,64);
        if(!valid_name(e->name)) return 0;
        for(size_t j=strlen(e->name)+1;j<64;j++) if(e->name[j]) return 0;
        memcpy(e->account,p+72,32); memcpy(e->context,p+104,32);
        e->active=p[136];
        if(e->active>1) return 0;
        for(unsigned j=137;j<ENTRY_SIZE;j++) if(p[j]) return 0;
        if(!unique_ids(r,e,i)) return 0;
        for(unsigned j=0;j<i;j++) {
            const struct entry *old=&r->entries[j];
            if(e->active && old->active &&
                (e->uid==old->uid || !strcmp(e->name,old->name))) return 0;
        }
    }
    return 1;
}
static enum cfp_store_status lock_directory(int directory,uid_t owner,int *out)
{
    int fd=openat(directory,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return CFP_STORE_UNSAFE;
    struct stat st;
    if(fstat(fd,&st)<0 || st.st_uid!=owner || (st.st_mode&07777)!=0700) {
        close(fd); return CFP_STORE_UNSAFE;
    }
    if(flock(fd,LOCK_EX|LOCK_NB)<0) {
        int saved=errno; close(fd);
        return saved==EWOULDBLOCK?CFP_STORE_BUSY:CFP_STORE_IO;
    }
    *out=fd; return CFP_STORE_OK;
}
static enum cfp_store_status empty_directory(int fd)
{
    int scan=openat(fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(scan<0) return CFP_STORE_IO;
    DIR *dir=fdopendir(scan);
    if(!dir) { close(scan); return CFP_STORE_IO; }
    enum cfp_store_status status=CFP_STORE_OK;
    struct dirent *e;
    errno=0;
    while((e=readdir(dir))) {
        if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")) {
            status=CFP_STORE_EXISTS; break;
        }
    }
    if(status==CFP_STORE_OK && errno) status=CFP_STORE_IO;
    closedir(dir); return status;
}
static enum cfp_store_status load(int dir,uid_t owner,
    const struct cfp_registry_ops *ops,struct registry *r)
{
    struct stat st;
    if(fstatat(dir,".registry.pending",&st,AT_SYMLINK_NOFOLLOW)==0) return CFP_STORE_RECOVERY;
    if(errno!=ENOENT) return CFP_STORE_IO;
    int fd=openat(dir,"registry",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) return errno==ENOENT?CFP_STORE_MISSING:
        errno==ELOOP?CFP_STORE_UNSAFE:CFP_STORE_IO;
    unsigned char data[MAX_RECORD+1]={0};
    enum cfp_store_status status=CFP_STORE_UNSAFE;
    if(fstat(fd,&st)<0 || !S_ISREG(st.st_mode) || st.st_uid!=owner ||
        (st.st_mode&07777)!=0600 || st.st_nlink!=1) goto done;
    status=CFP_STORE_CORRUPT;
    if(st.st_size<(off_t)(HEADER_SIZE+32) || st.st_size>(off_t)MAX_RECORD) goto done;
    size_t offset=0;
    while(offset<sizeof(data)) {
        ssize_t n=read(fd,data+offset,sizeof(data)-offset);
        if(n<0) { status=CFP_STORE_IO; goto done; }
        if(!n) break;
        offset+=(size_t)n;
    }
    if(offset!=(size_t)st.st_size || !decode(data,offset,r)) goto done;
    status=CFP_STORE_IO;
    if(ops->sync_fd(fd)<0 || ops->sync_fd(dir)<0) goto done;
    status=CFP_STORE_OK;
done:
    cfp_seed_erase(data,sizeof(data)); close(fd); return status;
}
static enum cfp_store_status save(int dir,const struct cfp_registry_ops *ops,
    const struct registry *r)
{
    unsigned char data[MAX_RECORD]={0};
    size_t n=HEADER_SIZE+r->count*ENTRY_SIZE+32;
    memcpy(data,magic,8); memcpy(data+8,r->generation,32);
    put_number(data+40,r->count,4);
    for(unsigned i=0;i<r->count;i++) {
        const struct entry *e=&r->entries[i];
        unsigned char *p=data+HEADER_SIZE+i*ENTRY_SIZE;
        put_number(p,e->uid,8); memcpy(p+8,e->name,64);
        memcpy(p+72,e->account,32); memcpy(p+104,e->context,32);
        p[136]=(unsigned char)e->active;
    }
    enum cfp_store_status status=CFP_STORE_IO;
    int fd=-1;
    if(!digest(data,n-32,data+n-32)) goto done;
    fd=openat(dir,".registry.pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0 || fchmod(fd,0600)<0) goto done;
    size_t offset=0;
    while(offset<n) {
        ssize_t written=ops->write_bytes(fd,data+offset,n-offset);
        if(written<=0 || (size_t)written>n-offset) goto done;
        offset+=(size_t)written;
    }
    if(ops->sync_fd(fd)<0) goto done;
    if(close(fd)<0) { fd=-1; goto done; }
    fd=-1;
    if(ops->replace(dir,".registry.pending",dir,"registry")<0 ||
        ops->sync_fd(dir)<0) goto done;
    status=CFP_STORE_OK;
done:
    if(fd>=0) close(fd);
    cfp_seed_erase(data,sizeof(data)); return status;
}
static struct cfp_binding binding(const struct registry *r,const struct entry *e)
{
    struct cfp_binding b={.schema=1,.template_format=4};
    memcpy(b.reader_generation,r->generation,32);
    memcpy(b.account_id,e->account,32); memcpy(b.context_id,e->context,32);
    return b;
}
static int same_binding(const struct cfp_binding *a,const struct cfp_binding *b)
{
    return a->schema==b->schema && a->template_format==b->template_format &&
        !memcmp(a->reader_generation,b->reader_generation,32) &&
        !memcmp(a->account_id,b->account_id,32) && !memcmp(a->context_id,b->context_id,32);
}
enum cfp_store_status cfp_registry_apply(int directory,uid_t owner,
    const struct cfp_registry_ops *ops,const struct cfp_registry_request *q,
    struct cfp_binding *out)
{
    if(!ops) ops=&defaults;
    if(!q || !ops->random_bytes || !ops->write_bytes || !ops->sync_fd || !ops->replace ||
        q->action<CFP_REGISTRY_INITIALIZE || q->action>CFP_REGISTRY_CHECK ||
        !valid_id(q->reader_generation)) return CFP_STORE_UNSAFE;
    if(q->action!=CFP_REGISTRY_INITIALIZE && q->action!=CFP_REGISTRY_CHECK && !valid_name(q->name)) return CFP_STORE_UNSAFE;
    if((q->action==CFP_REGISTRY_REGISTER || q->action==CFP_REGISTRY_LOOKUP) && !out)
        return CFP_STORE_UNSAFE;
    if(q->action==CFP_REGISTRY_RETIRE && !q->expected) return CFP_STORE_UNSAFE;
    int dir=-1;
    enum cfp_store_status status=lock_directory(directory,owner,&dir);
    if(status!=CFP_STORE_OK) return status;
    struct registry r={0};
    struct cfp_binding selected={0};
    unsigned char ids[64]={0};
    if(q->action==CFP_REGISTRY_INITIALIZE) {
        status=empty_directory(dir);
        if(status!=CFP_STORE_OK) goto done;
        memcpy(r.generation,q->reader_generation,32);
        status=save(dir,ops,&r); goto done;
    }
    status=load(dir,owner,ops,&r);
    if(status!=CFP_STORE_OK) goto done;
    status=CFP_STORE_RECOVERY;
    if(memcmp(r.generation,q->reader_generation,32)) goto done;
    if(q->action==CFP_REGISTRY_CHECK) { status=CFP_STORE_OK; goto done; }
    unsigned index=r.count;
    for(unsigned i=0;i<r.count;i++) {
        const struct entry *e=&r.entries[i];
        if(!e->active) continue;
        if(q->action==CFP_REGISTRY_REGISTER && (e->uid==q->uid || !strcmp(e->name,q->name))) {
            status=CFP_STORE_EXISTS; goto done;
        }
        if(e->uid==q->uid && !strcmp(e->name,q->name)) index=i;
    }
    if(q->action==CFP_REGISTRY_REGISTER) {
        if(r.count==CFP_REGISTRY_MAX_ENTRIES) { status=CFP_STORE_RECOVERY; goto done; }
        status=CFP_STORE_RANDOM;
        if(ops->random_bytes(ids,sizeof(ids))!=(ssize_t)sizeof(ids)) goto done;
        struct entry *e=&r.entries[r.count];
        e->uid=q->uid; memcpy(e->name,q->name,strlen(q->name)+1);
        memcpy(e->account,ids,32); memcpy(e->context,ids+32,32); e->active=1;
        if(!unique_ids(&r,e,r.count)) goto done;
        selected=binding(&r,e); r.count++;
        status=save(dir,ops,&r);
    } else {
        status=CFP_STORE_MISSING;
        if(index==r.count) goto done;
        selected=binding(&r,&r.entries[index]);
        if(q->action==CFP_REGISTRY_LOOKUP) status=CFP_STORE_OK;
        else {
            status=CFP_STORE_RECOVERY;
            if(!same_binding(q->expected,&selected)) goto done;
            r.entries[index].active=0;
            status=save(dir,ops,&r);
        }
    }
    if(status==CFP_STORE_OK && (q->action==CFP_REGISTRY_REGISTER || q->action==CFP_REGISTRY_LOOKUP))
        *out=selected;
done:
    cfp_seed_erase(&r,sizeof(r)); cfp_seed_erase(&selected,sizeof(selected));
    cfp_seed_erase(ids,sizeof(ids)); close(dir); return status;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _GNU_SOURCE
#include "cros_fp_seed.h"
#include "cros_fp_provision.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

struct record_type { const char *name, *pending; unsigned char magic[8]; size_t payload; };
static const struct record_type seed_type={"seed",".seed.pending",{'C','F','P','S','E','E','D','1'},32};
static const struct record_type context_type={"context",".context.pending",{'C','F','P','C','T','X','0','1'},32};
static const struct record_type provision_type={"provision",".provision.pending",{'C','F','P','P','R','V','0','1'},64};
static int valid_payload(const unsigned char *p,size_t n)
{
    for(size_t j=0;j<n;j+=32) {
        unsigned nonzero=0,nonff=0;
        for(size_t i=j;i<j+32;i++) { nonzero|=p[i]; nonff|=p[i]^255u; }
        if(!nonzero || !nonff) return 0;
    }
    return n!=64 || memcmp(p,p+32,32)!=0;
}
static ssize_t system_random(void *p,size_t n) { return getrandom(p,n,0); }
static const struct cfp_seed_ops defaults={system_random,write,fsync,linkat};
void cfp_seed_erase(void *data,size_t size)
{
    volatile unsigned char *p=data;
    while(size--) *p++=0;
}
static int digest(const unsigned char *record,size_t n,unsigned char sum[32])
{
    unsigned int size=0;
    return EVP_Digest(record,n,sum,&size,EVP_sha256(),NULL)==1 && size==32;
}
static int valid_ops(const struct cfp_seed_ops *ops)
{
    return ops->random_bytes && ops->write_bytes && ops->sync_fd && ops->publish;
}
static enum cfp_store_status locked_directory(int directory,uid_t owner,int *out)
{
    /* Reopen, not dup: flock must have an independent open file description. */
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
    enum cfp_store_status result=CFP_STORE_OK;
    errno=0;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) {
            result=CFP_STORE_EXISTS; break;
        }
    }
    if(result==CFP_STORE_OK && errno) result=CFP_STORE_IO;
    closedir(dir); return result;
}
static enum cfp_store_status record_create(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,unsigned char *seed,const struct record_type *type)
{
    if(!ops) ops=&defaults;
    if(!seed || !valid_ops(ops)) return CFP_STORE_UNSAFE;
    int dir=-1, file=-1;
    enum cfp_store_status result=locked_directory(directory,owner,&dir);
    if(result!=CFP_STORE_OK) return result;
    unsigned char record[104]={0};
    size_t length=8+type->payload+32;
    result=empty_directory(dir);
    if(result!=CFP_STORE_OK) goto done;
    memcpy(record,type->magic,8);
    if(ops->random_bytes(record+8,type->payload)!=(ssize_t)type->payload ||
        !valid_payload(record+8,type->payload)) { result=CFP_STORE_RANDOM; goto done; }
    result=CFP_STORE_IO;
    if(!digest(record,8+type->payload,record+8+type->payload)) goto done;
    file=openat(dir,type->pending,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(file<0) goto done;
    /* Permissions must not depend on the caller's umask. */
    if(fchmod(file,0600)<0) goto done;
    size_t offset=0;
    while(offset<length) {
        ssize_t n=ops->write_bytes(file,record+offset,length-offset);
        if(n<=0 || (size_t)n>length-offset) goto done;
        offset+=(size_t)n;
    }
    if(ops->sync_fd(file)<0) goto done;
    if(close(file)<0) { file=-1; goto done; }
    file=-1;
    /* linkat publishes without replacing any existing destination. */
    if(ops->publish(dir,type->pending,dir,type->name,0)<0) goto done;
    if(unlinkat(dir,type->pending,0)<0) goto done;
    if(ops->sync_fd(dir)<0) goto done;
    memcpy(seed,record+8,type->payload);
    result=CFP_STORE_OK;
done:
    if(file>=0) close(file);
    cfp_seed_erase(record,sizeof(record));
    close(dir);
    return result;
}
static enum cfp_store_status record_load(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,unsigned char *seed,const struct record_type *type)
{
    if(!ops) ops=&defaults;
    if(!seed || !valid_ops(ops)) return CFP_STORE_UNSAFE;
    int dir=-1, file=-1;
    enum cfp_store_status result=locked_directory(directory,owner,&dir);
    if(result!=CFP_STORE_OK) return result;
    unsigned char record[105]={0}, sum[32]={0};
    size_t length=8+type->payload+32;
    struct stat st;
    if(fstatat(dir,type->pending,&st,AT_SYMLINK_NOFOLLOW)==0) {
        result=CFP_STORE_RECOVERY; goto done;
    }
    if(errno!=ENOENT) { result=CFP_STORE_IO; goto done; }
    file=openat(dir,type->name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(file<0) {
        result=errno==ENOENT?CFP_STORE_MISSING:errno==ELOOP?CFP_STORE_UNSAFE:CFP_STORE_IO;
        goto done;
    }
    result=CFP_STORE_UNSAFE;
    if(fstat(file,&st)<0 || !S_ISREG(st.st_mode) || st.st_uid!=owner ||
        (st.st_mode&07777)!=0600 || st.st_nlink!=1) goto done;
    result=CFP_STORE_CORRUPT;
    if(st.st_size!=(off_t)length) goto done;
    size_t offset=0;
    while(offset<sizeof(record)) {
        ssize_t n=read(file,record+offset,sizeof(record)-offset);
        if(n<0) { result=CFP_STORE_IO; goto done; }
        if(n==0) break;
        offset+=(size_t)n;
    }
    if(offset!=length || memcmp(record,type->magic,8)) goto done;
    if(!digest(record,8+type->payload,sum)) { result=CFP_STORE_IO; goto done; }
    if(memcmp(sum,record+8+type->payload,32)) goto done;
    if(type->payload==64 && !valid_payload(record+8,64)) goto done;
    /* Also resolve durability before releasing a previously published record
     * whose creator may have failed its final directory sync. */
    result=CFP_STORE_IO;
    if(ops->sync_fd(file)<0 || ops->sync_fd(dir)<0) goto done;
    memcpy(seed,record+8,type->payload);
    result=CFP_STORE_OK;
done:
    if(file>=0) close(file);
    cfp_seed_erase(record,sizeof(record)); cfp_seed_erase(sum,sizeof(sum));
    close(dir);
    return result;
}

enum cfp_store_status cfp_seed_create(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,unsigned char seed[32])
{ return record_create(directory,owner,ops,seed,&seed_type); }
enum cfp_store_status cfp_seed_load(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,unsigned char seed[32])
{ return record_load(directory,owner,ops,seed,&seed_type); }
enum cfp_store_status cfp_context_id_create(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,unsigned char context[32])
{ return record_create(directory,owner,ops,context,&context_type); }
enum cfp_store_status cfp_context_id_load(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,unsigned char context[32])
{ return record_load(directory,owner,ops,context,&context_type); }

static enum cfp_store_status provision(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,struct cfp_provision *out,int create)
{
    if(!out) return CFP_STORE_UNSAFE;
    unsigned char data[64]={0};
    enum cfp_store_status status=create?
        record_create(directory,owner,ops,data,&provision_type):
        record_load(directory,owner,ops,data,&provision_type);
    if(status==CFP_STORE_OK) {
        memcpy(out->generation,data,32); memcpy(out->seed,data+32,32);
    }
    cfp_seed_erase(data,sizeof(data)); return status;
}
enum cfp_store_status cfp_provision_create(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,struct cfp_provision *out)
{ return provision(directory,owner,ops,out,1); }
enum cfp_store_status cfp_provision_load(int directory,uid_t owner,
    const struct cfp_seed_ops *ops,struct cfp_provision *out)
{ return provision(directory,owner,ops,out,0); }

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "client_install_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__FreeBSD__)
#include <fcntl.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <unistd.h>
extern int sceKernelDlsym(int, const char *, void *);
/* Common libkernel exposes _fstatfs, whereas statfs is only present in the
   sys variant. Do not link the SDK's raw statfs syscall into a native title:
   only payload CRT establishes permission for those syscall instructions. */
static int describe_mount(const char *path, struct statfs *fs){
    int (*query)(int,struct statfs *)=NULL;
    if(sceKernelDlsym(0x2001,"_fstatfs",&query)||!query){
        query=NULL;
        if(sceKernelDlsym(0x1,"_fstatfs",&query)||!query)return -1;
    }
    int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;
    int rc=query(fd,fs);close(fd);return rc;
}
#endif
int muse_path_valid(const char *s){
    if(!s||s[0]!='/'||strnlen(s,1024)>=1024)return 0;
    for(const unsigned char *p=(const unsigned char*)s;*p;p++)if(*p<32)return 0;
    for(const char *p=s;*p;){while(*p=='/')p++;const char *a=p;while(*p&&*p!='/')p++;if((p-a==1&&a[0]=='.')||(p-a==2&&a[0]=='.'&&a[1]=='.'))return 0;}
    return 1;
}
int muse_path_describe(const char *local,const char *override,struct muse_path_locator *out){
    memset(out,0,sizeof(*out));if(!muse_path_valid(local)||(override&&*override&&!muse_path_valid(override)))return -1;
    strcpy(out->direct,override&&*override?override:local);
    /* An explicit helper path is authoritative and still must pass its probe. */
    if(override&&*override)return 0;
#if defined(__FreeBSD__)
    struct statfs fs;memset(&fs,0,sizeof(fs));
    if(!describe_mount(local,&fs)&&memchr(fs.f_mntonname,0,sizeof(fs.f_mntonname))){
        size_t n=strlen(fs.f_mntonname);while(n>1&&fs.f_mntonname[n-1]=='/')n--;
        if(n&&strlen(local)>=n&&sizeof(fs.f_fsid)==sizeof(out->fsid)&&!memcmp(local,fs.f_mntonname,n)&&
           (n==1||!local[n]||local[n]=='/')){
            const char *relative=n==1?local:local+n;if(!*relative)relative="/";
            if(muse_path_valid(relative)){memcpy(out->fsid,&fs.f_fsid,sizeof(out->fsid));strcpy(out->relative,relative);out->has_mount=1;}
        }
    }
#endif
    return 0;
}
static int accept_path(const char *path,muse_path_probe probe,void *context,char *out,size_t capacity){
    if(!muse_path_valid(path)||strlen(path)>=capacity||probe(path,context))return -1;
    strcpy(out,path);return 0;
}
int muse_path_resolve_records(const struct muse_path_locator *locator,
    const struct muse_mount_record *records,size_t count,muse_path_probe probe,
    void *context,char *out,size_t capacity){
    if(!locator||!probe||!out||!capacity||!memchr(locator->direct,0,sizeof(locator->direct))||
       !memchr(locator->relative,0,sizeof(locator->relative))||locator->has_mount>1||count>512)return -1;
    if(!accept_path(locator->direct,probe,context,out,capacity))return 0;
    if(!locator->has_mount||!muse_path_valid(locator->relative))return -1;
    for(size_t i=0;i<count;i++){
        if(memcmp(locator->fsid,records[i].fsid,sizeof(locator->fsid))||
           !memchr(records[i].path,0,sizeof(records[i].path))||!muse_path_valid(records[i].path))continue;
        char candidate[2048];size_t n=strlen(records[i].path);while(n>1&&records[i].path[n-1]=='/')n--;
        int size=snprintf(candidate,sizeof(candidate),"%.*s%s",n==1?0:(int)n,records[i].path,locator->relative);
        if(size>0&&size<1024&&!accept_path(candidate,probe,context,out,capacity))return 0;
    }return -1;
}
int muse_path_resolve(const struct muse_path_locator *locator,muse_path_probe probe,
                      void *context,char *out,size_t capacity){
    if(!muse_path_resolve_records(locator,NULL,0,probe,context,out,capacity))return 0;
#if defined(__FreeBSD__) && defined(MUSE_INSTALL_PAYLOAD_HELPER)
    if(!locator->has_mount)return -1;
    int count=getfsstat(NULL,0,MNT_NOWAIT);if(count<=0||count>=512)return -1;
    size_t cap=(size_t)count+1;struct statfs *fs=calloc(cap,sizeof(*fs));
    struct muse_mount_record *records=calloc(cap,sizeof(*records));if(!fs||!records){free(fs);free(records);return -1;}
    int got=getfsstat(fs,(long)(cap*sizeof(*fs)),MNT_NOWAIT),rc=-1;
    if(got>0&&(size_t)got<=cap){
        for(int i=0;i<got;i++){memcpy(records[i].fsid,&fs[i].f_fsid,sizeof(records[i].fsid));if(memchr(fs[i].f_mntonname,0,sizeof(fs[i].f_mntonname)))snprintf(records[i].path,sizeof(records[i].path),"%s",fs[i].f_mntonname);}
        rc=muse_path_resolve_records(locator,records,(size_t)got,probe,context,out,capacity);
    }free(records);free(fs);return rc;
#else
    return -1;
#endif
}

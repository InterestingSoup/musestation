/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "client_install_protocol.h"
#include "client_install_fds.h"
#include <stddef.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
static int unix_socket(const char *path,int listening){
    struct sockaddr_un address={0};if(!path||strlen(path)>=sizeof(address.sun_path))return -1;
    address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
    socklen_t size=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+strlen(path)+1);
#if defined(__FreeBSD__)
    address.sun_len=(unsigned char)size;
#endif
    int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return -1;
    if(listening){if(bind(fd,(struct sockaddr*)&address,size)||listen(fd,2)){close(fd);return -1;}}
    else{if(fcntl(fd,F_SETFL,O_NONBLOCK)){close(fd);return -1;}int rc=connect(fd,(struct sockaddr*)&address,size);
        if(rc&&errno==EINPROGRESS){struct pollfd p={fd,POLLOUT,0};int e=0;socklen_t n=sizeof(e);rc=poll(&p,1,5000)>0&&!getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)&&!e?0:-1;}
        if(rc){close(fd);return -1;}}
    return fd;
}
int muse_unix_listen(const char *path){return unix_socket(path,1);}
int muse_unix_connect(const char *path){return unix_socket(path,0);}
int muse_fds_send(int fd,const int fds[3],const unsigned char token[32]){
    int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK))return -1;
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(3*sizeof(int))]; } control={0};
    struct iovec io={(void*)token,32};struct msghdr message={0};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control.bytes;message.msg_controllen=sizeof(control.bytes);
    struct cmsghdr *h=CMSG_FIRSTHDR(&message);h->cmsg_level=SOL_SOCKET;h->cmsg_type=SCM_RIGHTS;h->cmsg_len=CMSG_LEN(3*sizeof(int));memcpy(CMSG_DATA(h),fds,3*sizeof(int));
    struct pollfd poller={fd,POLLOUT,0};if(poll(&poller,1,5000)<=0)return -1;
    ssize_t n=sendmsg(fd,&message,MSG_NOSIGNAL);if(n<=0||n>32)return -1;
    return n<32?muse_ipc_io(fd,(void*)(token+n),32-(size_t)n,1,5000):0;
}
int muse_fds_receive(int fd,int fds[3],const unsigned char token[32]){
    fds[0]=fds[1]=fds[2]=-1;unsigned char data[32];
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(3*sizeof(int))]; } control={0};
    struct iovec io={data,sizeof(data)};struct msghdr message={0};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control.bytes;message.msg_controllen=sizeof(control.bytes);
    struct pollfd poller={fd,POLLIN,0};if(poll(&poller,1,10000)<=0)return -1;
    ssize_t n=recvmsg(fd,&message,0);int count=0,bad=n<=0||n>32||(message.msg_flags&(MSG_CTRUNC|MSG_TRUNC));
    for(struct cmsghdr *h=CMSG_FIRSTHDR(&message);h;h=CMSG_NXTHDR(&message,h)){
        size_t offset=(size_t)((unsigned char*)h-control.bytes);
        if(offset>message.msg_controllen||message.msg_controllen-offset<sizeof(*h)||h->cmsg_len<CMSG_LEN(0)||h->cmsg_len>message.msg_controllen-offset){bad=1;break;}
        if(h->cmsg_level!=SOL_SOCKET||h->cmsg_type!=SCM_RIGHTS){bad=1;continue;}
        size_t bytes=h->cmsg_len-CMSG_LEN(0);if(bytes%sizeof(int))bad=1;
        for(size_t i=0;i<bytes/sizeof(int);i++){int value;memcpy(&value,CMSG_DATA(h)+i*sizeof(int),sizeof(value));if(count<3)fds[count++]=value;else{close(value);bad=1;}}
    }
    if(!bad&&n<32)bad=muse_ipc_io(fd,data+n,32-(size_t)n,0,5000);
    if(count!=3||(!bad&&memcmp(data,token,32)))bad=1;
    struct stat stats[3];
    for(int i=0;!bad&&i<3;i++){int mode=fcntl(fds[i],F_GETFL);if(mode<0||(mode&O_ACCMODE)!=O_RDWR||fstat(fds[i],&stats[i])||!S_ISREG(stats[i].st_mode)||fcntl(fds[i],F_SETFD,FD_CLOEXEC))bad=1;
        for(int j=0;!bad&&j<i;j++)if(stats[j].st_dev==stats[i].st_dev&&stats[j].st_ino==stats[i].st_ino)bad=1;}
    if(bad){for(int i=0;i<3;i++)if(fds[i]>=0){close(fds[i]);fds[i]=-1;}return -1;}return 0;
}

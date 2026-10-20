/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_INSTALL_PROTOCOL_H
#define MUSE_INSTALL_PROTOCOL_H
#include "client_install.h"
#include "client_install_paths.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#define MUSE_INSTALL_PROTOCOL 0x4d490005u
#define MUSE_INSTALL_BOOT_MARKER "MUSE_INSTALL_BOOTSTRAP_v1_4be21"
enum muse_install_op { MUSE_INSTALL_READY=0, MUSE_INSTALL_START=1, MUSE_INSTALL_STATUS=2, MUSE_INSTALL_LAUNCH=3, MUSE_INSTALL_CLOSE=4, MUSE_INSTALL_PREPARE=5 };
struct muse_install_boot { char marker[32]; uint32_t port; unsigned char token[32]; };
struct muse_install_request {
    uint32_t version,op;
    char path[1024],server_url[256],log_dir[512];
    /* Console-scoped relay credential; authenticated loopback IPC only, never logs. */
    char relay_token[65];
    char probe_name[64], socket_name[80];
    struct muse_path_locator package_location,journal_location;
    struct muse_build_status manifest;
};
struct muse_install_response {
    uint32_t version,op;
    unsigned char token[32];
    int32_t result;
    struct muse_install_status status;
};
static inline int64_t muse_ipc_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static inline int muse_ipc_io(int fd,void *data,size_t length,int sending,int timeout){
    int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return -1;
    unsigned char *p=data;int64_t deadline=muse_ipc_now()+timeout;
    while(length){int64_t left=deadline-muse_ipc_now();if(left<=0)return -1;struct pollfd item={fd,sending?POLLOUT:POLLIN,0};
        int rc=poll(&item,1,(int)left);if(rc<0&&errno==EINTR)continue;if(rc<=0)return -1;
        ssize_t n=sending?send(fd,p,length,MSG_NOSIGNAL):recv(fd,p,length,0);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(n<=0)return -1;p+=n;length-=(size_t)n;
    }return 0;
}
#endif

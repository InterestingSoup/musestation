// SPDX-License-Identifier: GPL-3.0-or-later
#include <errno.h>
#include <stddef.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
// Optional backtrace helper cannot spawn processes in a native title.
int execlp(const char *file,const char *arg,...) {
    (void)file; (void)arg; errno=ENOSYS; return -1;
}
// Read hints are advisory; the model's actual file reads still execute.
int posix_madvise(void *address,size_t size,int advice) {
    (void)address; (void)size; (void)advice; return 0;
}

// The pinned platform's getaddrinfo deliberately returns EAI_FAIL. Muse's
// HTTP client accepts only numeric origins, so implement those real addresses
// instead. Based on PS5LM 6323ecb ps5/app/compat_app.c; never performs DNS.
int getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **result) {
    if (!result) return EAI_FAIL;
    *result = NULL;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return EAI_FAMILY;
    if (hints && hints->ai_socktype != 0 && hints->ai_socktype != SOCK_STREAM && hints->ai_socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    unsigned port = 0;
    if (service && *service) {
        for (const unsigned char *p=(const unsigned char *)service;*p;++p) {
            if (*p<'0' || *p>'9') return EAI_SERVICE;
            port=port*10+(*p-'0'); if (port>65535) return EAI_SERVICE;
        }
    }
    unsigned char address[16] = {0};
    int passive = hints && (hints->ai_flags & AI_PASSIVE);
    if (!node || !strcmp(node,"localhost")) {
        if (family == AF_UNSPEC) family = AF_INET;
        if (node || !passive) {
            if (family == AF_INET) { address[0]=127; address[3]=1; }
            else address[15]=1;
        }
    } else if (family != AF_INET6 && inet_pton(AF_INET,node,address)==1) family=AF_INET;
    else if (family != AF_INET && inet_pton(AF_INET6,node,address)==1) family=AF_INET6;
    else return EAI_NONAME;
    size_t size = family==AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    struct addrinfo *ai=calloc(1,sizeof(*ai)+size);
    if (!ai) return EAI_MEMORY;
    ai->ai_family=family;
    ai->ai_socktype=hints ? hints->ai_socktype : 0;
    ai->ai_protocol=hints ? hints->ai_protocol : 0;
    ai->ai_addrlen=(socklen_t)size;
    ai->ai_addr=(struct sockaddr *)(ai+1);
    if (family==AF_INET) {
        struct sockaddr_in *a=(struct sockaddr_in *)ai->ai_addr;
#if defined(__FreeBSD__) || defined(__PROSPERO__)
        a->sin_len=sizeof(*a);
#endif
        a->sin_family=AF_INET; a->sin_port=htons(port); memcpy(&a->sin_addr,address,4);
    } else {
        struct sockaddr_in6 *a=(struct sockaddr_in6 *)ai->ai_addr;
#if defined(__FreeBSD__) || defined(__PROSPERO__)
        a->sin6_len=sizeof(*a);
#endif
        a->sin6_family=AF_INET6; a->sin6_port=htons(port); memcpy(&a->sin6_addr,address,16);
    }
    *result=ai;
    return 0;
}
void freeaddrinfo(struct addrinfo *a) {
    while (a) { struct addrinfo *next=a->ai_next; free(a); a=next; }
}

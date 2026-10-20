// Linux Docker validation of the native numeric resolver's actual sockaddrs.
#include <assert.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
int main(void) {
    struct addrinfo hints={0},*a=NULL;
    hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_DGRAM;
    hints.ai_flags=AI_NUMERICHOST|AI_NUMERICSERV;
    assert(getaddrinfo("::1","9021",&hints,&a)==0);
    assert(a->ai_family==AF_INET6 && ntohs(((struct sockaddr_in6 *)a->ai_addr)->sin6_port)==9021);
    freeaddrinfo(a); a=NULL;
    assert(getaddrinfo("invalid.example","80",&hints,&a)==EAI_NONAME && !a);
    assert(getaddrinfo("127.0.0.1","65536",&hints,&a)==EAI_SERVICE && !a);
    assert(getaddrinfo("127.0.0.1","-1",&hints,&a)==EAI_SERVICE && !a);
    hints.ai_family=AF_UNIX;
    assert(getaddrinfo("127.0.0.1","80",&hints,&a)==EAI_FAMILY && !a);
    hints.ai_family=AF_UNSPEC;
    int server=socket(AF_INET,SOCK_DGRAM,0),client=socket(AF_INET,SOCK_DGRAM,0);
    assert(server>=0 && client>=0);
    struct sockaddr_in target={0}; target.sin_family=AF_INET; target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(server,(struct sockaddr *)&target,sizeof(target))==0);
    socklen_t length=sizeof(target);
    assert(getsockname(server,(struct sockaddr *)&target,&length)==0);
    char port[6]; snprintf(port,sizeof(port),"%u",ntohs(target.sin_port));
    assert(getaddrinfo("127.0.0.1",port,&hints,&a)==0);
    assert(connect(client,a->ai_addr,a->ai_addrlen)==0);
    assert(send(client,"muse",4,0)==4);
    struct timeval timeout={1,0};
    assert(setsockopt(server,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    char result[8]={0}; assert(recv(server,result,sizeof(result),0)==4 && !strcmp(result,"muse"));
    freeaddrinfo(a); close(client); close(server);
    puts("PASS: numeric IPv4/IPv6, rejected names/invalid ports, real loopback datagram; ASan+UBSan");
    return 0;
}

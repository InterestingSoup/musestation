#define _POSIX_C_SOURCE 200809L
#include "client_install_fds.h"
#include "log.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
int main(int argc,char **argv){
    assert(argc==3&&getuid()==0);char path[1024];int files[3];
    for(int i=0;i<3;i++){snprintf(path,sizeof(path),"%s/fd-journal-%d",argv[2],i);files[i]=open(path,O_RDWR|O_CREAT|O_EXCL,0600);assert(files[i]>=0);}
    unsigned char inactive=0;assert(write(files[1],&inactive,1)==1);unsigned char token[32];memset(token,0x67,sizeof(token));
    int sockets[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));pid_t child=fork();assert(child>=0);
    if(!child){
        close(sockets[0]);for(int i=0;i<3;i++)close(files[i]);assert(!setgid(65534)&&!setuid(65534));
        errno=0;assert(open(path,O_RDWR)==-1&&errno==EACCES);
        int received[3];assert(!muse_fds_receive(sockets[1],received,token));close(sockets[1]);
        struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};assert(!fcntl(received[2],F_SETLK,&lock));
        struct muse_config cfg;muse_config_defaults(&cfg);strcpy(cfg.build_mode,"hybrid");memset(cfg.relay_token,'a',64);cfg.relay_token[64]=0;snprintf(cfg.server_url,sizeof(cfg.server_url),"http://127.0.0.1:%s",argv[1]);
        struct muse_client client;muse_client_init(&client,&cfg);struct muse_log log;assert(!muse_log_open_fds(&log,&client,received[0],received[1]));
        assert(!muse_log_event(&log,"info","descriptor_access_test","Helper appended/fsynced/uploaded using received descriptors despite denied pathname access"));
        assert(!muse_log_flush(&log));muse_log_close(&log);
        for(int i=0;i<3;i++)close(received[i]);_exit(0);
    }
    close(sockets[1]);assert(!muse_fds_send(sockets[0],files,token));close(sockets[0]);int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
    unsigned char state=9;assert(pread(files[1],&state,1,0)==1&&state==0);struct stat st;assert(!fstat(files[0],&st)&&!st.st_size);
    /* Wrong nonce and non-regular descriptor must fail with all received fds closed. */
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));assert(!muse_fds_send(sockets[0],files,token));token[0]^=1;int rejected[3];assert(muse_fds_receive(sockets[1],rejected,token));for(int i=0;i<3;i++)assert(rejected[i]==-1);close(sockets[0]);close(sockets[1]);token[0]^=1;
    int wrong[3]={files[0],files[1],open(argv[2],O_RDONLY)};assert(wrong[2]>=0);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));assert(!muse_fds_send(sockets[0],wrong,token));assert(muse_fds_receive(sockets[1],rejected,token));close(wrong[2]);close(sockets[0]);close(sockets[1]);
    for(int i=0;i<3;i++)close(files[i]);puts("SCM_RIGHTS descriptor journal survives denied pathname access; invalid nonce/file types rejected");
}

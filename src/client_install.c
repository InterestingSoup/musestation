/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "client_install_protocol.h"
#include "client_install_fds.h"
#include "log.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
struct muse_install { struct muse_client *client; int fd; struct muse_install_status last; int64_t begun; };
static int error(struct muse_client *c,const char *detail){snprintf(c->error,sizeof(c->error),"%s",detail);return -1;}
static int send_journal_fds(int listener,const char *dir,const unsigned char token[32]){
    struct pollfd p={listener,POLLIN,0};if(poll(&p,1,5000)<=0)return -1;int socket=accept(listener,NULL,NULL);if(socket<0)return -1;
    unsigned char peer[32];int result=-1,files[3]={-1,-1,-1};
    if(muse_ipc_io(socket,peer,sizeof(peer),0,3000)||memcmp(peer,token,32))goto done;
    const char *names[]={"pending.jsonl","pending.jsonl.active","journal.lock"};char path[600];
    for(int i=0;i<3;i++){
        if(snprintf(path,sizeof(path),"%s/%s",dir,names[i])>=(int)sizeof(path))goto done;
        files[i]=open(path,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(files[i]>=0){if(i==1){unsigned char inactive=0;if(write(files[i],&inactive,1)!=1||fsync(files[i]))goto done;}}
        else if(errno==EEXIST)files[i]=open(path,O_RDWR|O_NOFOLLOW);
        if(files[i]<0||fsync(files[i]))goto done;
    }
    /* Persist file names before a helper can outlive the parent's sandbox. */
    {int directory=open(dir,O_RDONLY|O_DIRECTORY);if(directory<0)goto done;
     int bad=fsync(directory),saved=errno;close(directory);if(bad&&saved!=EINVAL&&saved!=ENOTSUP)goto done;}
    result=muse_fds_send(socket,files,token);
done:
    for(int i=0;i<3;i++)if(files[i]>=0)close(files[i]);close(socket);return result;
}
static int exchange(struct muse_install *s,uint32_t op,struct muse_install_request *request,struct muse_install_status *out,int timeout){
    request->version=MUSE_INSTALL_PROTOCOL;request->op=op;struct muse_install_response reply;
    if(muse_ipc_io(s->fd,request,sizeof(*request),1,10000)||muse_ipc_io(s->fd,&reply,sizeof(reply),0,timeout)||reply.version!=MUSE_INSTALL_PROTOCOL||reply.op!=op){
        *out=s->last;snprintf(out->detail,sizeof(out->detail),"Installer communication lost; accepted OS installation may continue. Check helper logs.");return error(s->client,out->detail);
    }
    if(!memchr(reply.status.state,0,sizeof(reply.status.state))||!memchr(reply.status.detail,0,sizeof(reply.status.detail))||!memchr(reply.status.title_id,0,sizeof(reply.status.title_id))||!memchr(reply.status.content_id,0,sizeof(reply.status.content_id)))return error(s->client,"Invalid installer response");
    s->last=reply.status;*out=s->last;if(reply.result)return error(s->client,out->detail);return 0;
}
static int spawn_helper(struct muse_client *c,const unsigned char token[32]){
    if(!c->config.install_helper_path[0])return error(c,"Bundled installer helper path is not configured");
    int file=open(c->config.install_helper_path,O_RDONLY|O_NOFOLLOW);if(file<0)return error(c,"Bundled installer helper is missing");struct stat st;
    if(fstat(file,&st)||!S_ISREG(st.st_mode)||st.st_size<64||st.st_size>2*1024*1024){close(file);return error(c,"Installer helper is invalid or too large");}
    size_t size=(size_t)st.st_size;unsigned char *elf=malloc(size);if(!elf){close(file);return error(c,"Installer allocation failed");}
    size_t at=0;while(at<size){ssize_t n=read(file,elf+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(size_t)n;}close(file);
    if(at!=size||memcmp(elf,"\177ELF",4)){free(elf);return error(c,"Cannot read installer ELF");}
    int listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0){free(elf);return error(c,"Cannot create installer callback socket");}
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=inet_addr("127.0.0.1");
    socklen_t address_size=sizeof(address);
    if(bind(listener,(struct sockaddr*)&address,sizeof(address))||listen(listener,4)||getsockname(listener,(struct sockaddr*)&address,&address_size)){close(listener);free(elf);return error(c,"Cannot bind installer loopback callback");}
    struct muse_install_boot boot={.marker=MUSE_INSTALL_BOOT_MARKER,.port=ntohs(address.sin_port)};memcpy(boot.token,token,32);size_t match=0;unsigned matches=0;
    for(size_t i=0;i+sizeof(boot)<=size;i++)if(!memcmp(elf+i,boot.marker,sizeof(boot.marker))){match=i;matches++;}
    if(matches!=1){close(listener);free(elf);return error(c,"Installer bootstrap marker missing or ambiguous");}memcpy(elf+match,&boot,sizeof(boot));
    int loader=socket(AF_INET,SOCK_STREAM,0);address.sin_port=htons(c->config.loader_port);
    if(loader<0){close(listener);free(elf);return error(c,"Cannot create local ELF loader socket");}
    fcntl(loader,F_SETFL,O_NONBLOCK);int connected=connect(loader,(struct sockaddr*)&address,sizeof(address));
    if(connected&&errno==EINPROGRESS){struct pollfd p={loader,POLLOUT,0};int e=0;socklen_t len=sizeof(e);connected=poll(&p,1,5000)>0&&!getsockopt(loader,SOL_SOCKET,SO_ERROR,&e,&len)&&!e?0:-1;}
    int sent=connected||muse_ipc_io(loader,elf,size,1,30000);free(elf);shutdown(loader,SHUT_WR);
    if(sent){close(loader);close(listener);return error(c,"Local ELF loader unavailable; reapply jailbreak and start elfldr");}
    int fd=-1;int64_t deadline=muse_ipc_now()+30000;
    while(muse_ipc_now()<deadline){struct pollfd p={listener,POLLIN,0};int wait=(int)(deadline-muse_ipc_now());if(wait<=0||poll(&p,1,wait)<=0)break;
        int candidate=accept(listener,NULL,NULL);if(candidate<0)continue;struct muse_install_response ready;
        if(!muse_ipc_io(candidate,&ready,sizeof(ready),0,3000)&&ready.version==MUSE_INSTALL_PROTOCOL&&ready.op==MUSE_INSTALL_READY&&!memcmp(ready.token,token,32)){
            if(!ready.result)fd=candidate;else{snprintf(c->error,sizeof(c->error),"AppInstUtil helper initialization failed (0x%08x)",(unsigned)ready.result);close(candidate);}break;
        }close(candidate);
    }
    close(loader);close(listener);if(fd<0&&!c->error[0])error(c,"Installer helper did not authenticate; runtime capability unavailable");return fd;
}
struct muse_install *muse_install_begin(struct muse_client *c,const char *path,const struct muse_build_status *manifest,struct muse_install_status *out){
    memset(out,0,sizeof(*out));out->progress_percent=-1;c->error[0]=0;
    if(muse_config_local(&c->config)){error(c,"Local apps run inside MuseStation; native installation is a hybrid operation");return NULL;}
    if(!muse_config_token_valid(c->config.relay_token)){error(c,"Relay console token is missing or invalid; run setup before installing");return NULL;}
    if(muse_client_verify_package(c,path,manifest))return NULL;
    unsigned char token[32];int entropy=open("/dev/urandom",O_RDONLY);if(entropy<0){error(c,"Cannot authenticate installer helper");return NULL;}
    ssize_t got=read(entropy,token,sizeof(token));close(entropy);if(got!=(ssize_t)sizeof(token)){error(c,"Cannot obtain installer authentication token");return NULL;}
    struct muse_install_request request={0};
    if(strlen(path)>=sizeof(request.path)){error(c,"Package path too long");return NULL;}
    strcpy(request.path,path);strcpy(request.server_url,c->config.server_url);strcpy(request.relay_token,c->config.relay_token);request.manifest=*manifest;
    if(snprintf(request.log_dir,sizeof(request.log_dir),"%s/installer",c->config.log_dir)>=(int)sizeof(request.log_dir)){error(c,"Installer journal path too long");return NULL;}
    if(mkdir(request.log_dir,0700)&&errno!=EEXIST){error(c,"Cannot create local installer journal directory");return NULL;}
    char override[1024]={0};
    if(c->config.installer_artifact_dir[0]){
        const char *base=strrchr(path,'/');if(!base||!base[1]||snprintf(override,sizeof(override),"%s/%s",c->config.installer_artifact_dir,base+1)>=(int)sizeof(override)){error(c,"Installer artifact path override is invalid");return NULL;}
    }
    if(muse_path_describe(path,override,&request.package_location)||muse_path_describe(request.log_dir,c->config.installer_log_dir,&request.journal_location)){error(c,"Cannot describe installer paths");return NULL;}
    char probe_path[600];snprintf(probe_path,sizeof(probe_path),"%s/.share-XXXXXX",request.log_dir);
    int probe=mkstemp(probe_path);if(probe<0){error(c,"Cannot create installer shared-journal probe");return NULL;}
    strcpy(request.probe_name,strrchr(probe_path,'/')+1);
    if(write(probe,token,sizeof(token))!=(ssize_t)sizeof(token)||fsync(probe)){close(probe);unlink(probe_path);error(c,"Cannot persist installer shared-journal probe");return NULL;}
    char socket_path[640];snprintf(socket_path,sizeof(socket_path),"%s.sock",probe_path);int unix_listener=muse_unix_listen(socket_path);
    if(unix_listener>=0)snprintf(request.socket_name,sizeof(request.socket_name),"%s.sock",request.probe_name);
    struct muse_install *s=calloc(1,sizeof(*s));if(!s){close(probe);unlink(probe_path);if(unix_listener>=0){close(unix_listener);unlink(socket_path);}error(c,"Installer allocation failed");return NULL;}s->client=c;s->fd=spawn_helper(c,token);s->begun=muse_ipc_now();
    if(s->fd<0){free(s);close(probe);unlink(probe_path);if(unix_listener>=0){close(unix_listener);unlink(socket_path);}return NULL;}
    int prepared=exchange(s,MUSE_INSTALL_PREPARE,&request,out,120000);
    /* A failed fd transfer can still use the helper's separately verified
       shared-writable-path fallback. No install has been requested yet. */
    if(!prepared&&unix_listener>=0)(void)send_journal_fds(unix_listener,request.log_dir,token);
    if(unix_listener>=0){close(unix_listener);unlink(socket_path);}
    if(prepared){close(probe);unlink(probe_path);muse_install_close(s);return NULL;}
    memset(&request,0,sizeof(request));
    int started=exchange(s,MUSE_INSTALL_START,&request,out,120000);close(probe);unlink(probe_path);
    if(started){muse_install_close(s);return NULL;}return s;
}
int muse_install_poll(struct muse_install *s,struct muse_install_status *out){
    if(!s)return -1;if(muse_ipc_now()-s->begun>1800000){*out=s->last;snprintf(out->detail,sizeof(out->detail),"Installation monitoring timed out after 30 minutes; OS task may continue.");return error(s->client,out->detail);}
    struct muse_install_request request={0};return exchange(s,MUSE_INSTALL_STATUS,&request,out,15000);
}
int muse_install_launch(struct muse_install *s,struct muse_install_status *out){
    if(!s)return -1;if(!s->last.completed){*out=s->last;return error(s->client,"Full OS installation must complete before launch; playable alone is insufficient");}struct muse_install_request request={0};return exchange(s,MUSE_INSTALL_LAUNCH,&request,out,45000);
}
void muse_install_close(struct muse_install *s){
    if(!s)return;struct muse_install_request request={.version=MUSE_INSTALL_PROTOCOL,.op=MUSE_INSTALL_CLOSE};struct muse_install_response response;
    if(!muse_ipc_io(s->fd,&request,sizeof(request),1,500)&&!muse_ipc_io(s->fd,&response,sizeof(response),0,1000)){
        struct pollfd p={s->fd,POLLIN,0};if(poll(&p,1,4000)>0){char discard;(void)recv(s->fd,&discard,1,0);}
    }
    shutdown(s->fd,SHUT_RDWR);close(s->fd);free(s);
}
int muse_install_recover_logs(struct muse_client *c){
    if(muse_config_local(&c->config))return 0;
    char dir[512],path[600];if(snprintf(dir,sizeof(dir),"%s/installer",c->config.log_dir)>=(int)sizeof(dir))return error(c,"Installer journal path too long");
    struct stat st;if(stat(dir,&st))return errno==ENOENT?0:error(c,"Cannot inspect installer journal");
    snprintf(path,sizeof(path),"%s/journal.lock",dir);int fd=open(path,O_RDWR|O_CREAT,0600);if(fd<0)return error(c,"Cannot open installer journal lock");
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_start=0,.l_len=0};
    if(fcntl(fd,F_SETLK,&lock)){int busy=errno==EACCES||errno==EAGAIN;close(fd);return busy?0:error(c,"Cannot lock installer journal");}
    struct muse_config config=c->config;strcpy(config.log_dir,dir);struct muse_client recovery;muse_client_init(&recovery,&config);struct muse_log log;
    int rc=muse_log_open(&log,&recovery);if(!rc){rc=muse_log_flush(&log);muse_log_close(&log);}close(fd);
    if(rc){c->http_status=recovery.http_status;
        if(c->http_status==401||c->http_status==403)return error(c,"Relay console key rejected; update it in setup. Installer journal retained.");
        return error(c,"Installer journal upload pending; durable spool retained");}return 0;
}

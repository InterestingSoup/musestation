/* SPDX-License-Identifier: GPL-3.0-or-later
 * Muse's one-job installer helper. AppInstUtil flow follows the public
 * ps5-pkg-manager helper architecture; this implementation adds authenticated
 * ephemeral loopback IPC, verified content identity and no-replacement policy.
 * It never patches firmware, removes titles, or injects into system processes.
 */
#define _POSIX_C_SOURCE 200809L
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef MUSE_INSTALL_OBSERVE_MS
#define MUSE_INSTALL_OBSERVE_MS 30000
#endif
#include "client_install_protocol.h"
#include "client_install_fds.h"
#include "appinst_abi.h"
#include "appinst_runtime.h"
#include "log.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
/* Patched in the trusted bundled ELF before elfldr submission. */
volatile struct muse_install_boot muse_install_bootstrap={.marker=MUSE_INSTALL_BOOT_MARKER};
struct watcher { int fd; atomic_int done,detached_observation,parent_gone; };
static void *watch_parent(void *arg){
    struct watcher *w=arg;while(!atomic_load(&w->done)){
        struct pollfd p={w->fd,POLLIN,0};int rc=poll(&p,1,100);
        if(rc>0){char b;ssize_t n=recv(w->fd,&b,1,MSG_PEEK);
            if(!n||(p.revents&(POLLHUP|POLLNVAL))){atomic_store(&w->parent_gone,1);if(!atomic_load(&w->detached_observation))_exit(0);return NULL;}
        }struct timespec delay={0,10000000};nanosleep(&delay,NULL);
    }return NULL;
}
static void describe(struct muse_install_status *s,const char *operation,int rc){
    s->native_error=rc;snprintf(s->detail,sizeof(s->detail),"%s failed (0x%08x); accepted OS tasks are not rolled back",operation,(unsigned)rc);strcpy(s->state,"error");
}
static int record(struct muse_log *log,const char *event,const struct muse_install_status *s,int error){
    char detail[768];snprintf(detail,sizeof(detail),"title=%s content=%s state=%s submitted=%d installed=%d completed=%d launch_requested=%d running_observed=%d progress=%d native=0x%08x app_id=%d; %s",s->title_id,s->content_id,s->state,s->submitted,s->installed,s->completed,s->launch_requested,s->running_observed,s->progress_percent,(unsigned)s->native_error,s->app_id,s->detail);
    return muse_log_event(log,error?"error":"info",event,detail);
}
static void journal_error(struct muse_install_status *status,const struct muse_client *client,const char *fallback){
    snprintf(status->detail,sizeof(status->detail),"%s",client->http_status==401||client->http_status==403
        ?"Relay console key rejected; update it in setup. Installer journal retained.":fallback);
}
static int status_query(const struct muse_appinst_api *api,struct muse_install_status *s){
    muse_native_install_status native={0};int rc=api->install_status(s->content_id,&native);
    if(rc){describe(s,"GetInstallStatus",rc);return -1;}
    native.status[sizeof(native.status)-1]=0;native.error_info.description[sizeof(native.error_info.description)-1]=0;
    if(native.error_info.error_code||!strcmp(native.status,"error")||!strcmp(native.status,"none")){
        describe(s,"OS installation",native.error_info.error_code?native.error_info.error_code:-1);return -1;
    }
    if(!s->launch_requested)snprintf(s->state,sizeof(s->state),"%s",native.status);
    s->progress_percent=-1;
    if(native.total_size&&native.downloaded_size<=native.total_size)s->progress_percent=(int)((long double)native.downloaded_size*100/native.total_size);
    if(native.is_copy_only&&native.local_copy_percent>=0&&native.local_copy_percent<=100)s->progress_percent=native.local_copy_percent;
    s->installed=!strcmp(native.status,"completed")||!strcmp(native.status,"playable");
    s->completed=!strcmp(native.status,"completed");
    if(s->completed)s->progress_percent=100;
    snprintf(s->detail,sizeof(s->detail),"OS reports %s; copied/downloaded %llu of %llu bytes, promotion %u%%",native.status,(unsigned long long)native.downloaded_size,(unsigned long long)native.total_size,native.promote_progress);
    return 0;
}
static int existing_title(const struct muse_appinst_api *api,const char *title){
    /* The public output-flag ABI is checked at runtime. A missing write is
       unsupported, never interpreted as an absent app. */
    int exists=-1;int rc=api->app_exists(title,&exists);
    if(rc||exists!=0)return rc?rc:-2005;
    return 0;
}
struct package_probe { struct muse_client *client; const struct muse_build_status *manifest; };
static int probe_package(const char *path,void *arg){struct package_probe *p=arg;return muse_client_verify_package(p->client,path,p->manifest);}
struct journal_probe { const char *name; const unsigned char *token; int writable; };
static int probe_journal(const char *dir,void *arg){
    struct journal_probe *p=arg;char path[1200];if(snprintf(path,sizeof(path),"%s/%s",dir,p->name)>=(int)sizeof(path))return -1;
    int fd=open(path,(p->writable?O_RDWR:O_RDONLY)|O_NOFOLLOW);if(fd<0)return -1;
    struct stat st;unsigned char data[32],ack[32];int bad=fstat(fd,&st)||!S_ISREG(st.st_mode)||(st.st_size!=32&&st.st_size!=64)||pread(fd,data,32,0)!=32||memcmp(data,p->token,32);
    for(int i=0;i<32;i++)ack[i]=(unsigned char)(p->token[i]^0xa5);
    if(!bad&&p->writable)bad=pwrite(fd,ack,32,32)!=32||fsync(fd);close(fd);return bad?-1:0;
}
int muse_install_helper_serve(int fd,const unsigned char token[32]){
    struct watcher watch={.fd=fd};atomic_init(&watch.done,0);atomic_init(&watch.detached_observation,0);atomic_init(&watch.parent_gone,0);
    pthread_t thread;if(pthread_create(&thread,NULL,watch_parent,&watch))return 1;
    /* READY authenticates the IPC endpoint only. Capabilities are loaded after
       START establishes a durable, authenticated journal, before any OS action. */
    struct muse_appinst_api api={0};int initialized=0;
    struct muse_install_response response={.version=MUSE_INSTALL_PROTOCOL,.op=MUSE_INSTALL_READY,.result=0};memcpy(response.token,token,32);
    int used=0,prepared=0,preparation_attempted=0,opened=0,journal_lock=-1,bridge=-1,log_fds[3]={-1,-1,-1};struct muse_client client;struct muse_log journal;struct muse_install_request kept={0};
    muse_pkg_metadata metadata={0};muse_pkg_info info={0};muse_playgo_info playgo={0};
    muse_launch_context launch_ctx={0};const char *launch_argv[]={NULL};
    if(muse_ipc_io(fd,&response,sizeof(response),1,10000))goto done;
    for(;;){
        struct muse_install_request request;
        if(muse_ipc_io(fd,&request,sizeof(request),0,120000)||request.version!=MUSE_INSTALL_PROTOCOL)break;
        if(!memchr(request.path,0,sizeof(request.path))||!memchr(request.server_url,0,sizeof(request.server_url))||!memchr(request.log_dir,0,sizeof(request.log_dir))||!memchr(request.relay_token,0,sizeof(request.relay_token))||!memchr(request.manifest.kind,0,sizeof(request.manifest.kind))||!memchr(request.manifest.title_id,0,sizeof(request.manifest.title_id))||!memchr(request.manifest.content_id,0,sizeof(request.manifest.content_id))||!memchr(request.manifest.sha256,0,sizeof(request.manifest.sha256))||!memchr(request.manifest.state,0,sizeof(request.manifest.state)))break;
        response.op=request.op;response.result=-1;
        if(request.op==MUSE_INSTALL_PREPARE&&!preparation_attempted){
            preparation_attempted=1;
            if(!muse_config_token_valid(request.relay_token)||!muse_config_private_origin(request.server_url)){strcpy(response.status.detail,"Invalid relay credential or private relay origin; nothing installed");goto reply;}
            kept=request;struct muse_config config;muse_config_defaults(&config);strcpy(config.build_mode,"hybrid");strcpy(config.server_url,kept.server_url);strcpy(config.relay_token,kept.relay_token);config.timeout_ms=3000;muse_client_init(&client,&config);
            response.status.progress_percent=-1;strcpy(response.status.title_id,kept.manifest.title_id);strcpy(response.status.content_id,kept.manifest.content_id);
            if(!memchr(kept.probe_name,0,sizeof(kept.probe_name))||strncmp(kept.probe_name,".share-",7)||strchr(kept.probe_name,'/')||strstr(kept.probe_name,"..")){strcpy(response.status.detail,"Invalid shared-journal probe");goto reply;}
            struct package_probe package={&client,&kept.manifest};
            if(muse_path_resolve(&kept.package_location,probe_package,&package,kept.path,sizeof(kept.path))){strcpy(response.status.detail,"Helper cannot access the verified package in this filesystem namespace. Configure installer_artifact_dir or shared artifact storage; nothing installed.");goto reply;}
            if(!memchr(kept.socket_name,0,sizeof(kept.socket_name))||strchr(kept.socket_name,'/')||strstr(kept.socket_name,"..")){strcpy(response.status.detail,"Invalid descriptor bridge path");goto reply;}
            struct journal_probe probe={kept.probe_name,token,kept.socket_name[0]?0:1};
            if(muse_path_resolve(&kept.journal_location,probe_journal,&probe,kept.log_dir,sizeof(kept.log_dir))){strcpy(response.status.detail,"Helper cannot share a writable journal with Muse. Configure installer_log_dir or shared log storage; nothing installed.");goto reply;}
            strcpy(client.config.log_dir,kept.log_dir);
            if(kept.socket_name[0]){
                char socket_path[640];snprintf(socket_path,sizeof(socket_path),"%s/%s",kept.log_dir,kept.socket_name);bridge=muse_unix_connect(socket_path);
                if(bridge>=0&&muse_ipc_io(bridge,(void*)token,32,1,5000)){close(bridge);bridge=-1;}
                if(bridge<0){probe.writable=1;if(probe_journal(kept.log_dir,&probe)){strcpy(response.status.detail,"Descriptor bridge unavailable and helper cannot write shared journal; configure shared installer_log_dir. Nothing installed.");goto reply;}}
            }
            prepared=1;strcpy(response.status.state,"prepared");strcpy(response.status.detail,"Verified helper-visible package and shared journal namespace");response.result=0;
        }else if(request.op==MUSE_INSTALL_START&&prepared&&!used){
            used=1;
            int descriptors=bridge>=0&&!muse_fds_receive(bridge,log_fds,token);if(bridge>=0){close(bridge);bridge=-1;}
            char lock_path[600];snprintf(lock_path,sizeof(lock_path),"%s/journal.lock",kept.log_dir);
            if(descriptors)journal_lock=log_fds[2];
            else{struct journal_probe probe={kept.probe_name,token,1};if(probe_journal(kept.log_dir,&probe)){strcpy(response.status.detail,"Writable journal descriptors unavailable and shared path is read-only; nothing installed");goto reply;}journal_lock=open(lock_path,O_RDWR|O_CREAT,0600);}
            struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_start=0,.l_len=0};
            if(journal_lock<0||fcntl(journal_lock,F_SETLK,&lock)){strcpy(response.status.detail,"Previous installer helper still owns its journal");goto reply;}
            opened=(descriptors?muse_log_open_fds(&journal,&client,log_fds[0],log_fds[1]):muse_log_open(&journal,&client))==0;
            if(!opened){snprintf(response.status.detail,sizeof(response.status.detail),"Installer journal cannot be persisted");goto reply;}
            if(muse_log_event(&journal,"info","installer_journal",descriptors?"Authenticated writable file descriptors; no sandbox pathname writes":"Verified shared writable path fallback")||muse_log_flush(&journal)){journal_error(&response.status,&client,"Cannot persist/upload installer journal capability; nothing installed");goto reply;}
            if(muse_client_verify_package(&client,kept.path,&kept.manifest)){snprintf(response.status.detail,sizeof(response.status.detail),"%s",client.error);goto reply;}
            int rc=muse_appinst_load(&api,response.status.detail,sizeof(response.status.detail));
            if(rc){response.status.native_error=rc;strcpy(response.status.state,"error");goto reply;}
            rc=api.initialize();if(rc){describe(&response.status,"AppInstUtil Initialize",rc);goto reply;}initialized=1;
            rc=existing_title(&api,kept.manifest.title_id);if(rc){describe(&response.status,"New-title check (existing title or unsupported query)",rc);goto reply;}
            strcpy(response.status.state,"submitting");strcpy(response.status.detail,"Verified package; submitting one new homebrew title");if(record(&journal,"install_submit",&response.status,0)){journal_error(&response.status,&client,"Cannot persist/flush installation audit log");goto reply;}
            metadata=(muse_pkg_metadata){kept.path,"","","",kept.manifest.title_id,""};
            rc=api.install_package(&metadata,&info,&playgo);
            if(rc){describe(&response.status,"InstallByPackage",rc);goto reply;}
            response.status.submitted=1;
            if(!memchr(info.content_id,0,sizeof(info.content_id))||strcmp(info.content_id,kept.manifest.content_id)){describe(&response.status,"Installer content identity mismatch",-2006);goto reply;}
            strcpy(response.status.state,"submitted");strcpy(response.status.detail,"OS accepted installation; completion is not yet confirmed");response.result=0;
        }else if(request.op==MUSE_INSTALL_STATUS&&response.status.submitted){
            response.result=status_query(&api,&response.status);
        }else if(request.op==MUSE_INSTALL_LAUNCH&&response.status.completed&&!response.status.launch_requested){
            if(status_query(&api,&response.status))goto reply;
            if(!response.status.completed){strcpy(response.status.detail,"OS no longer reports full installation completed; no launch was submitted");goto reply;}
            memset(&launch_ctx,0,sizeof(launch_ctx));launch_ctx.structsize=sizeof(launch_ctx);
            int rc=sceUserServiceGetForegroundUser(&launch_ctx.user_id);
            if(rc){describe(&response.status,"Foreground user query",rc);goto reply;}
            strcpy(response.status.state,"launch_request");if(record(&journal,"launch_request",&response.status,0)||muse_log_flush(&journal)){journal_error(&response.status,&client,"Cannot persist/upload launch audit log");goto reply;}
            atomic_store(&watch.detached_observation,1);
            rc=sceSystemServiceLaunchApp(kept.manifest.title_id,launch_argv,&launch_ctx);
            if(rc<0){describe(&response.status,"SystemServiceLaunchApp",rc);goto reply;}
            response.status.launch_requested=1;strcpy(response.status.state,"launch_requested");strcpy(response.status.detail,"OS accepted launch; observing the matching running title");record(&journal,"launch_accepted",&response.status,0);muse_log_flush(&journal);
            int64_t deadline=muse_ipc_now()+MUSE_INSTALL_OBSERVE_MS;
            while(muse_ipc_now()<deadline){
                int app=sceSystemServiceGetAppIdOfRunningBigApp();char title[32]={0};
                if(app>0&&sceLncUtilGetAppTitleId((uint32_t)app,title)==0&&memchr(title,0,sizeof(title))&&!strcmp(title,kept.manifest.title_id)){
                    response.status.running_observed=1;response.status.app_id=app;strcpy(response.status.state,"running_observed");strcpy(response.status.detail,"System running-title query matches this installed app");break;
                }struct timespec delay={0,200000000};nanosleep(&delay,NULL);
            }
            if(!response.status.running_observed){strcpy(response.status.state,"launch_unconfirmed");strcpy(response.status.detail,"Launch accepted, but matching running title was not observed within 30 seconds; check the console");}
            response.result=response.status.running_observed?0:-1;
        }else if(request.op==MUSE_INSTALL_CLOSE){response.result=0;goto reply;}
        else{snprintf(response.status.detail,sizeof(response.status.detail),"Invalid or duplicate installer transition");}
reply:
        if(opened){record(&journal,"installer_status",&response.status,response.result!=0);if(response.status.installed||response.status.launch_requested||response.result)muse_log_flush(&journal);}
        if(atomic_load(&watch.parent_gone)||muse_ipc_io(fd,&response,sizeof(response),1,10000)||request.op==MUSE_INSTALL_CLOSE)break;
    }
done:
    if(initialized)api.terminate();if(opened){muse_log_event(&journal,"info","session_end","Installer monitoring ended; accepted OS tasks are not cancelled");muse_log_close(&journal);}
    if(bridge>=0)close(bridge);for(int i=0;i<3;i++)if(log_fds[i]>=0)close(log_fds[i]);if(journal_lock>=0&&journal_lock!=log_fds[2])close(journal_lock);
    atomic_store(&watch.done,1);pthread_join(thread,NULL);close(fd);return 0;
}
#ifndef MUSE_INSTALL_HELPER_TEST
int main(void){
    struct muse_install_boot boot;memcpy(&boot,(const void *)&muse_install_bootstrap,sizeof(boot));if(!boot.port||boot.port>65535)return 2;
    sceNetCtlInit();sceNetInit();int priority=256;sceUserServiceInitialize(&priority);
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 2;struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((uint16_t)boot.port);address.sin_addr.s_addr=inet_addr("127.0.0.1");
    if(connect(fd,(struct sockaddr*)&address,sizeof(address))){close(fd);return 2;}return muse_install_helper_serve(fd,boot.token);
}
#endif

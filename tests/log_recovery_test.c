/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "log.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int posts,offline,fail_file_sync,fail_directory_sync,file_syncs,directory_syncs;
static char *last_body;
int __real_fsync(int);
int __wrap_fsync(int fd){
    struct stat st;assert(!fstat(fd,&st));
    if(S_ISDIR(st.st_mode)){directory_syncs++;if(fail_directory_sync){fail_directory_sync=0;errno=EIO;return -1;}}
    else{file_syncs++;if(fail_file_sync){fail_file_sync=0;errno=EIO;return -1;}}
    return __real_fsync(fd);
}
int muse_client_http(struct muse_client *client,const char *origin,const char *method,const char *path,
    const char *type,const void *body,size_t length,size_t max,unsigned char **reply,size_t *reply_length){
    (void)client;(void)origin;(void)max;assert(!strcmp(method,"POST")&&!strcmp(path,"/logs")&&!strcmp(type,"application/x-ndjson"));
    assert(file_syncs>0&&length>0&&((const char*)body)[length-1]=='\n');
    posts++;free(last_body);last_body=malloc(length+1);assert(last_body);memcpy(last_body,body,length);last_body[length]=0;
    *reply=NULL;*reply_length=0;return offline?-1:0;
}
static void client(struct muse_client *c,const char *root,const char *name,int local){
    memset(c,0,sizeof(*c));muse_config_defaults(&c->config);strcpy(c->config.build_mode,local?"local":"hybrid");
    snprintf(c->config.log_dir,sizeof c->config.log_dir,"%s/%s",root,name);
}
static off_t bytes(const char *path){struct stat st;assert(!stat(path,&st));return st.st_size;}
static void marker_path(const struct muse_log *log,char out[1100]){snprintf(out,1100,"%s.active",log->path);}
static int contains(const char *path,const char *text){
    int fd=open(path,O_RDONLY);assert(fd>=0);char b[8192];ssize_t n=read(fd,b,sizeof b-1);assert(n>=0);b[n]=0;close(fd);return strstr(b,text)!=NULL;
}
static void append(const char *path,const char *text){int fd=open(path,O_WRONLY|O_APPEND);assert(fd>=0);assert(write(fd,text,strlen(text))==(ssize_t)strlen(text));assert(!fsync(fd));close(fd);}

int main(int argc,char **argv){
    assert(argc==2);struct muse_client c;struct muse_log log,recovered;char marker[1100];
    client(&c,argv[1],"killed-process",0);
    pid_t child=fork();assert(child>=0);
    if(!child){assert(!muse_log_open(&log,&c));assert(!muse_log_event(&log,"info","before_crash","durable child event"));_exit(0);}
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status));
    assert(!muse_log_open(&recovered,&c));assert(posts==1&&!recovered.recovery_pending);
    assert(strstr(last_body,"before_crash")&&strstr(last_body,"\"event\":\"crash\""));
    assert(strstr(last_body,"before_crash")<strstr(last_body,"\"event\":\"crash\""));
    assert(!bytes(recovered.path));muse_log_close(&recovered);
    puts("process-exit recovery persists crash event before one combined replay");

    client(&c,argv[1],"offline",0);assert(!muse_log_open(&log,&c));offline=1;
    int before=posts;assert(muse_log_event(&log,"error","failure","must survive lost POST"));assert(posts==before+1&&log.recovery_pending);
    off_t pending=bytes(log.path);assert(pending>0);marker_path(&log,marker);
    assert(muse_log_event(&log,"info","operation_begin","must not overtake recovery"));assert(bytes(log.path)==pending);
    assert(muse_log_event(&log,"error","second_failure","later failure still retained"));
    assert(muse_log_event(&log,"info","session_end","offline close retained"));
    assert(contains(log.path,"later failure still retained")&&contains(log.path,"offline close retained"));pending=bytes(log.path);
    muse_log_close(&log);assert(!access(marker,F_OK)&&bytes(log.path)==pending);
    append(log.path,"{\"incomplete_tail\":");
    assert(!muse_log_open(&recovered,&c)&&recovered.recovery_pending);
    assert(contains(recovered.path,"must survive lost POST")&&contains(recovered.path,"\"event\":\"crash\""));
    assert(!contains(recovered.path,"incomplete_tail"));
    pending=bytes(recovered.path);assert(muse_log_event(&recovered,"info","operation_begin","still blocked"));assert(bytes(recovered.path)==pending);
    offline=0;assert(!muse_log_event(&recovered,"info","operation_begin","after recovery"));
    assert(!recovered.recovery_pending&&strstr(last_body,"must survive lost POST")&&!strstr(last_body,"after recovery"));
    assert(contains(recovered.path,"after recovery"));muse_log_close(&recovered);assert(access(marker,F_OK)<0);
    puts("failed POST/close retain spool+marker; incomplete tail repaired; work waits for replay");

    client(&c,argv[1],"private",1);before=posts;assert(!muse_log_open(&log,&c));
    assert(!muse_log_event(&log,"error","secret_local_event","private local detail"));
    assert(!muse_log_open(&recovered,&c));assert(contains(recovered.path,"\"event\":\"crash\""));
    strcpy(c.config.build_mode,"hybrid");assert(!muse_log_flush(&recovered));muse_log_close(&recovered);
    assert(posts==before&&contains(recovered.path,"private local detail"));
    puts("local journals remain private across recovery and an in-memory mode change");

    client(&c,argv[1],"hybrid-to-local",0);assert(!muse_log_open(&log,&c));offline=1;
    assert(muse_log_event(&log,"error","failure","old Hybrid evidence"));marker_path(&log,marker);
    pending=bytes(log.path);before=posts;strcpy(c.config.build_mode,"local");muse_log_close(&log);
    assert(posts==before&&!access(marker,F_OK)&&bytes(log.path)==pending);
    strcpy(c.config.build_mode,"hybrid");offline=0;assert(!muse_log_open(&recovered,&c));
    assert(strstr(last_body,"old Hybrid evidence")&&strstr(last_body,"\"event\":\"crash\""));muse_log_close(&recovered);
    puts("entering Local sends no HTTP and preserves the unacknowledged Hybrid marker");

    client(&c,argv[1],"fd-mode",0);char spoolpath[1100],markerfile[1100];
    snprintf(spoolpath,sizeof spoolpath,"%s/fd-spool",argv[1]);snprintf(markerfile,sizeof markerfile,"%s/fd-marker",argv[1]);
    int spool=open(spoolpath,O_RDWR|O_CREAT|O_EXCL,0600),mark=open(markerfile,O_RDWR|O_CREAT|O_EXCL,0600);assert(spool>=0&&mark>=0);
    unsigned char state=0;assert(write(mark,&state,1)==1);assert(!muse_log_open_fds(&log,&c,spool,mark));
    offline=1;assert(muse_log_event(&log,"error","fd_failure","descriptor data retained"));muse_log_close(&log);
    assert(pread(mark,&state,1,0)==1&&state==1&&bytes(spoolpath)>0);
    assert(!muse_log_open_fds(&recovered,&c,spool,mark)&&recovered.recovery_pending);offline=0;
    assert(!muse_log_flush(&recovered)&&strstr(last_body,"fd_failure")&&strstr(last_body,"\"event\":\"crash\""));
    muse_log_close(&recovered);assert(pread(mark,&state,1,0)==1&&state==0&&!bytes(spoolpath));close(spool);close(mark);
    puts("borrowed-descriptor recovery has the same acknowledgement and marker guarantees");

    client(&c,argv[1],"sync-failure",0);assert(!muse_log_open(&log,&c));before=posts;fail_file_sync=1;
    assert(muse_log_event(&log,"error","fsync_failure","not acknowledged as durable"));assert(posts==before);
    assert(!muse_log_open(&recovered,&c));muse_log_close(&recovered);
    client(&c,argv[1],"directory-sync-failure",0);before=posts;fail_directory_sync=1;
    assert(muse_log_open(&log,&c)&&posts==before);
    assert(directory_syncs>0);
    puts("file/directory fsync failure stops success reporting and avoids premature upload");
    client(&c,argv[1],"fifo",0);assert(!mkdir(c.config.log_dir,0700));
    snprintf(spoolpath,sizeof spoolpath,"%s/pending.jsonl",c.config.log_dir);assert(!mkfifo(spoolpath,0600));
    assert(muse_log_open(&log,&c));assert(!unlink(spoolpath));
    int plain=open(spoolpath,O_RDWR|O_CREAT,0600);assert(plain>=0);close(plain);
    snprintf(markerfile,sizeof markerfile,"%s.active",spoolpath);assert(!mkfifo(markerfile,0600));
    assert(muse_log_open(&log,&c));
    puts("FIFO spool/marker rejected without blocking startup");
    free(last_body);return 0;
}

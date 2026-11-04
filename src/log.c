/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "log.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

/* Calls are serialized by the worker or the installer journal lock. HTTP is
 * never attempted from a signal handler; a surviving marker is replayed later. */
static char last_error[160];
const char *muse_log_error(void){return last_error;}
/* Records the failing step for the on-screen error; always returns -1. */
static int note(const char *step){int saved=errno;snprintf(last_error,sizeof last_error,"%s: %s",step,saved?strerror(saved):"invalid state");errno=saved;return -1;}
/* The filesystem or the title sandbox declining a directory sync is not an I/O failure. */
static int no_directory_sync(int e){return e==EINVAL||e==ENOTSUP||e==EOPNOTSUPP||e==EPERM||e==EACCES||e==EBADF;}
static int sync_parent(const char *path){
    char parent[1100];if(!path||strlen(path)>=sizeof parent)return note("journal path");strcpy(parent,path);
    char *slash=strrchr(parent,'/');if(!slash)return note("journal path");if(slash==parent)slash[1]=0;else *slash=0;
    int fd=open(parent,O_RDONLY|O_DIRECTORY);if(fd<0)return no_directory_sync(errno)?0:note("open journal directory");
    int rc=fsync(fd),saved=errno;if(close(fd)&&!rc)return note("close journal directory");
    /* These filesystems still get file fsync, but cannot promise directory durability. */
    if(rc&&!no_directory_sync(saved)){errno=saved;return note("fsync journal directory");}
    return 0;
}
static int mkdirs(const char *path){
    char s[1024];if(!path||strlen(path)>=sizeof(s)||*path!='/')return -1;strcpy(s,path);
    for(char *p=s+1;;p++){if(*p!='/'&&*p)continue;char ch=*p;*p=0;
        int created=mkdir(s,0700)==0;if(!created&&errno!=EEXIST)return -1;
        struct stat st;if(stat(s,&st)||!S_ISDIR(st.st_mode)||(created&&sync_parent(s)))return -1;
        *p=ch;if(!ch)break;
    }return 0;
}
/* Regular file only. No link-count check: a PS5 12.40 title's fstat does not
 * report st_nlink as 1 for its own private files; O_NOFOLLOW and 0700 parents remain. */
static int regular(int fd){struct stat st;errno=0;return fstat(fd,&st)||!S_ISREG(st.st_mode)?-1:0;}
static int repair_fd(int fd){
    struct stat st;if(fstat(fd,&st)||st.st_size<0||st.st_size>8*1024*1024)return -1;
    off_t end=st.st_size,keep=0;char block[4096];
    while(end>0){size_t n=end>(off_t)sizeof(block)?sizeof(block):(size_t)end;off_t start=end-(off_t)n;
        ssize_t got=pread(fd,block,n,start);if(got<0&&errno==EINTR)continue;if(got!=(ssize_t)n)return -1;
        for(size_t i=n;i>0;i--)if(block[i-1]=='\n'){keep=start+(off_t)i;goto found;}end=start;
    }
found:;
    return keep!=st.st_size&&(ftruncate(fd,keep)||fsync(fd))?-1:0;
}
static int marker_active(int fd){unsigned char state=1;return pread(fd,&state,1,0)!=1||state!=0;}
static int set_marker(int fd,unsigned char state){return pwrite(fd,&state,1,0)!=1||ftruncate(fd,1)||fsync(fd)?-1:0;}
static int append_record(struct muse_log *log,const char *level,const char *event,const char *detail){
    char qlevel[100],qevent[390],qdetail[24580],line[26000];
    if(muse_json_quote(level,qlevel,sizeof qlevel)||muse_json_quote(event,qevent,sizeof qevent)||muse_json_quote(detail,qdetail,sizeof qdetail))return -1;
    int len=snprintf(line,sizeof line,"{\"ts\":%lld,\"level\":%s,\"event\":%s,\"detail\":%s}\n",(long long)time(NULL),qlevel,qevent,qdetail);
    if(len<0||len>=(int)sizeof line)return -1;
    int fd=log->fd_mode?log->spool_fd:open(log->path,O_WRONLY|O_APPEND|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);
    if(fd<0)return -1;struct stat st;int rc=regular(fd)||fstat(fd,&st)||st.st_size<0||st.st_size>8*1024*1024-len;size_t at=0;
    while(!rc&&at<(size_t)len){ssize_t n=write(fd,line+at,(size_t)len-at);if(n<0&&errno==EINTR)continue;if(n<=0){rc=-1;break;}at+=(size_t)n;}
    if(!rc&&fsync(fd)){if(!no_directory_sync(errno))rc=note("fsync append");}if(!log->fd_mode){if(close(fd)&&!rc)rc=note("close append");if(!rc&&sync_parent(log->path))rc=-1;}
    if(!rc)log->pending++;return rc;
}
static int finish_open(struct muse_log *log,int unclean){
    struct stat st;if(log->fd_mode?fstat(log->spool_fd,&st):stat(log->path,&st))return -1;
    log->pending=st.st_size>0;
    /* Persist recovered event BEFORE uploading/truncating any old records. */
    if(unclean&&append_record(log,"error","crash",
        "Previous session did not complete durable journal cleanup (process termination or an offline close). Recovered prior records; no crash cause is inferred."))return -1;
    if(log->pending&&!log->local_only&&muse_log_flush(log))log->recovery_pending=1;
    return 0;
}
int muse_log_open_fds(struct muse_log *log,struct muse_client *client,int spool,int marker){
    struct stat a,b;int af=fcntl(spool,F_GETFL),bf=fcntl(marker,F_GETFL);
    if(af<0||bf<0||(af&O_ACCMODE)!=O_RDWR||(bf&O_ACCMODE)!=O_RDWR||regular(spool)||regular(marker)||
       fstat(spool,&a)||fstat(marker,&b)||(a.st_dev==b.st_dev&&a.st_ino==b.st_ino)||a.st_size>8*1024*1024||b.st_size>1)return -1;
    memset(log,0,sizeof(*log));log->client=client;log->local_only=muse_config_local(&client->config);
    log->fd_mode=1;log->spool_fd=spool;log->marker_fd=marker;
    if(fcntl(spool,F_SETFL,af|O_APPEND)||repair_fd(spool))return -1;
    int unclean=marker_active(marker);if(set_marker(marker,1))return -1;
    /* Descriptor supplier fsyncs its parent directory before transfer. */
    return finish_open(log,unclean);
}
int muse_log_open(struct muse_log *log,struct muse_client *client){
    memset(log,0,sizeof(*log));log->client=client;log->local_only=muse_config_local(&client->config);
    char directory[1024];if(snprintf(directory,sizeof directory,"%s%s",client->config.log_dir,log->local_only?"/local":"")>=(int)sizeof directory)return -1;
    last_error[0]=0;
    if(mkdirs(directory))return last_error[0]?-1:note("create journal directory");
    if(snprintf(log->path,sizeof log->path,"%s/pending.jsonl",directory)>=(int)sizeof log->path)return note("journal path");
    int spool=open(log->path,O_RDWR|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);if(spool<0)return note("open journal");
    int bad=regular(spool)?note("journal is not a regular file"):repair_fd(spool)?note("repair journal"):fsync(spool)?note("fsync journal"):0;
    if(close(spool)&&!bad)bad=note("close journal");if(bad||sync_parent(log->path))return -1;
    char marker[1040];snprintf(marker,sizeof marker,"%s.active",log->path);
    int fd=open(marker,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK,0600),unclean=0;
    if(fd<0&&errno==EEXIST){fd=open(marker,O_RDWR|O_NOFOLLOW|O_NONBLOCK);if(fd>=0)unclean=marker_active(fd);}
    if(fd<0)return note("open journal marker");bad=regular(fd)?note("marker is not a regular file"):set_marker(fd,1)?note("write journal marker"):0;
    if(close(fd)&&!bad)bad=note("close journal marker");if(bad||sync_parent(marker))return -1;
    if(finish_open(log,unclean))return last_error[0]?-1:note("journal recovery");
    return 0;
}
int muse_log_flush(struct muse_log *log){
    if(!log||!log->client)return -1;
    /* Local records remain private even if this client's mode later changes. */
    if(log->local_only||muse_config_local(&log->client->config))return 0;
    int fd=log->fd_mode?log->spool_fd:open(log->path,O_RDWR|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return -1;struct stat st;int rc=regular(fd)||fstat(fd,&st)||st.st_size<0||st.st_size>8*1024*1024;
    if(!rc&&st.st_size){
        size_t size=(size_t)st.st_size,used=0;unsigned char *data=malloc(size);
        if(!data)rc=-1;
        while(!rc&&used<size){ssize_t n=pread(fd,data+used,size-used,(off_t)used);if(n<0&&errno==EINTR)continue;if(n<=0){rc=-1;break;}used+=(size_t)n;}
        if(!rc){unsigned char *reply=NULL;size_t length=0;
            rc=muse_client_http(log->client,log->client->config.server_url,"POST","/logs","application/x-ndjson",data,size,4096,&reply,&length);free(reply);
        }free(data);
        /* Lost acknowledgements replay records: delivery is at least once. */
        if(!rc&&(ftruncate(fd,0)||fsync(fd)))rc=-1;
    }
    if(!log->fd_mode&&close(fd))rc=-1;
    if(rc)log->recovery_pending=1;else{log->pending=0;log->recovery_pending=0;}return rc?-1:0;
}
int muse_log_event(struct muse_log *log,const char *level,const char *event,const char *detail){
    if(!log||!log->client||(!log->fd_mode&&!*log->path)||!level||!event||!detail||strlen(level)>16||strlen(event)>64||strlen(detail)>4096)return -1;
    /* Best-effort recovery flush: try to drain pending records before new work,
     * but do not block the session if the relay is temporarily unreachable. */
    if(log->recovery_pending&&!strcmp(event,"operation_begin"))muse_log_flush(log);
    struct stat st;if(!(log->fd_mode?fstat(log->spool_fd,&st):stat(log->path,&st))&&st.st_size>7*1024*1024){
        if(log->local_only&&!log->fd_mode){char previous[1060];if(snprintf(previous,sizeof previous,"%s.previous",log->path)>=(int)sizeof previous||rename(log->path,previous)||sync_parent(log->path))return -1;log->pending=0;}
        else if(muse_log_flush(log))return -1;
    }
    if(append_record(log,level,event,detail))return -1;
    return log->recovery_pending||!strcmp(level,"error")||!strcmp(event,"crash")||!strcmp(event,"session_end")||log->pending>=16?muse_log_flush(log):0;
}
void muse_log_close(struct muse_log *log){
    if(!log||!log->client||(!log->fd_mode&&!*log->path))return;
    /* Entering Local suppresses HTTP; it is not an acknowledgement of the
     * old Hybrid journal. Retain that marker for a later Hybrid recovery. */
    if(!log->local_only&&muse_config_local(&log->client->config))return;
    /* Never erase the unacknowledged-session marker on a failed POST. */
    if(muse_log_flush(log))return;
    if(log->fd_mode){(void)set_marker(log->marker_fd,0);return;}
    char marker[1040];snprintf(marker,sizeof marker,"%s.active",log->path);
    if(!unlink(marker))(void)sync_parent(marker);
}

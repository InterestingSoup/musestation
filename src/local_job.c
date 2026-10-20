/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "local_job.h"
#include "json.h"
#include "client_sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECKPOINT_MAX 65536u
static int fail(struct muse_local_store *s,const char *why){snprintf(s->error,sizeof(s->error),"%s",why);return -1;}
static int hex(const char *p,size_t n){if(!p||strlen(p)!=n)return 0;for(size_t i=0;i<n;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return 1;}
static int path_valid(const char *p){if(!p||*p!='/'||strlen(p)>900)return 0;for(const char *at=p;*at;){while(*at=='/')at++;const char *a=at;while(*at&&*at!='/'){if((unsigned char)*at<32)return 0;at++;}if((at-a==1&&a[0]=='.')||(at-a==2&&!memcmp(a,"..",2)))return 0;}return 1;}
static int mkdirs(const char *p){char b[1024];if(!path_valid(p))return -1;strcpy(b,p);for(char *a=b+1;;a++){if(*a!='/'&&*a)continue;char c=*a;*a=0;struct stat st;if((mkdir(b,0700)&&errno!=EEXIST)||lstat(b,&st)||!S_ISDIR(st.st_mode))return -1;*a=c;if(!c)break;}return 0;}
static int write_all(int fd,const void *bytes,size_t n){const char *p=bytes;while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;}
static int read_file(const char *path,char *out,size_t capacity,size_t *length){int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;struct stat st;int bad=fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>=capacity;size_t at=0,n=bad?0:(size_t)st.st_size;while(!bad&&at<n){ssize_t k=read(fd,out+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0){bad=1;break;}at+=(size_t)k;}close(fd);if(bad)return -1;out[n]=0;*length=n;return 0;}
static int sync_dir(const char *path){int fd=open(path,O_RDONLY|O_DIRECTORY);if(fd<0)return -1;int rc=fsync(fd);int e=errno;close(fd);/* Some native filesystems do not implement directory fsync. File data is still fsynced. */return rc&&(e!=EINVAL&&e!=ENOTSUP)?-1:0;}
static int job_path(struct muse_local_store *s,const char *id,const char *name,char *out,size_t cap){if(!hex(id,32)||snprintf(out,cap,"%s/%s/%s",s->root,id,name)>=(int)cap)return fail(s,"Invalid local project path");return 0;}
const char *muse_local_state_name(enum muse_local_state state){static const char *names[]={"awaiting approval","queued","generating","validating","ready","paused","interrupted","failed","cancelled"};return state>=MUSE_LOCAL_AWAITING_APPROVAL&&state<=MUSE_LOCAL_CANCELLED?names[state]:"invalid";}
static int valid(const struct muse_local_job *j){
    return hex(j->id,32)&&hex(j->revision,64)&&(!*j->approved_revision||hex(j->approved_revision,64))&&
      (!*j->candidate_sha||hex(j->candidate_sha,64))&&(!*j->validated_sha||hex(j->validated_sha,64))&&
      j->state>=MUSE_LOCAL_AWAITING_APPROVAL&&j->state<=MUSE_LOCAL_CANCELLED&&j->next_phase>=MUSE_LOCAL_GENERATE&&j->next_phase<=MUSE_LOCAL_VALIDATE&&
      j->max_attempts>=1&&j->max_attempts<=MUSE_LOCAL_ATTEMPTS_MAX&&j->attempts<=j->max_attempts&&j->max_seconds>=1&&j->max_seconds<=604800&&
      j->seconds_used<=j->max_seconds&&j->reserved_seconds<=1800&&j->reserved_seconds<=j->seconds_used&&
      *j->request&&*j->plan&&*j->title;
}
static void plan_hash(const struct muse_local_job *j,char out[65]){struct muse_sha256 h;muse_sha256_init(&h);muse_sha256_update(&h,j->request,strlen(j->request)+1);muse_sha256_update(&h,j->plan,strlen(j->plan)+1);char budgets[80];int n=snprintf(budgets,sizeof budgets,"%u:%u",j->max_attempts,j->max_seconds);muse_sha256_update(&h,budgets,(size_t)n);muse_sha256_final(&h,out);}
static int encode(const struct muse_local_job *j,char *out,size_t capacity){
    size_t used=0;int n=snprintf(out,capacity,"{\"schema\":1");if(n<0)return -1;used=(size_t)n;char quoted[24584];
#define TEXT(field) do{if(muse_json_quote(j->field,quoted,sizeof quoted))return -1;n=snprintf(out+used,capacity-used,",\"" #field "\":%s",quoted);if(n<0||(size_t)n>=capacity-used)return -1;used+=(size_t)n;}while(0)
    TEXT(id);TEXT(title);TEXT(request);TEXT(plan);TEXT(revision);TEXT(approved_revision);TEXT(candidate_sha);TEXT(validated_sha);TEXT(detail);
#undef TEXT
    n=snprintf(out+used,capacity-used,",\"state\":%u,\"next_phase\":%u,\"sequence\":%llu,\"created_at\":%llu,\"updated_at\":%llu,\"attempts\":%u,\"max_attempts\":%u,\"seconds_used\":%u,\"max_seconds\":%u,\"reserved_seconds\":%u}",
      (unsigned)j->state,(unsigned)j->next_phase,(unsigned long long)j->sequence,(unsigned long long)j->created_at,(unsigned long long)j->updated_at,j->attempts,j->max_attempts,j->seconds_used,j->max_seconds,j->reserved_seconds);
    return n<0||(size_t)n>=capacity-used?-1:(int)(used+(size_t)n);
}
static int decode(const char *data,size_t n,struct muse_local_job *out){
    struct muse_json json;if(muse_json_parse(&json,data,n))return -1;int p=muse_json_get(&json,0,"payload"),sum=muse_json_get(&json,0,"sha256");char sha[65],actual[65];
    if(p<0||json.tokens[p].type!='{'||muse_json_string(&json,sum,sha,sizeof sha)||!hex(sha,64))return -1;
    muse_sha256_hex(data+json.tokens[p].start,json.tokens[p].end-json.tokens[p].start,actual);if(strcmp(sha,actual))return -1;
    memset(out,0,sizeof(*out));uint64_t x;
#define TEXT(field) if(muse_json_string(&json,muse_json_get(&json,p,#field),out->field,sizeof(out->field)))return -1
    TEXT(id);TEXT(title);TEXT(request);TEXT(plan);TEXT(revision);TEXT(approved_revision);TEXT(candidate_sha);TEXT(validated_sha);TEXT(detail);
#undef TEXT
#define NUM(field,limit) do{if(muse_json_u64(&json,muse_json_get(&json,p,#field),&x)||x>(limit))return -1;out->field=x;}while(0)
    NUM(state,MUSE_LOCAL_CANCELLED);NUM(next_phase,MUSE_LOCAL_VALIDATE);NUM(sequence,UINT64_MAX);NUM(created_at,UINT64_MAX);NUM(updated_at,UINT64_MAX);NUM(attempts,MUSE_LOCAL_ATTEMPTS_MAX);NUM(max_attempts,MUSE_LOCAL_ATTEMPTS_MAX);NUM(seconds_used,604800);NUM(max_seconds,604800);NUM(reserved_seconds,1800);
#undef NUM
    if(muse_json_u64(&json,muse_json_get(&json,p,"schema"),&x)||x!=1||!valid(out))return -1;
    plan_hash(out,actual);if(strcmp(actual,out->revision)||(*out->approved_revision&&strcmp(out->approved_revision,out->revision)))return -1;
    return 0;
}
/* Only store-owned immutable revisions and mkstemp leftovers are eligible.
 * Both checksum-valid slots protect their candidate AND validated sources. */
static int source_name(const char *name,char sha[65]){
    if(strlen(name)!=75||strncmp(name,"source-",7)||strcmp(name+71,".lua"))return 0;
    memcpy(sha,name+7,64);sha[64]=0;return hex(sha,64);
}
static int temporary_name(const char *name){
    const char *suffix=NULL;char original[76],sha[65];size_t length=strlen(name);
    if(length==86){memcpy(original,name,75);original[75]=0;if(source_name(original,sha)&&!strncmp(name+75,".tmp-",5))suffix=name+80;}
    const char *prefixes[]={"state-a.json.tmp-","state-b.json.tmp-"};
    for(unsigned i=0;i<2;i++){size_t prefix=strlen(prefixes[i]);if(length==prefix+6&&!strncmp(name,prefixes[i],prefix))suffix=name+prefix;}
    if(!suffix)return 0;for(unsigned i=0;i<6;i++)if(!((suffix[i]>='0'&&suffix[i]<='9')||(suffix[i]>='a'&&suffix[i]<='z')||(suffix[i]>='A'&&suffix[i]<='Z')))return 0;return 1;
}
static int prune_sources(struct muse_local_store *s,const char *id){
    char *data=malloc(CHECKPOINT_MAX+1);if(!data)return fail(s,"Source retention allocation failed");
    char keep[4][65]={{0}};unsigned count=0,valid_slots=0;char path[1200];
    for(unsigned slot=0;slot<2;slot++){
      struct muse_local_job j;size_t n;
      if(job_path(s,id,slot?"state-b.json":"state-a.json",path,sizeof path)){free(data);return -1;}
      if(!read_file(path,data,CHECKPOINT_MAX+1,&n)&&!decode(data,n,&j)&&!strcmp(j.id,id)){
        valid_slots++;if(*j.candidate_sha)strcpy(keep[count++],j.candidate_sha);
        if(*j.validated_sha)strcpy(keep[count++],j.validated_sha);
      }
    }
    free(data);if(!valid_slots)return fail(s,"Source cleanup refused: no valid recovery checkpoint");
    char directory[1200];struct stat st;
    if(job_path(s,id,"",directory,sizeof directory))return -1;
    directory[strlen(directory)-1]=0;
    if(lstat(directory,&st)||!S_ISDIR(st.st_mode))return fail(s,"Source retention directory unavailable");
    DIR *dir=opendir(directory);if(!dir)return fail(s,"Cannot inspect retained source revisions");
    struct dirent *entry;int bad=0,changed=0;
    for(;;){
      errno=0;entry=readdir(dir);if(!entry){if(errno)bad=1;break;}
      char sha[65];int remove=temporary_name(entry->d_name);
      if(source_name(entry->d_name,sha)){remove=1;for(unsigned i=0;i<count;i++)if(!strcmp(keep[i],sha))remove=0;}
      if(remove){if(job_path(s,id,entry->d_name,path,sizeof path)||unlink(path)){bad=1;break;}changed=1;}
    }
    closedir(dir);
    if(!bad){
      dir=opendir(directory);if(!dir)bad=1;
      else {for(;;){errno=0;entry=readdir(dir);if(!entry){if(errno)bad=1;break;}
        char sha[65];int obsolete=temporary_name(entry->d_name);
        if(source_name(entry->d_name,sha)){obsolete=1;for(unsigned i=0;i<count;i++)if(!strcmp(keep[i],sha))obsolete=0;}
        if(obsolete){bad=1;break;}
      }closedir(dir);}
    }
    if(!bad&&changed)bad=sync_dir(directory);
    return bad?fail(s,"Cannot prune obsolete local source files; free/check project storage before resuming"):0;
}

static int checkpoint(struct muse_local_store *s,struct muse_local_job *j){
    if(!valid(j)||j->sequence==UINT64_MAX)return fail(s,"Invalid local job checkpoint");
    struct muse_local_job next=*j;next.sequence++;time_t now=time(NULL);next.updated_at=now>0?(uint64_t)now:0;
    char *body=malloc(CHECKPOINT_MAX),*envelope=malloc(CHECKPOINT_MAX);if(!body||!envelope){free(body);free(envelope);return fail(s,"Local checkpoint allocation failed");}
    int size=encode(&next,body,CHECKPOINT_MAX);struct muse_json parsed;char sha[65];int bad=size<0||size>65000||muse_json_parse(&parsed,body,size>0?(size_t)size:0);
    if(!bad)muse_sha256_hex(body,(size_t)size,sha);int n=bad?-1:snprintf(envelope,CHECKPOINT_MAX,"{\"sha256\":\"%s\",\"payload\":%s}\n",sha,body);free(body);
    char dir[1200],dest[1200],temp[1230];bad|=n<0||n>=(int)CHECKPOINT_MAX||job_path(s,j->id,"",dir,sizeof dir)||job_path(s,j->id,next.sequence%2?"state-a.json":"state-b.json",dest,sizeof dest);
    if(!bad){snprintf(temp,sizeof temp,"%s.tmp-XXXXXX",dest);int fd=mkstemp(temp);bad=fd<0;if(!bad){bad=fchmod(fd,0600)||write_all(fd,envelope,(size_t)n)||fsync(fd);if(close(fd))bad=1;if(!bad)bad=rename(temp,dest)||sync_dir(dir);if(bad)unlink(temp);}}
    free(envelope);if(bad)return fail(s,"Cannot commit local job checkpoint; previous checkpoint retained");*j=next;
    /* A committed transition remains successful even if cleanup is temporarily
     * unavailable. Next load/phase/source write requires successful pruning. */
    (void)prune_sources(s,j->id);return 0;
}
int muse_local_open(struct muse_local_store *s,const char *root){memset(s,0,sizeof(*s));s->lock_fd=-1;if(!path_valid(root)||mkdirs(root))return fail(s,"Cannot open local projects directory");snprintf(s->root,sizeof s->root,"%s",root);char lock[1100];snprintf(lock,sizeof lock,"%s/.lock",root);s->lock_fd=open(lock,O_RDWR|O_CREAT|O_NOFOLLOW,0600);struct flock f={.l_type=F_WRLCK,.l_whence=SEEK_SET};if(s->lock_fd<0||fcntl(s->lock_fd,F_SETLK,&f)){muse_local_close(s);return fail(s,"Local project store is already in use or unavailable");}return 0;}
void muse_local_close(struct muse_local_store *s){if(s&&s->lock_fd>=0){close(s->lock_fd);s->lock_fd=-1;}}
int muse_local_load(struct muse_local_store *s,const char *id,struct muse_local_job *out,int recover){
    char *data=malloc(CHECKPOINT_MAX+1);if(!data)return fail(s,"Checkpoint allocation failed");struct muse_local_job best={0};int found=0;
    for(int slot=0;slot<2;slot++){char path[1200];size_t n;struct muse_local_job candidate;if(job_path(s,id,slot?"state-b.json":"state-a.json",path,sizeof path))continue;if(!read_file(path,data,CHECKPOINT_MAX+1,&n)&&!decode(data,n,&candidate)&&!strcmp(candidate.id,id)&&(!found||candidate.sequence>best.sequence)){best=candidate;found=1;}}
    free(data);if(!found)return fail(s,"No valid checkpoint for this local project");
    if(recover&&(best.state==MUSE_LOCAL_QUEUED||best.state==MUSE_LOCAL_GENERATING||best.state==MUSE_LOCAL_VALIDATING)){best.state=MUSE_LOCAL_INTERRUPTED;best.reserved_seconds=0;snprintf(best.detail,sizeof best.detail,"Previous execution was interrupted. Reserved step time remains charged; resume explicitly.");if(checkpoint(s,&best))return -1;}
    if(prune_sources(s,id))return -1;*out=best;return 0;
}
int muse_local_list(struct muse_local_store *s,struct muse_local_job *jobs,size_t cap,size_t *count){
    *count=0;DIR *dir=opendir(s->root);if(!dir)return fail(s,"Cannot list local projects");struct dirent *e;
    while((e=readdir(dir))){if(!hex(e->d_name,32))continue;if(*count>=cap){closedir(dir);return fail(s,"Local project list capacity exceeded");}struct muse_local_job j;if(!muse_local_load(s,e->d_name,&j,0))jobs[(*count)++]=j;}
    closedir(dir);for(size_t i=0;i<*count;i++)for(size_t k=i+1;k<*count;k++)if(jobs[k].updated_at>jobs[i].updated_at){struct muse_local_job t=jobs[i];jobs[i]=jobs[k];jobs[k]=t;}s->error[0]=0;return 0;
}
int muse_local_create(struct muse_local_store *s,const char *title,const char *request,const char *plan,unsigned attempts,unsigned seconds,struct muse_local_job *out){
    if(!title||!*title||strlen(title)>=sizeof out->title||!request||!*request||strlen(request)>=sizeof out->request||!plan||!*plan||strlen(plan)>=sizeof out->plan||!attempts||attempts>MUSE_LOCAL_ATTEMPTS_MAX||!seconds||seconds>604800)return fail(s,"Local request, plan or budget exceeds its limit");
    DIR *scan=opendir(s->root);if(!scan)return fail(s,"Cannot inspect local project capacity");unsigned count=0;struct dirent *e;while((e=readdir(scan)))if(hex(e->d_name,32))count++;closedir(scan);if(count>=MUSE_LOCAL_JOBS_MAX)return fail(s,"Local project limit reached (64); preserve or remove older projects before creating more");
    struct muse_local_job j={0};unsigned char random[16];int fd=open("/dev/urandom",O_RDONLY);if(fd<0)return fail(s,"Local project identity unavailable");ssize_t n=read(fd,random,sizeof random);close(fd);if(n!=(ssize_t)sizeof random)return fail(s,"Local project entropy read failed");for(unsigned i=0;i<16;i++)snprintf(j.id+2*i,3,"%02x",random[i]);
    strcpy(j.title,title);strcpy(j.request,request);strcpy(j.plan,plan);j.max_attempts=attempts;j.max_seconds=seconds;j.state=MUSE_LOCAL_AWAITING_APPROVAL;j.next_phase=MUSE_LOCAL_GENERATE;time_t now=time(NULL);j.created_at=now>0?(uint64_t)now:0;plan_hash(&j,j.revision);snprintf(j.detail,sizeof j.detail,"Review this saved plan before approving local work.");
    char dir[1200];if(job_path(s,j.id,"",dir,sizeof dir)||mkdir(dir,0700))return fail(s,"Cannot create local project directory");if(checkpoint(s,&j))return -1;*out=j;return 0;
}
int muse_local_approve(struct muse_local_store *s,struct muse_local_job *j,const char *revision){if(j->state!=MUSE_LOCAL_AWAITING_APPROVAL||!revision||strcmp(revision,j->revision))return fail(s,"Approval must match the displayed saved plan revision");struct muse_local_job next=*j;strcpy(next.approved_revision,next.revision);next.state=MUSE_LOCAL_QUEUED;strcpy(next.detail,"Approved local work queued; the finished app will require an explicit Run.");if(checkpoint(s,&next))return -1;*j=next;return 0;}
int muse_local_resume(struct muse_local_store *s,struct muse_local_job *j){if((j->state!=MUSE_LOCAL_PAUSED&&j->state!=MUSE_LOCAL_INTERRUPTED)||strcmp(j->approved_revision,j->revision))return fail(s,"Only an explicitly approved paused/interrupted job can resume");if(j->seconds_used>=j->max_seconds||(j->next_phase==MUSE_LOCAL_GENERATE&&j->attempts>=j->max_attempts))return fail(s,"Local job budget exhausted; review a new plan to authorize more work");struct muse_local_job next=*j;next.state=MUSE_LOCAL_QUEUED;strcpy(next.detail,"Explicitly resumed from the last saved step.");if(checkpoint(s,&next))return -1;*j=next;return 0;}
int muse_local_pause(struct muse_local_store *s,struct muse_local_job *j,int cancel){if(j->state==MUSE_LOCAL_GENERATING||j->state==MUSE_LOCAL_VALIDATING)return fail(s,"Active step must observe the cooperative stop request first");if(j->state==MUSE_LOCAL_READY||j->state==MUSE_LOCAL_CANCELLED)return fail(s,"This job is no longer running");struct muse_local_job next=*j;next.state=cancel?MUSE_LOCAL_CANCELLED:MUSE_LOCAL_PAUSED;snprintf(next.detail,sizeof next.detail,"%s explicitly; latest saved source retained.",cancel?"Cancelled":"Paused");if(checkpoint(s,&next))return -1;*j=next;return 0;}
int muse_local_begin_step(struct muse_local_store *s,struct muse_local_job *j,unsigned requested,unsigned *allowed){if(prune_sources(s,j->id))return -1;if(j->state!=MUSE_LOCAL_QUEUED||strcmp(j->approved_revision,j->revision)||!requested||requested>1800)return fail(s,"Local step lacks approval or has an invalid deadline");if(j->seconds_used>=j->max_seconds||(j->next_phase==MUSE_LOCAL_GENERATE&&j->attempts>=j->max_attempts))return fail(s,"Local job budget exhausted");struct muse_local_job next=*j;unsigned remain=next.max_seconds-next.seconds_used;*allowed=requested<remain?requested:remain;next.reserved_seconds=*allowed;next.seconds_used+=*allowed;next.state=next.next_phase==MUSE_LOCAL_GENERATE?MUSE_LOCAL_GENERATING:MUSE_LOCAL_VALIDATING;if(next.next_phase==MUSE_LOCAL_GENERATE)next.attempts++;strcpy(next.detail,"Bounded step is running; its budget reservation is durable.");if(checkpoint(s,&next))return -1;*j=next;return 0;}
static int store_source(struct muse_local_store *s,struct muse_local_job *j,const char *data,size_t n){if(prune_sources(s,j->id))return -1;if(!data||!n||n>MUSE_LOCAL_SOURCE_MAX||memchr(data,0,n))return fail(s,"Local source is empty, binary or over64KiB");char sha[65];muse_sha256_hex(data,n,sha);char name[90],dest[1200],temp[1240];snprintf(name,sizeof name,"source-%s.lua",sha);if(job_path(s,j->id,name,dest,sizeof dest))return -1;
    struct stat existing_stat;
    if(!lstat(dest,&existing_stat)){char *existing=malloc(MUSE_LOCAL_SOURCE_MAX+1);size_t len;int bad=!existing||read_file(dest,existing,MUSE_LOCAL_SOURCE_MAX+1,&len)||len!=n||memcmp(existing,data,n);free(existing);if(bad)return fail(s,"Existing source revision failed integrity check");}
    else if(errno==ENOENT){snprintf(temp,sizeof temp,"%s.tmp-XXXXXX",dest);int fd=mkstemp(temp);if(fd<0)return fail(s,"Cannot create source revision temporary file");int bad=fchmod(fd,0600)||write_all(fd,data,n)||fsync(fd);if(close(fd))bad=1;if(!bad)bad=rename(temp,dest);if(bad){unlink(temp);return fail(s,"Cannot commit source revision");}}
    else return fail(s,"Cannot inspect source revision");
    if(job_path(s,j->id,"",temp,sizeof temp)||sync_dir(temp))return fail(s,"Cannot synchronize source revision directory");strcpy(j->candidate_sha,sha);return 0;
}
int muse_local_checkpoint_source(struct muse_local_store *s,struct muse_local_job *j,const char *source,size_t length){if(j->state!=MUSE_LOCAL_GENERATING||strcmp(j->approved_revision,j->revision))return fail(s,"Only approved active generation can checkpoint partial source");struct muse_local_job next=*j;if(store_source(s,&next,source,length)||checkpoint(s,&next))return -1;*j=next;return 0;}
int muse_local_finish_step(struct muse_local_store *s,struct muse_local_job *j,enum muse_local_state state,enum muse_local_phase phase,const char *source,size_t length,const char *detail,uint64_t elapsed){
    if((j->state!=MUSE_LOCAL_GENERATING&&j->state!=MUSE_LOCAL_VALIDATING)||!detail||strlen(detail)>=sizeof j->detail||phase<MUSE_LOCAL_GENERATE||phase>MUSE_LOCAL_VALIDATE||
       (state!=MUSE_LOCAL_QUEUED&&state!=MUSE_LOCAL_READY&&state!=MUSE_LOCAL_PAUSED&&state!=MUSE_LOCAL_FAILED&&state!=MUSE_LOCAL_CANCELLED)||
       (state==MUSE_LOCAL_READY&&(j->state!=MUSE_LOCAL_VALIDATING||!*j->candidate_sha||source)))return fail(s,"Invalid local step transition");
    struct muse_local_job next=*j;if(source&&store_source(s,&next,source,length))return -1;
    uint64_t used=elapsed/1000+(elapsed%1000!=0);if(used>next.reserved_seconds)used=next.reserved_seconds;next.seconds_used-=next.reserved_seconds-(uint32_t)used;next.reserved_seconds=0;next.state=state;next.next_phase=phase;strcpy(next.detail,detail);
    if(state==MUSE_LOCAL_READY)strcpy(next.validated_sha,next.candidate_sha);
    if(state==MUSE_LOCAL_QUEUED&&(next.seconds_used>=next.max_seconds||(phase==MUSE_LOCAL_GENERATE&&next.attempts>=next.max_attempts))){next.state=MUSE_LOCAL_FAILED;strcpy(next.detail,"Local budget exhausted; saved candidate retained for review.");}
    if(checkpoint(s,&next))return -1;*j=next;return 0;
}
int muse_local_source_path(struct muse_local_store *s,const struct muse_local_job *j,int validated,char *out,size_t cap){const char *sha=validated?j->validated_sha:j->candidate_sha;if(!hex(sha,64)||(validated&&(j->state!=MUSE_LOCAL_READY||strcmp(sha,j->candidate_sha))))return fail(s,"No verified local source is ready");char name[90];snprintf(name,sizeof name,"source-%s.lua",sha);return job_path(s,j->id,name,out,cap);}
int muse_local_read_source(struct muse_local_store *s,const struct muse_local_job *j,int validated,char *source,size_t cap,size_t *n){char path[1200];if(cap<MUSE_LOCAL_SOURCE_MAX+1||muse_local_source_path(s,j,validated,path,sizeof path)||read_file(path,source,cap,n)||!*n||*n>MUSE_LOCAL_SOURCE_MAX||memchr(source,0,*n))return fail(s,"Cannot read bounded local source revision");char sha[65];muse_sha256_hex(source,*n,sha);return strcmp(sha,validated?j->validated_sha:j->candidate_sha)?fail(s,"Local source revision hash mismatch"):0;}

#define _POSIX_C_SOURCE 200809L
#include "local_job.h"
#include "config.h"
#include "client.h"
#include "log.h"
#include <assert.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    assert(argc==2);char root[1024];snprintf(root,sizeof root,"%s/projects",argv[1]);struct muse_local_store s;assert(!muse_local_open(&s,root));
    struct muse_local_job j;assert(!muse_local_create(&s,"Clock","Make a clock","Draw a clock. Use the pad.",4,240,&j));
    assert(j.state==MUSE_LOCAL_AWAITING_APPROVAL);assert(muse_local_resume(&s,&j));assert(muse_local_approve(&s,&j,"wrong"));
    assert(!muse_local_approve(&s,&j,j.revision));unsigned allowed;
    assert(!muse_local_begin_step(&s,&j,120,&allowed)&&allowed==120&&j.seconds_used==120&&j.attempts==1);
    assert(!muse_local_checkpoint_source(&s,&j,"-- durable partial",18));
    assert(j.state==MUSE_LOCAL_GENERATING&&j.reserved_seconds==120&&j.candidate_sha[0]);
    char id[33];strcpy(id,j.id);muse_local_close(&s);assert(!muse_local_open(&s,root));
    assert(!muse_local_load(&s,id,&j,1)&&j.state==MUSE_LOCAL_INTERRUPTED&&j.seconds_used==120&&j.candidate_sha[0]);
    assert(muse_local_begin_step(&s,&j,120,&allowed));assert(!muse_local_resume(&s,&j));assert(!muse_local_begin_step(&s,&j,120,&allowed));
    const char source[]="function update(dt) end\nfunction draw() end\n";
    assert(!muse_local_finish_step(&s,&j,MUSE_LOCAL_QUEUED,MUSE_LOCAL_VALIDATE,source,strlen(source),"Generated candidate",1500));
    assert(j.seconds_used==122&&j.attempts==2&&!j.validated_sha[0]);char bytes[MUSE_LOCAL_SOURCE_MAX+1];size_t n;
    assert(muse_local_read_source(&s,&j,1,bytes,sizeof bytes,&n));assert(!muse_local_read_source(&s,&j,0,bytes,sizeof bytes,&n)&&n==strlen(source)&&!strcmp(bytes,source));
    assert(!muse_local_begin_step(&s,&j,10,&allowed));assert(!muse_local_finish_step(&s,&j,MUSE_LOCAL_READY,MUSE_LOCAL_VALIDATE,NULL,0,"Validated; explicit Run required",50));
    assert(!muse_local_read_source(&s,&j,1,bytes,sizeof bytes,&n));assert(muse_local_resume(&s,&j));
    char path[1200];assert(!muse_local_source_path(&s,&j,1,path,sizeof path));int fd=open(path,O_WRONLY);assert(fd>=0&&write(fd,"!",1)==1);close(fd);assert(muse_local_read_source(&s,&j,1,bytes,sizeof bytes,&n));
    struct muse_local_job budget;assert(!muse_local_create(&s,"Budget","Make a game","One bounded attempt",1,1,&budget));assert(!muse_local_approve(&s,&budget,budget.revision));assert(!muse_local_begin_step(&s,&budget,1800,&allowed)&&allowed==1);
    assert(!muse_local_finish_step(&s,&budget,MUSE_LOCAL_QUEUED,MUSE_LOCAL_GENERATE,NULL,0,"Retry",1000));assert(budget.state==MUSE_LOCAL_FAILED&&budget.seconds_used==1&&budget.attempts==1);
    struct muse_local_job pause;assert(!muse_local_create(&s,"Pause","Make a timer","Bounded timer",3,300,&pause));assert(!muse_local_approve(&s,&pause,pause.revision));assert(!muse_local_pause(&s,&pause,0));assert(!muse_local_resume(&s,&pause));assert(!muse_local_pause(&s,&pause,1));assert(pause.state==MUSE_LOCAL_CANCELLED&&muse_local_resume(&s,&pause));
    /* A torn latest checkpoint falls back to the previous checksum-valid slot. */
    snprintf(path,sizeof path,"%s/%s/state-%c.json",root,pause.id,pause.sequence%2?'a':'b');fd=open(path,O_WRONLY|O_TRUNC);assert(fd>=0&&write(fd,"{broken",7)==7);close(fd);struct muse_local_job recovered;assert(!muse_local_load(&s,pause.id,&recovered,1));assert(recovered.state==MUSE_LOCAL_INTERRUPTED);
    struct muse_local_job *list=calloc(MUSE_LOCAL_JOBS_MAX,sizeof(*list));size_t count;assert(list&&!muse_local_list(&s,list,MUSE_LOCAL_JOBS_MAX,&count)&&count==3);free(list);
    /* Retention preserves both crash-recovery slots, then removes only obsolete
     * hash-named source files and our abandoned atomic-write temporaries. */
    struct muse_local_job retained;assert(!muse_local_create(&s,"Retention","Make a timer","Keep source checkpoints bounded",4,400,&retained));
    assert(!muse_local_approve(&s,&retained,retained.revision));assert(!muse_local_begin_step(&s,&retained,120,&allowed));
    assert(!muse_local_checkpoint_source(&s,&retained,"-- revision one",15));char first[1200],second[1200];assert(!muse_local_source_path(&s,&retained,0,first,sizeof first));
    assert(!muse_local_checkpoint_source(&s,&retained,"-- revision two",15));assert(!muse_local_source_path(&s,&retained,0,second,sizeof second));assert(!access(first,F_OK)&&!access(second,F_OK));
    assert(!muse_local_checkpoint_source(&s,&retained,"-- revision three",17));assert(access(first,F_OK)&&!access(second,F_OK));
    char obstruction[1200];snprintf(obstruction,sizeof obstruction,"%s/%s/source-%064d.lua",root,retained.id,0);assert(!mkdir(obstruction,0700));
    uint64_t saved_sequence=retained.sequence;assert(muse_local_checkpoint_source(&s,&retained,"-- blocked new source",21));assert(retained.sequence==saved_sequence);assert(!rmdir(obstruction));
    snprintf(path,sizeof path,"%s/%s/state-%c.json",root,retained.id,retained.sequence%2?'a':'b');fd=open(path,O_WRONLY|O_TRUNC);assert(fd>=0&&write(fd,"bad",3)==3);close(fd);
    assert(!muse_local_load(&s,retained.id,&retained,1)&&retained.state==MUSE_LOCAL_INTERRUPTED);assert(!access(second,F_OK));assert(!muse_local_read_source(&s,&retained,0,bytes,sizeof bytes,&n)&&!strcmp(bytes,"-- revision two"));
    assert(!muse_local_resume(&s,&retained));assert(!muse_local_begin_step(&s,&retained,120,&allowed));
    for(unsigned i=0;i<48;i++){int prefix=snprintf(bytes,sizeof bytes,"-- large partial %u\n",i);memset(bytes+prefix,'x',MUSE_LOCAL_SOURCE_MAX-(size_t)prefix);assert(!muse_local_checkpoint_source(&s,&retained,bytes,MUSE_LOCAL_SOURCE_MAX));}
    char directory[1200];snprintf(directory,sizeof directory,"%s/%s",root,retained.id);DIR *scan=opendir(directory);assert(scan);struct dirent *entry;unsigned sources=0;while((entry=readdir(scan)))if(!strncmp(entry->d_name,"source-",7)&&strlen(entry->d_name)==75)sources++;closedir(scan);assert(sources<=4);
    assert(!muse_local_finish_step(&s,&retained,MUSE_LOCAL_QUEUED,MUSE_LOCAL_VALIDATE,source,strlen(source),"Final candidate",1000));assert(!muse_local_begin_step(&s,&retained,10,&allowed));assert(!muse_local_finish_step(&s,&retained,MUSE_LOCAL_READY,MUSE_LOCAL_VALIDATE,NULL,0,"Validation completed",1));
    char ready_source[1200];assert(!muse_local_source_path(&s,&retained,1,ready_source,sizeof ready_source));
    char statefile[1240];snprintf(statefile,sizeof statefile,"%s.state",ready_source);fd=open(statefile,O_WRONLY|O_CREAT,0600);assert(fd>=0);close(fd);
    snprintf(path,sizeof path,"%s/state-b.json.tmp-Abc123",directory);fd=open(path,O_WRONLY|O_CREAT,0600);assert(fd>=0);close(fd);
    char source_temp[1240];snprintf(source_temp,sizeof source_temp,"%s.tmp-Abc123",first);fd=open(source_temp,O_WRONLY|O_CREAT,0600);assert(fd>=0);close(fd);
    assert(!muse_local_load(&s,retained.id,&retained,1));assert(access(path,F_OK)&&access(source_temp,F_OK)&&!access(ready_source,F_OK)&&!access(statefile,F_OK));
    assert(!muse_local_read_source(&s,&retained,1,bytes,sizeof bytes,&n)&&!strcmp(bytes,source));

    struct muse_local_job days;assert(muse_local_create(&s,"Too many","Make a game","Bounded attempts",513,604800,&days));
    assert(!muse_local_create(&s,"Days","Make a game","Persist a long approved attempt budget",512,2000,&days));assert(!muse_local_approve(&s,&days,days.revision));
    char days_id[33];strcpy(days_id,days.id);
    for(unsigned attempt=0;attempt<512;attempt++){
      if(attempt==256){muse_local_close(&s);assert(!muse_local_open(&s,root));assert(!muse_local_load(&s,days_id,&days,1));assert(days.max_attempts==512&&days.attempts==256&&days.seconds_used==256);assert(!muse_local_resume(&s,&days));}
      assert(!muse_local_begin_step(&s,&days,1,&allowed));assert(!muse_local_finish_step(&s,&days,MUSE_LOCAL_QUEUED,MUSE_LOCAL_GENERATE,NULL,0,"Bounded retry fixture",1000));
    }
    assert(days.state==MUSE_LOCAL_FAILED&&days.attempts==512&&days.seconds_used==512&&muse_local_resume(&s,&days));
    muse_local_close(&s);
    struct muse_config c;muse_config_defaults(&c);assert(!muse_config_local(&c)&&!strcmp(c.hybrid_provider,"muse")&&!c.phone_port);strcpy(c.build_mode,"local");snprintf(c.log_dir,sizeof c.log_dir,"%s/logs",argv[1]);snprintf(c.artifact_dir,sizeof c.artifact_dir,"%s/artifacts",argv[1]);snprintf(c.model_dir,sizeof c.model_dir,"%s/models",argv[1]);char error[256];assert(!muse_config_validate(&c,error,sizeof error));
    snprintf(path,sizeof path,"%s/config.json",argv[1]);assert(!muse_config_save(path,&c,error,sizeof error));struct muse_config loaded;assert(!muse_config_load(path,&loaded,error,sizeof error)&&muse_config_local(&loaded));
    snprintf(path,sizeof path,"%s/legacy.json",argv[1]);FILE *legacy=fopen(path,"wb");assert(legacy);
    fprintf(legacy,"{\"schema_version\":1,\"console_ip\":\"127.0.0.1\",\"server_url\":\"http://127.0.0.1:1\",\"ps5lm_url\":\"\",\"speech_url\":\"\",\"log_dir\":\"/tmp/logs\",\"artifact_dir\":\"/tmp/artifacts\",\"model_dir\":\"/tmp/models\",\"audio_in_module\":\"/tmp/AudioIn.sprx\",\"firmware_reported\":\"12.40\"}");assert(!fclose(legacy));
    assert(muse_config_load(path,&loaded,error,sizeof error)&&strstr(error,"relay console token"));
    strcpy(c.server_url,"http://127.0.0.1:1");struct muse_client client;muse_client_init(&client,&c);unsigned char *response;size_t length;
    assert(muse_client_http(&client,c.server_url,"GET","/forbidden","text/plain",NULL,0,1024,&response,&length)&&strstr(client.error,"Local mode"));assert(!response&&!length);
    struct muse_log log;assert(!muse_log_open(&log,&client));assert(!muse_log_event(&log,"error","offline","This remains private"));assert(!muse_log_flush(&log));
    fd=open(log.path,O_WRONLY);assert(fd>=0&&!ftruncate(fd,7*1024*1024+1));close(fd);assert(!muse_log_event(&log,"info","rotation","Bounded private diagnostics"));
    char previous[1100];snprintf(previous,sizeof previous,"%s.previous",log.path);struct stat st;assert(!stat(previous,&st)&&st.st_size>7*1024*1024);strcpy(client.config.build_mode,"hybrid");assert(!muse_log_flush(&log));assert(!stat(log.path,&st)&&st.st_size>0);muse_log_close(&log);
    puts("local jobs: approval binding, crash budget, explicit resume, immutable source hashes, bounded retention preserving both slots, 512-attempt restart accounting, torn checkpoint fallback, offline transport and private bounded logs passed");
}

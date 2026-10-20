#define _POSIX_C_SOURCE 200809L
#include "client_install.h"
#include "client_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    assert(argc==4);struct muse_config cfg;muse_config_defaults(&cfg);strcpy(cfg.build_mode,"hybrid");memset(cfg.relay_token,'a',64);cfg.relay_token[64]=0;snprintf(cfg.server_url,sizeof(cfg.server_url),"http://127.0.0.1:%s",argv[1]);cfg.loader_port=(unsigned short)atoi(argv[2]);
    snprintf(cfg.artifact_dir,sizeof(cfg.artifact_dir),"%s/artifacts",argv[3]);snprintf(cfg.log_dir,sizeof(cfg.log_dir),"%s/logs",argv[3]);snprintf(cfg.install_helper_path,sizeof(cfg.install_helper_path),"%s/helper.payload",argv[3]);
    assert(!mkdir(cfg.log_dir,0700));
    char alias[1024],target[1024];
    snprintf(alias,sizeof(alias),"%s/helper-artifacts",argv[3]);assert(!symlink(cfg.artifact_dir,alias));snprintf(cfg.installer_artifact_dir,sizeof(cfg.installer_artifact_dir),"%s/helper-artifacts",argv[3]);
    snprintf(alias,sizeof(alias),"%s/helper-journal",argv[3]);snprintf(target,sizeof(target),"%s/installer",cfg.log_dir);assert(!symlink(target,alias));snprintf(cfg.installer_log_dir,sizeof(cfg.installer_log_dir),"%s/helper-journal",argv[3]);
    struct muse_client client;muse_client_init(&client,&cfg);struct muse_build_status manifest;char path[1024];
    /* Identity, size and unsupported streaming framing fail before any helper executes. */
    assert(muse_client_status(&client,"own",&manifest));assert(muse_client_status(&client,"system",&manifest));
    const char *invalid[]={"wrongcid","corrupt","truncated","chunked","oversized"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){assert(!muse_client_status(&client,invalid[i],&manifest));assert(muse_client_artifact(&client,invalid[i],&manifest,path,sizeof(path)));}
    assert(!muse_client_status(&client,"app1",&manifest));assert(!muse_client_artifact(&client,"app1",&manifest,path,sizeof(path)));
    struct muse_install_status rejected;
    strcpy(client.config.installer_artifact_dir,"/nonexistent-helper-namespace");
    assert(!muse_install_begin(&client,path,&manifest,&rejected)&&!rejected.submitted&&strstr(client.error,"namespace"));
    strcpy(client.config.installer_artifact_dir,cfg.installer_artifact_dir);strcpy(client.config.installer_log_dir,"/nonexistent-helper-journal");
    assert(!muse_install_begin(&client,path,&manifest,&rejected)&&!rejected.submitted&&strstr(client.error,"journal"));
    strcpy(client.config.installer_log_dir,cfg.installer_log_dir);
    /* Arbitrary chunk boundaries must produce the published SHA vector. */
    struct muse_sha256 sha;muse_sha256_init(&sha);muse_sha256_update(&sha,"a",1);muse_sha256_update(&sha,"bc",2);char hex[65];muse_sha256_final(&sha,hex);assert(!strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    for(int which=0;which<9;which++){
        char job[32];snprintf(job,sizeof(job),"app%d",which);assert(!muse_client_status(&client,job,&manifest));assert(!muse_client_artifact(&client,job,&manifest,path,sizeof(path)));assert(!muse_client_verify_package(&client,path,&manifest));
        struct muse_install_status status;struct muse_install *session=muse_install_begin(&client,path,&manifest,&status);
        if(which==0||which==2||which==3){assert(!session);assert(!status.installed&&!status.launch_requested);continue;}
        assert(session&&status.submitted&&!status.installed&&!status.launch_requested);
        /* Launch before OS completion must be refused without sending a native launch. */
        if(which==1)assert(muse_install_launch(session,&status));
        int rc=muse_install_poll(session,&status);
        if(which==4){assert(rc&&!status.installed&&!status.launch_requested);muse_install_close(session);continue;}
        assert(!rc&&!status.installed&&status.progress_percent==50);
        assert(!muse_install_poll(session,&status)&&status.installed);
        if(which==7){
            assert(status.progress_percent==60&&!status.completed);
            assert(muse_install_launch(session,&status)&&!status.launch_requested&&!status.running_observed);
            assert(!muse_install_poll(session,&status)&&status.installed&&!status.completed&&status.progress_percent==60);
            assert(!muse_install_poll(session,&status)&&status.completed&&status.progress_percent==100);
        }else assert(status.completed);
        rc=muse_install_launch(session,&status);
        if(which==5)assert(rc&&status.installed&&!status.launch_requested&&!status.running_observed);
        else if(which==6)assert(rc&&status.installed&&status.launch_requested&&!status.running_observed);
        else if(which==8)assert(rc&&!status.installed&&!status.completed&&!status.launch_requested&&!status.running_observed);
        else assert(!rc&&status.installed&&status.completed&&status.launch_requested&&status.running_observed&&status.app_id==42);
        muse_install_close(session);
    }
    assert(!muse_install_recover_logs(&client));
    char journal[1100];snprintf(journal,sizeof journal,"%s/installer/pending.jsonl",cfg.log_dir);
    FILE *pending=fopen(journal,"ab");assert(pending);assert(fputs("{\"ts\":1,\"level\":\"error\",\"event\":\"auth_recovery\",\"detail\":\"retained until key restored\"}\n",pending)>0);assert(!fclose(pending));
    for(int denied=401;denied<=403;denied+=2){
        memset(client.config.relay_token,denied==401?'b':'c',64);client.config.relay_token[64]=0;
        assert(muse_install_recover_logs(&client));assert(client.http_status==denied&&strstr(client.error,"update it in setup"));
        struct stat retained;assert(!stat(journal,&retained)&&retained.st_size>0);
    }
    strcpy(client.config.relay_token,cfg.relay_token);assert(!muse_install_recover_logs(&client));
    struct stat drained;assert(!stat(journal,&drained)&&!drained.st_size);
    puts("package identity/stream/hash, authenticated helper, shared-journal preflight, new-title check, install status and observed launch tests passed");return 0;
}

#define _POSIX_C_SOURCE 200809L
#include "client_install.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc,char **argv){
    assert(argc==5);const char *scenario=argv[4];
    struct muse_config cfg;muse_config_defaults(&cfg);strcpy(cfg.build_mode,"hybrid");
    memset(cfg.relay_token,'a',64);cfg.relay_token[64]=0;
    snprintf(cfg.server_url,sizeof(cfg.server_url),"http://127.0.0.1:%s",argv[1]);
    cfg.loader_port=(unsigned short)atoi(argv[2]);
    snprintf(cfg.artifact_dir,sizeof(cfg.artifact_dir),"%s/artifacts",argv[3]);
    snprintf(cfg.log_dir,sizeof(cfg.log_dir),"%s/logs",argv[3]);
    snprintf(cfg.install_helper_path,sizeof(cfg.install_helper_path),"%s/helper.payload",argv[3]);
    assert(!mkdir(cfg.log_dir,0700));
    struct muse_client client;muse_client_init(&client,&cfg);
    struct muse_build_status manifest;char path[1024];
    assert(!muse_client_status(&client,"app1",&manifest));
    assert(!muse_client_artifact(&client,"app1",&manifest,path,sizeof(path)));
    if(!strcmp(scenario,"auth401")||!strcmp(scenario,"auth403")){
        memset(client.config.relay_token,!strcmp(scenario,"auth401")?'b':'c',64);client.config.relay_token[64]=0;
    }
    struct muse_install_status status;
    struct muse_install *session=muse_install_begin(&client,path,&manifest,&status);
    if(!strcmp(scenario,"success")){
        assert(session&&status.submitted&&!status.completed&&!status.launch_requested);
        assert(!muse_install_poll(session,&status)&&!status.completed);
        assert(!muse_install_poll(session,&status)&&status.completed);
        assert(!muse_install_launch(session,&status)&&status.running_observed);
        muse_install_close(session);
    }else{
        assert(!session&&!status.submitted&&!status.installed&&!status.completed&&!status.launch_requested);
        if(!strncmp(scenario,"auth",4))assert(strstr(client.error,"update it in setup"));
        else if(!strncmp(scenario,"symbol:",7))assert(strstr(client.error,scenario+7));
        else if(!strcmp(scenario,"init"))assert(strstr(client.error,"AppInstUtil Initialize"));
        else assert(strstr(client.error,"AppInstUtil module load failed"));
    }
    printf("helper runtime scenario %s passed\n",scenario);return 0;
}

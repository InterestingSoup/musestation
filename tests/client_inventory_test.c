/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    assert(argc==3);struct muse_config cfg;muse_config_defaults(&cfg);
    strcpy(cfg.console_ip,"127.0.0.1");memset(cfg.relay_token,'a',64);cfg.relay_token[64]=0;
    snprintf(cfg.server_url,sizeof cfg.server_url,"http://127.0.0.1:%s",argv[1]);
    strcpy(cfg.shadowmount_url,cfg.server_url);cfg.timeout_ms=1000;
    if(!strcmp(argv[2],"local"))strcpy(cfg.build_mode,"local");
    if(!strcmp(argv[2],"remote"))strcpy(cfg.shadowmount_url,"http://192.168.5.22:10101");
    struct muse_client c;muse_client_init(&c,&cfg);char job[129];
    int rc=muse_client_approve(&c,"p1","r1",job);
    if(!strcmp(argv[2],"ok")){if(rc)fprintf(stderr,"inventory: %s\n",c.error);assert(!rc&&!strcmp(job,"j1"));
        /* Each explicit retry obtains a new snapshot; no cached empty inventory. */
        assert(!muse_client_approve(&c,"p1","r1",job)&&!strcmp(job,"j1"));
    }else {assert(rc<0);assert(strstr(c.error,!strcmp(argv[2],"local")?"Local mode":"ShadowMount"));}
    return 0;
}

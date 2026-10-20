/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "client_phone.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int received;
static int submit(void *context,const char *path){
    (void)context;struct stat st;assert(!stat(path,&st)&&st.st_size==44);assert(!unlink(path));pthread_mutex_lock(&lock);received++;pthread_mutex_unlock(&lock);return 0;
}
int main(int argc,char **argv){
    assert(argc==3);struct muse_config cfg;muse_config_defaults(&cfg);strcpy(cfg.build_mode,"hybrid");cfg.phone_port=(unsigned short)atoi(argv[1]);snprintf(cfg.artifact_dir,sizeof(cfg.artifact_dir),"%s",argv[2]);char error[256];
    strcpy(cfg.build_mode,"local");assert(!muse_phone_start(&cfg,submit,NULL,error,sizeof error)&&strstr(error,"Local mode"));strcpy(cfg.build_mode,"hybrid");
    struct muse_phone *phone=muse_phone_start(&cfg,submit,NULL,error,sizeof(error));assert(phone);printf("%s\n",muse_phone_token(phone));fflush(stdout);
    for(int i=0;i<150;i++){pthread_mutex_lock(&lock);int done=received;pthread_mutex_unlock(&lock);if(done)break;struct timespec delay={0,100000000};nanosleep(&delay,NULL);}
    muse_phone_stop(phone);assert(received==1);puts("phone pairing/upload tests passed");return 0;
}

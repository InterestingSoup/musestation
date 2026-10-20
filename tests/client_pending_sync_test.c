/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int directory_error,file_error;
static unsigned file_syncs,directory_syncs;
int __real_fsync(int);
int __wrap_fsync(int fd){
    struct stat st;assert(!fstat(fd,&st));
    if(S_ISDIR(st.st_mode)){++directory_syncs;if(directory_error){errno=directory_error;return -1;}}
    else {++file_syncs;if(file_error){errno=file_error;return -1;}}
    return __real_fsync(fd);
}
int main(int argc,char **argv){
    assert(argc==3);struct muse_config cfg;muse_config_defaults(&cfg);strcpy(cfg.relay_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(cfg.server_url,sizeof cfg.server_url,"http://127.0.0.1:%s",argv[1]);
    snprintf(cfg.artifact_dir,sizeof cfg.artifact_dir,"%s",argv[2]);
    struct muse_client c;muse_client_init(&c,&cfg);struct muse_plan p;
    const int unsupported[]={EINVAL,ENOTSUP};
    for(unsigned i=0;i<sizeof unsupported/sizeof *unsupported;i++){
        directory_error=unsupported[i];
        assert(muse_client_transcript(&c,"sync fixture","",&p)==MUSE_CLIENT_PENDING);
        assert(muse_client_pending_load(&c,&p)==MUSE_CLIENT_PENDING&&strlen(p.id)==33);
        assert(!muse_client_pending_clear(&c,p.id));
        assert(muse_client_pending_load(&c,&p)==MUSE_CLIENT_NO_PENDING);
    }
    directory_error=EIO;
    assert(muse_client_transcript(&c,"sync fixture","",&p)<0&&strstr(c.error,"sync pending-plan"));
    /* The rename happened, but the I/O error was not called success. */
    assert(muse_client_pending_load(&c,&p)==MUSE_CLIENT_PENDING);
    directory_error=0;assert(!muse_client_pending_clear(&c,""));
    file_error=EIO;
    assert(muse_client_transcript(&c,"sync fixture","",&p)<0&&strstr(c.error,"commit pending"));
    assert(muse_client_pending_load(&c,&p)==MUSE_CLIENT_NO_PENDING);
    assert(file_syncs==4&&directory_syncs>=5);
    puts("pending-plan checkpoint: unsupported directory fsync tolerated; file fsync and real directory I/O errors fail explicitly");
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-only integration fixture: explicit simulated X, no install/launch calls. */
#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include "gate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void check(int rc, struct muse_client *c, const char *operation) {
    if (rc) { fprintf(stderr, "%s failed (%d): %s\n", operation, rc, c->error); exit(2); }
}
int main(int argc, char **argv) {
    assert(argc == 4);
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct muse_config cfg; muse_config_defaults(&cfg);
    snprintf(cfg.build_mode,sizeof cfg.build_mode,"hybrid");
    snprintf(cfg.server_url,sizeof cfg.server_url,"%s",argv[1]);
    snprintf(cfg.console_ip,sizeof cfg.console_ip,"127.0.0.1");
    snprintf(cfg.shadowmount_url,sizeof cfg.shadowmount_url,"%s",argv[3]);
    snprintf(cfg.artifact_dir,sizeof cfg.artifact_dir,"%s",argv[2]);
    char token_path[1024]; snprintf(token_path,sizeof token_path,"%s/console-token",argv[2]);
    FILE *token_file=fopen(token_path,"rb"); assert(token_file);
    assert(fread(cfg.relay_token,1,64,token_file)==64); cfg.relay_token[64]=0;
    assert(fgetc(token_file)=='\n' && fgetc(token_file)==EOF); assert(!fclose(token_file));
    cfg.timeout_ms=5000;
    struct muse_client c; muse_client_init(&c,&cfg);
    struct muse_gate gate={0}; muse_reset(&gate);
    struct muse_plan p={0}, recovered={0};
    int rc=muse_client_transcript(&c,"Fixture only: create the native graphics test app.","",&p);
    assert(rc==MUSE_CLIENT_PENDING && p.id[0] && !p.revision[0] && !p.text[0]);
    assert(gate.state==MUSE_IDLE);
    assert(muse_client_pending_load(&c,&recovered)==MUSE_CLIENT_PENDING);
    assert(!strcmp(p.id,recovered.id));
    printf("PENDING %s\n",p.id);
    char id[129]; snprintf(id,sizeof id,"%s",p.id);
    for(int n=0; rc==MUSE_CLIENT_PENDING && n<180; ++n) {
        sleep(1); rc=muse_client_plan_resume(&c,id,&p);
    }
    check(rc,&c,"plan");
    check(muse_set_plan(&gate,p.id,p.revision,p.text),&c,"gate plan");
    assert(gate.state==MUSE_PLAN);
    assert(muse_pad(&gate,1)==0);  /* held on arrival cannot approve */
    assert(muse_pad(&gate,0)==0);
    assert(muse_pad(&gate,1)==1);  /* explicit test-controller rising edge */
    assert(gate.state==MUSE_APPROVING);
    char job[129], duplicate[129];
    check(muse_client_approve(&c,p.id,p.revision,job),&c,"approve");
    check(muse_client_approve(&c,p.id,p.revision,duplicate),&c,"idempotent retry");
    assert(!strcmp(job,duplicate));
    check(muse_job(&gate,gate.generation,job),&c,"gate job");
    printf("APPROVED_FIXTURE %s\n",job);
    struct muse_build_status status={0};
    for(int n=0;n<900;++n) {
        check(muse_client_status(&c,job,&status),&c,"status");
        if(!strcmp(status.state,"done")) break;
        if(!strcmp(status.state,"error")) { fprintf(stderr,"Build error: %s\n",status.detail); return 3; }
        sleep(1);
    }
    assert(!strcmp(status.state,"done") && !strcmp(status.kind,"pkg"));
    char artifact[1024]; check(muse_client_artifact(&c,job,&status,artifact,sizeof artifact),&c,"download");
    check(muse_client_verify_package(&c,artifact,&status),&c,"package identity");
    check(muse_build_done(&gate,gate.generation,job),&c,"build state");
    check(muse_artifact_verified(&gate,gate.generation),&c,"verified state");
    assert(gate.state==MUSE_READY);
    char logpath[1200]; snprintf(logpath,sizeof logpath,"%s/fixture.jsonl",argv[2]);
    FILE *f=fopen(logpath,"wb"); assert(f);
    fprintf(f,"{\"ts\":%lld,\"level\":\"info\",\"event\":\"fixture_download_verified\",\"detail\":\"job=%s; host test only; no install or launch\"}\n",(long long)time(NULL),job);
    assert(!fclose(f)); check(muse_client_logs(&c,logpath),&c,"log upload");
    printf("VERIFIED %s %s %llu %s\n",status.title_id,status.sha256,(unsigned long long)status.size,artifact);
    puts("PASS actual gadget C client: pending plan recovery, explicit simulated approval, idempotency, native package download/identity/hash, and log upload. No console/install/launch.");
    return 0;
}

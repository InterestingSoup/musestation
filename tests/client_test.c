/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include "client_sha256.h"
#include "json.h"
#include "log.h"
#include "gate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void json_tests(void){
    struct muse_json j;char s[64];uint64_t n;
    const char *valid="{\"x\":[{\"a\":\"hello\\n\\uD83D\\uDE00\"}],\"n\":18446744073709551615}";
    assert(!muse_json_parse(&j,valid,strlen(valid)));
    int x=muse_json_get(&j,0,"x");assert(x>=0);
    assert(!muse_json_string(&j,muse_json_get(&j,x+1,"a"),s,sizeof(s)));assert(!strcmp(s,"hello\n\xf0\x9f\x98\x80"));
    assert(!muse_json_u64(&j,muse_json_get(&j,0,"n"),&n)&&n==UINT64_MAX);
    const char *bad[]={"{\"a\":1,\"a\":2}","{\"a\":1,\"\\u0061\":2}","{\"a\":}","[1,]","01","{\"x\":\"\\uD800\"}","{\"x\":\"\\u0000\"}","{\"x\":\"\xc0\x80\"}","true false","{\"x\":1e}","{\"x\":1.}"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(muse_json_parse(&j,bad[i],strlen(bad[i])));
    assert(!muse_json_quote("\"\\\n",s,sizeof(s)));assert(!strcmp(s,"\"\\\"\\\\\\u000a\""));
    size_t consumed=0;const char *prefix=" {\"x\":1}, next";
    assert(!muse_json_parse_prefix(&j,prefix,strlen(prefix),&consumed)&&consumed==8);
    assert(muse_json_parse(&j,prefix,strlen(prefix)));
    const char *duplicate_prefix="{\"x\":1,\"x\":2}, next";assert(muse_json_parse_prefix(&j,duplicate_prefix,strlen(duplicate_prefix),&consumed));
    char sha[65];muse_sha256_hex("",0,sha);assert(!strcmp(sha,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    muse_sha256_hex("abc",3,sha);assert(!strcmp(sha,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *longmsg="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    muse_sha256_hex(longmsg,strlen(longmsg),sha);assert(!strcmp(sha,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    char *million=malloc(1000000);assert(million);memset(million,'a',1000000);muse_sha256_hex(million,1000000,sha);free(million);assert(!strcmp(sha,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
int main(int argc,char **argv){
    assert(argc==4);json_tests();
    struct muse_config cfg;muse_config_defaults(&cfg);strcpy(cfg.relay_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");strcpy(cfg.build_mode,"hybrid");strcpy(cfg.console_ip,"127.0.0.1");snprintf(cfg.server_url,sizeof(cfg.server_url),"http://127.0.0.1:%s",argv[1]);strcpy(cfg.shadowmount_url,cfg.server_url);strcpy(cfg.ps5lm_url,cfg.server_url);strcpy(cfg.speech_url,cfg.server_url);
    snprintf(cfg.log_dir,sizeof(cfg.log_dir),"%s/logs",argv[2]);snprintf(cfg.artifact_dir,sizeof(cfg.artifact_dir),"%s/artifacts",argv[2]);snprintf(cfg.model_dir,sizeof(cfg.model_dir),"%s/models",argv[2]);strcpy(cfg.audio_in_module,"/system/common/lib/libSceAudioIn.sprx");strcpy(cfg.etahen_status,"owner-reported test version");cfg.timeout_ms=1000;cfg.loader_port=(unsigned short)atoi(argv[3]);
    char error[256],config_path[1024];snprintf(config_path,sizeof(config_path),"%s/config.json",argv[2]);assert(!muse_config_validate(&cfg,error,sizeof(error)));assert(!muse_config_save(config_path,&cfg,error,sizeof(error)));
    struct muse_config loaded;assert(!muse_config_load(config_path,&loaded,error,sizeof(error)));assert(!strcmp(loaded.etahen_status,cfg.etahen_status));assert(!strcmp(loaded.server_url,cfg.server_url));assert(!strcmp(loaded.relay_token,cfg.relay_token));assert(!strcmp(loaded.shadowmount_url,cfg.shadowmount_url));
    assert(muse_config_console_origin(&cfg,"http://127.0.0.2:10101"));assert(!muse_config_console_origin(&cfg,"http://192.168.5.22:10101"));
    const char *private_origins[]={"http://10.0.0.1","http://172.16.1.1:80/","http://192.168.5.54","http://127.0.0.1","http://169.254.1.1","http://[::1]","http://[fd00::1]","http://[fe80::1]","http://localhost"};
    for(unsigned i=0;i<sizeof private_origins/sizeof *private_origins;i++)assert(muse_config_private_origin(private_origins[i]));
    const char *public_origins[]={"http://8.8.8.8","http://100.64.0.1","http://172.15.0.1","http://172.32.0.1","http://[2001:4860:4860::8888]","http://[::ffff:127.0.0.1]","http://0.0.0.0"};
    for(unsigned i=0;i<sizeof public_origins/sizeof *public_origins;i++)assert(!muse_config_private_origin(public_origins[i]));
    loaded.relay_token[0]=0;assert(muse_config_validate(&loaded,error,sizeof error));
    strcpy(loaded.server_url,"https://127.0.0.1");assert(muse_config_validate(&loaded,error,sizeof(error)));strcpy(loaded.server_url,"http://example.com");assert(muse_config_validate(&loaded,error,sizeof(error)));
    struct muse_config recovery_config=cfg;strcpy(recovery_config.inference_backend,"cpu");strcpy(recovery_config.speech_mode,"espeak");strcpy(recovery_config.install_helper_path,"/app0/assets/install/muse-install-helper.payload");strcpy(recovery_config.installer_artifact_dir,"/shared/artifacts");strcpy(recovery_config.installer_log_dir,"/shared/installer-journal");
    assert(!muse_config_save(config_path,&recovery_config,error,sizeof(error)));assert(!muse_config_load(config_path,&loaded,error,sizeof(error)));assert(!strcmp(loaded.inference_backend,"cpu")&&!strcmp(loaded.speech_mode,"espeak")&&!strcmp(loaded.install_helper_path,recovery_config.install_helper_path));
    assert(!strcmp(loaded.installer_artifact_dir,recovery_config.installer_artifact_dir)&&!strcmp(loaded.installer_log_dir,recovery_config.installer_log_dir));
    strcpy(recovery_config.installer_log_dir,"/bad/../path");assert(muse_config_validate(&recovery_config,error,sizeof(error)));strcpy(recovery_config.installer_log_dir,"/shared/installer-journal");
    strcpy(recovery_config.inference_backend,"imaginary");assert(muse_config_validate(&recovery_config,error,sizeof(error)));strcpy(recovery_config.inference_backend,"auto");strcpy(recovery_config.speech_mode,"cloud");assert(muse_config_validate(&recovery_config,error,sizeof(error)));
    struct muse_config bundled=cfg;bundled.ps5lm_url[0]=0;bundled.speech_url[0]=0;
    bundled.audio_in_module[0]=0;
    assert(!muse_config_validate(&bundled,error,sizeof(error)));assert(!muse_config_save(config_path,&bundled,error,sizeof(error)));
    assert(!muse_config_load(config_path,&loaded,error,sizeof(error)));assert(!*loaded.ps5lm_url&&!*loaded.speech_url&&!*loaded.audio_in_module);
    strcpy(bundled.audio_in_module,"relative/AudioIn.sprx");assert(muse_config_validate(&bundled,error,sizeof error));bundled.audio_in_module[0]=0;
    struct muse_client disabled;muse_client_init(&disabled,&loaded);char local_text[4097];
    assert(muse_client_draft(&disabled,"make tetris",local_text));assert(strstr(disabled.error,"not configured"));
    assert(muse_client_transcribe(&disabled,"missing.wav",local_text));assert(strstr(disabled.error,"not configured"));
    strcpy(bundled.ps5lm_url,"not-a-url");assert(muse_config_validate(&bundled,error,sizeof(error)));bundled.ps5lm_url[0]=0;
    strcpy(bundled.speech_url,"https://127.0.0.1");assert(muse_config_validate(&bundled,error,sizeof(error)));bundled.speech_url[0]=0;
    bundled.server_url[0]=0;assert(muse_config_validate(&bundled,error,sizeof(error)));
    struct muse_client c;muse_client_init(&c,&cfg);struct muse_plan plan;assert(!muse_client_plan(&c,"p1",&plan));assert(!strcmp(plan.text,"Build Tetris. Use the controller."));assert(!strcmp(plan.revision,"r1"));assert(!strcmp(plan.reply_wav,"/replies/p1.wav"));assert(muse_client_plan(&c,"../escape",&plan));assert(muse_client_plan(&c,"wrong_id",&plan));assert(muse_client_plan(&c,"duplicate",&plan));
    for(int i=0;i<8;i++){char id[16];snprintf(id,sizeof(id),"uri%d",i);assert(muse_client_plan(&c,id,&plan));}
    struct muse_plan no_reply;assert(!muse_client_plan(&c,"no_reply",&no_reply));assert(!*no_reply.reply_wav);
    unsigned char *speech_bytes=NULL;size_t speech_length=0;
    assert(muse_client_http(&c,c.config.server_url,"GET",plan.reply_wav,NULL,NULL,0,8u*1024*1024,&speech_bytes,&speech_length));
    assert(!muse_client_http(&c,c.config.server_url,"GET",plan.reply_wav,"audio/wav",NULL,0,8u*1024*1024,&speech_bytes,&speech_length));
    assert(speech_length==3244&&!memcmp(speech_bytes,"RIFF",4)&&!memcmp(speech_bytes+8,"WAVE",4));free(speech_bytes);
    struct muse_gate gate={0};muse_reset(&gate);assert(!muse_set_plan(&gate,plan.id,plan.revision,plan.text));uint64_t generation=gate.generation;
    assert(muse_pad(&gate,1)==0);assert(muse_pad(&gate,0)==0);assert(muse_pad(&gate,1)==1);
    char job[129];assert(!muse_client_approve(&c,"p1","r1",job));assert(!strcmp(job,"j1"));assert(!muse_job(&gate,generation,job));struct muse_build_status status;assert(!muse_client_status(&c,job,&status));assert(!strcmp(status.state,"done"));
    char artifact[1024];assert(!muse_client_artifact(&c,job,&status,artifact,sizeof(artifact)));struct stat st;assert(!stat(artifact,&st)&&st.st_size==(off_t)status.size);
    assert(!muse_build_done(&gate,generation,job));assert(!muse_artifact_verified(&gate,generation));assert(!muse_dispatch(&gate));assert(!muse_client_dispatch(&c,artifact));assert(muse_input(&gate,MUSE_CIRCLE)==-1);
    muse_reset(&gate);assert(!muse_set_plan(&gate,"p1","r1","Plan"));generation=gate.generation;assert(!muse_pad(&gate,0));assert(muse_pad(&gate,1)==1);assert(!muse_input(&gate,MUSE_CIRCLE));assert(muse_job(&gate,generation,"stale_job"));
    status.sha256[0]=status.sha256[0]=='0'?'1':'0';assert(muse_client_artifact(&c,job,&status,artifact,sizeof(artifact)));assert(muse_client_status(&c,"bad_hash",&status));assert(muse_client_status(&c,"huge",&status));assert(muse_client_status(&c,"pkg",&status));
    char draft[4097],intent[16];assert(!muse_client_draft(&c,"make tetris",draft));assert(!strcmp(draft,"Build Tetris with controller input. Muse will review."));assert(!muse_client_classify(&c,"yes please",intent));assert(!strcmp(intent,"approve"));assert(muse_client_classify(&c,"malformed",intent));
    assert(!muse_client_transcript(&c,"make tetris",draft,&plan));assert(!strcmp(plan.transcript,"make tetris"));
    assert(!muse_client_transcript_context(&c,"make tetris",draft,"parent1","rev2",&plan));
    assert(muse_client_transcript_context(&c,"make tetris",draft,"parent1","",&plan));
    assert(muse_client_transcript_context(&c,"make tetris",draft,"","rev2",&plan));
    assert(muse_client_transcript_context(&c,"make tetris",draft,"../parent","rev2",&plan));
    char wav[1024];snprintf(wav,sizeof(wav),"%s/input.wav",argv[2]);FILE *f=fopen(wav,"wb");assert(f);unsigned char audio[44]={0};memcpy(audio,"RIFF",4);memcpy(audio+8,"WAVE",4);assert(fwrite(audio,1,sizeof(audio),f)==sizeof(audio));assert(!fclose(f));
    assert(!muse_client_voice(&c,wav,&plan));assert(!muse_client_transcribe(&c,wav,draft));assert(!strcmp(draft,"make tetris"));
    struct muse_plan waiting;
    assert(muse_client_transcript(&c,"reject request","",&waiting)<0&&c.http_status==422&&strstr(c.error,"Request rejected by relay"));
    const char *lost[]={"lost saved","lost before save","lost body"};
    for(unsigned i=0;i<sizeof lost/sizeof *lost;i++){
        assert(muse_client_transcript(&c,lost[i],"exact draft",&waiting)==MUSE_CLIENT_PENDING);
        char saved_id[129];strcpy(saved_id,waiting.id);assert(strlen(saved_id)==33&&saved_id[0]=='p');
        struct muse_client resumed;muse_client_init(&resumed,&cfg);
        assert(muse_client_pending_load(&resumed,&waiting)==MUSE_CLIENT_PENDING&&!strcmp(waiting.id,saved_id));
        assert(!muse_client_plan_resume(&resumed,saved_id,&waiting)&&!strcmp(waiting.id,saved_id));
        assert(!muse_client_pending_clear(&resumed,saved_id));
    }
    assert(muse_client_transcript(&c,"slow request","",&waiting)==MUSE_CLIENT_PENDING);
    char slow_id[129];strcpy(slow_id,waiting.id);
    assert(!strcmp(waiting.id,slow_id)&&!strcmp(waiting.transcript,"slow request")&&!waiting.text[0]&&!waiting.revision[0]&&waiting.detail[0]);
    const char *bad_pending[]={"pending_injection","pending_wrong","pending_state","pending_large"};
    for(size_t i=0;i<sizeof(bad_pending)/sizeof(*bad_pending);i++)assert(muse_client_plan(&c,bad_pending[i],&waiting)<0);
    assert(muse_client_plan(&c,"rejected",&waiting)<0&&c.http_status==422&&strstr(c.error,"invalid plan"));
    struct muse_client restarted;muse_client_init(&restarted,&cfg);
    assert(muse_client_pending_load(&restarted,&waiting)==MUSE_CLIENT_PENDING&&!strcmp(waiting.id,slow_id)&&!strcmp(waiting.transcript,"slow request"));
    strcpy(restarted.config.server_url,"http://127.0.0.1:1");assert(muse_client_pending_load(&restarted,&waiting)==MUSE_CLIENT_NO_PENDING);
    strcpy(restarted.config.server_url,cfg.server_url);strcpy(restarted.config.build_mode,"local");assert(muse_client_pending_load(&restarted,&waiting)<0);
    strcpy(restarted.config.build_mode,"hybrid");assert(!muse_client_pending_clear(&restarted,"another_id"));assert(muse_client_pending_load(&restarted,&waiting)==MUSE_CLIENT_PENDING);
    assert(!muse_client_pending_clear(&restarted,slow_id));assert(muse_client_pending_load(&restarted,&waiting)==MUSE_CLIENT_NO_PENDING);
    char checkpoint[1024];snprintf(checkpoint,sizeof(checkpoint),"%s/muse-pending-plan.json",cfg.artifact_dir);FILE *broken=fopen(checkpoint,"wb");assert(broken);assert(fputs("{broken",broken)>0);assert(!fclose(broken));
    assert(muse_client_pending_load(&restarted,&waiting)<0);assert(!muse_client_pending_clear(&restarted,""));
    assert(!mkfifo(checkpoint,0600));assert(muse_client_pending_load(&restarted,&waiting)<0);assert(!unlink(checkpoint));
    unsigned char *reply;size_t len;assert(!muse_client_http(&c,cfg.server_url,"GET","/chunked","application/json",NULL,0,32,&reply,&len));assert(len==7&&!memcmp(reply,"{\"x\":1}",7));free(reply);
    const char *denied[]={"/denied","/forbidden"};for(unsigned i=0;i<2;i++){assert(muse_client_http(&c,cfg.server_url,"GET",denied[i],"application/json",NULL,0,1024,&reply,&len)<0);assert(!strcmp(c.error,"Relay console key rejected; update it in setup."));}
    const char *bad_paths[]={"/truncated","/conflict","/oversize","/badchunk","/redirect","/stall"};for(size_t i=0;i<sizeof(bad_paths)/sizeof(*bad_paths);i++)assert(muse_client_http(&c,cfg.server_url,"GET",bad_paths[i],"application/json",NULL,0,32,&reply,&len));
    struct muse_log log;assert(!muse_log_open(&log,&c));assert(!muse_log_event(&log,"info","boot","quoted \"message\"\nnext line"));assert(!stat(log.path,&st)&&st.st_size>0);assert(!muse_log_flush(&log));assert(!stat(log.path,&st)&&!st.st_size);
    strcpy(c.config.server_url,"http://127.0.0.1:1");assert(muse_log_event(&log,"error","failure","offline failure"));assert(!stat(log.path,&st)&&st.st_size>0);
    FILE *partial=fopen(log.path,"ab");assert(partial);assert(fputs("{\"partial\":",partial)>0);assert(!fclose(partial));
    strcpy(c.config.server_url,cfg.server_url);struct muse_log recovered;assert(!muse_log_open(&recovered,&c));assert(!stat(recovered.path,&st)&&!st.st_size);assert(!muse_log_event(&recovered,"info","session_end","complete"));assert(!stat(recovered.path,&st)&&!st.st_size);muse_log_close(&recovered);
    puts("client/json/config/sha256/log tests passed");return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Real worker/store/Lua integration; controlled model fixture tests orchestration,
// while the separate inference checks exercise the actual bundled model.
#include "worker.hpp"
#include "muse_inference.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
void test_enable_model_network(bool);
static std::atomic<int> calls{0}, behavior{0};
static uint32_t initial_seed=0;
struct muse_inference { bool llm; };
static int voice_only_starts=0;
extern "C" int sceUserServiceGetInitialUser(int *) { return -1; }
extern "C" muse_inference *muse_inference_create(const muse_inference_config *cfg,char *error,size_t cap){
 if(cfg->llm_model&&strstr(cfg->llm_model,"bad-coder")){snprintf(error,cap,"Unsupported model fixture");return nullptr;}
 bool llm=cfg->llm_model&&*cfg->llm_model;if(!llm)++voice_only_starts;return new muse_inference{llm};
}
extern "C" void muse_inference_destroy(muse_inference *p){delete p;}
extern "C" int muse_inference_draft(muse_inference *engine,const char *,char *out,size_t cap,char *error,size_t error_cap){if(!engine->llm){snprintf(error,error_cap,"coding model missing");return -1;}snprintf(out,cap,"Make a counter with Cross controls. Draw it in large type.");return 0;}
extern "C" int muse_inference_transcribe(muse_inference *,const int16_t *,size_t,int,char *,size_t,char *,size_t){return -1;}
extern "C" int muse_inference_speak(muse_inference *,const char *,const char *,char *,size_t){return -1;}
extern "C" int muse_inference_classify(muse_inference *,const char *,const char *,muse_local_intent *,char *,size_t){return -1;}
extern "C" int muse_inference_status(muse_inference *,char *out,size_t cap){snprintf(out,cap,"test fixture; Qwen: fixture; Whisper: fixture; TTS: fixture");return 0;}
extern "C" int muse_inference_codegen(muse_inference *,const muse_codegen_request *r,char *out,size_t cap,muse_codegen_stats *,char *,size_t){
  const int which=++calls;
  if(which==1)initial_seed=r->seed;else if(which==2)assert(r->seed!=initial_seed);
  assert(r->max_duration_ms<=1800000 && strstr(r->system_prompt,"Lua"));
  if(behavior.load()==1){
    const char partial[]="-- partial, must never execute\n";
    snprintf(out,cap,"%s",partial);
    r->progress(r->userdata,partial,strlen(partial),512);
    for(int i=0;i<2000&&!r->should_stop(r->userdata);i++)usleep(1000);
    assert(r->should_stop(r->userdata));return MUSE_CODEGEN_STOPPED;
  }
  if(which==1)snprintf(out,cap,"function update() forbidden() end\nfunction draw() end\n");
  else {
    assert(strstr(r->diagnostics,"forbidden"));
    assert(strstr(r->previous_source,"forbidden"));
    snprintf(out,cap,"local n=0\nfunction update(dt,input) if input.pressed.cross then n=n+1 end end\nfunction draw() gfx.clear(0,0,0) gfx.text(30,30,tostring(n),40,255,255,255) end\n");
  }
  r->progress(r->userdata,out,strlen(out),512);
  return MUSE_CODEGEN_COMPLETE;
}
static muse::Result wait(muse::Worker &worker){for(int i=0;i<1000;i++){muse::Result r;if(worker.take(r))return r;usleep(5000);}std::abort();}
static muse::Result send(muse::Worker &worker,muse::Work work){assert(worker.submit(work));return wait(worker);}
int main(int argc,char **argv){
  assert(argc==3);muse_config cfg{};muse_config_defaults(&cfg);assert(!muse_config_local(&cfg));strcpy(cfg.build_mode,"local");
  snprintf(cfg.artifact_dir,sizeof cfg.artifact_dir,"%s",argv[1]);
  snprintf(cfg.log_dir,sizeof cfg.log_dir,"%s/logs",argv[1]);
  snprintf(cfg.model_dir,sizeof cfg.model_dir,"%s/models",argv[1]);
  snprintf(cfg.coding_model_path,sizeof cfg.coding_model_path,"%s/coder.gguf",argv[1]);
  snprintf(cfg.server_url,sizeof cfg.server_url,"http://127.0.0.1:%s",argv[2]);
  // Upgrade an explicitly saved Local config with a remembered Muse endpoint
  // and private spool. The new Hybrid default must not enable either transport.
  char config_path[1200]{},config_error[256]{};
  snprintf(config_path,sizeof config_path,"%s/saved-local.json",argv[1]);
  assert(!muse_config_save(config_path,&cfg,config_error,sizeof config_error));
  muse_config loaded{};muse_config_defaults(&loaded);assert(!muse_config_local(&loaded));
  assert(!muse_config_load(config_path,&loaded,config_error,sizeof config_error));
  assert(muse_config_local(&loaded)&&!loaded.phone_port&&!strcmp(loaded.server_url,cfg.server_url)&&!strcmp(loaded.coding_model_path,cfg.coding_model_path));cfg=loaded;
  assert(!mkdir(cfg.log_dir,0700));const std::string private_dir=std::string(cfg.log_dir)+"/local";
  assert(!mkdir(private_dir.c_str(),0700));const std::string private_spool=private_dir+"/pending.jsonl";
  FILE *private_file=fopen(private_spool.c_str(),"wb");assert(private_file);fputs("{\"event\":\"retained-private-before-upgrade\"}\n",private_file);assert(!fclose(private_file));
  // A first-task Local switch must precede any old Hybrid startup upload.
  muse_config old=cfg;strcpy(old.build_mode,"hybrid");strcpy(old.console_ip,"127.0.0.1");
  strcpy(old.audio_in_module,"/test/AudioIn.sprx");snprintf(old.server_url,sizeof old.server_url,"http://127.0.0.1:%s",argv[2]);
  snprintf(old.log_dir,sizeof old.log_dir,"%s/old-hybrid",argv[1]);assert(!mkdir(old.log_dir,0700));
  std::string spool=std::string(old.log_dir)+"/pending.jsonl";FILE*f=fopen(spool.c_str(),"wb");assert(f);fputs("{\"event\":\"old-private-choice-test\"}\n",f);fclose(f);
  muse::Worker switching;assert(switching.start(old));usleep(50000);
  muse::Work change;change.type=muse::Task::configure;change.configuration=cfg;auto changed=send(switching,change);
  assert(!changed.rc&&changed.configuration_applied);change.type=muse::Task::stop;assert(!send(switching,change).rc);switching.join();
  struct stat spool_stat{};assert(!stat(spool.c_str(),&spool_stat)&&spool_stat.st_size>0);
  muse::Worker worker;assert(worker.start(cfg));muse::Work w;w.type=muse::Task::init;assert(!send(worker,w).rc);
  w.type=muse::Task::local_create;w.text="Make a counter";auto r=send(worker,w);
  assert(!r.rc&&r.local_job.state==MUSE_LOCAL_AWAITING_APPROVAL&&r.plan.text[0]);
  w.id=r.local_job.id;w.revision="not-the-approved-plan";w.type=muse::Task::local_approve;assert(send(worker,w).rc);
  w.revision=r.local_job.revision;r=send(worker,w);assert(!r.rc&&r.local_job.state==MUSE_LOCAL_QUEUED);
  auto job=w;
  w.type=muse::Task::model_download;w.text="https://huggingface.co/fixture/model/resolve/main/coder.gguf";
  r=send(worker,w);assert(r.rc&&r.path.empty()); // Active approval cannot change its model.
  w.type=muse::Task::configure;w.configuration=cfg;r=send(worker,w);assert(r.rc&&!r.configuration_applied);
  w=job;w.type=muse::Task::local_step;r=send(worker,w);assert(!r.rc&&r.local_job.state==MUSE_LOCAL_QUEUED&&r.local_job.next_phase==MUSE_LOCAL_VALIDATE);
  r=send(worker,w);assert(!r.rc&&r.local_job.state==MUSE_LOCAL_QUEUED&&r.local_job.next_phase==MUSE_LOCAL_GENERATE&&strstr(r.local_job.detail,"forbidden"));
  r=send(worker,w);assert(!r.rc&&r.local_job.next_phase==MUSE_LOCAL_VALIDATE);
  r=send(worker,w);assert(!r.rc&&r.local_job.state==MUSE_LOCAL_READY&&!r.path.empty()&&r.local_job.validated_sha[0]);
  assert(calls.load()==2); // validation and reaching READY never execute another generation or launch.
  worker.set_context(r.local_job.id,r.local_job.revision,r.local_job.plan);
  w=muse::Work{};w.type=muse::Task::local_create;w.text="Use blue text";r=send(worker,w);
  assert(!r.rc&&r.local_job.state==MUSE_LOCAL_AWAITING_APPROVAL&&strstr(r.local_job.request,"User change: Use blue text")&&strstr(r.local_job.request,"Cross controls"));
  worker.set_context("","","");w.type=muse::Task::local_cancel;w.id=r.local_job.id;assert(!send(worker,w).rc);
  behavior.store(1);w=muse::Work{};w.type=muse::Task::local_create;w.text="Long local app";r=send(worker,w);assert(!r.rc);
  w.id=r.local_job.id;w.revision=r.local_job.revision;w.type=muse::Task::local_approve;assert(!send(worker,w).rc);
  w.type=muse::Task::local_step;assert(worker.submit(w));usleep(20000);worker.request_pause();r=wait(worker);
  assert(!r.rc&&r.local_job.state==MUSE_LOCAL_PAUSED&&r.local_job.candidate_sha[0]&&!r.local_job.validated_sha[0]);
  w.type=muse::Task::local_resume;assert(!send(worker,w).rc);w.type=muse::Task::local_step;assert(worker.submit(w));usleep(20000);worker.request_pause(true);r=wait(worker);
  assert(!r.rc&&r.local_job.state==MUSE_LOCAL_CANCELLED);
  w.type=muse::Task::local_resume;assert(send(worker,w).rc);
  w.type=muse::Task::configure;w.configuration=cfg;r=send(worker,w);assert(!r.rc&&r.configuration_applied&&!r.needs_init);
  w.type=muse::Task::local_list;r=send(worker,w);assert(!r.rc&&r.local_jobs.size()==3);
  // No optional coder and an incompatible selected coder both preserve voice initialization.
  w.type=muse::Task::configure;w.configuration=cfg;w.configuration.coding_model_path[0]=0;
  r=send(worker,w);assert(!r.rc&&r.needs_init);
  w.type=muse::Task::init;r=send(worker,w);assert(!r.rc&&!r.coding_model_unavailable);
  w.type=muse::Task::local_create;w.text="No coder";r=send(worker,w);assert(r.rc&&r.error.find("coding model missing")!=std::string::npos);
  w.type=muse::Task::configure;w.configuration=cfg;strcpy(w.configuration.coding_model_path,"/models/bad-coder.gguf");
  r=send(worker,w);assert(!r.rc&&r.needs_init);
  w.type=muse::Task::init;r=send(worker,w);assert(!r.rc&&r.coding_model_unavailable&&r.error=="Unsupported model fixture");
  assert(voice_only_starts==2);
  test_enable_model_network(true);
  w.type=muse::Task::model_download;w.text="https://huggingface.co/fixture/model/resolve/main/coder.gguf";
  r=send(worker,w);assert(!r.rc&&!r.path.empty());
  w.type=muse::Task::configure;w.configuration=cfg;
  snprintf(w.configuration.coding_model_path,sizeof w.configuration.coding_model_path,"%s",r.path.c_str());
  r=send(worker,w);assert(!r.rc&&r.needs_init);
  w.type=muse::Task::init;r=send(worker,w);assert(!r.rc&&!r.coding_model_unavailable);
  test_enable_model_network(false);
  w.type=muse::Task::configure;w.configuration=cfg;strcpy(w.configuration.log_dir,"/proc/muse-cannot-create");r=send(worker,w);assert(r.rc&&r.configuration_applied);
  w.type=muse::Task::stop;assert(!send(worker,w).rc);worker.join();
  private_file=fopen(private_spool.c_str(),"rb");assert(private_file);char first_line[128]{};
  assert(fgets(first_line,sizeof first_line,private_file)&&strstr(first_line,"retained-private-before-upgrade"));assert(!fclose(private_file));
  puts("worker local plan/checkpoint approval, actual Lua repair/validation, periodic partial save, pause/cancel and idle reconfiguration passed");
}

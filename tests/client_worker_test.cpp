// SPDX-License-Identifier: GPL-3.0-or-later
// Actual native worker integration, with no inference engine or PS5 microphone.
#include "worker.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
extern "C" int sceUserServiceGetInitialUser(int *) { return -1; }
static muse::Result await_result(muse::Worker &worker) {
    for (int i = 0; i < 1000; ++i) {
        muse::Result result;
        if (worker.take(result)) return result;
        usleep(5000);
    }
    std::fputs("worker result timed out\n", stderr);
    std::abort();
}
int main(int argc, char **argv) {
    assert(argc == 3);
    muse_config config{};
    muse_config_defaults(&config);strcpy(config.relay_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");strcpy(config.build_mode,"hybrid");
    std::snprintf(config.server_url, sizeof(config.server_url), "http://127.0.0.1:%s", argv[1]);
    std::snprintf(config.artifact_dir, sizeof(config.artifact_dir), "%s", argv[2]);
    std::snprintf(config.log_dir, sizeof(config.log_dir), "%s/logs", argv[2]);
    assert(!mkdir(config.log_dir,0700));
    const std::string helper_dir=std::string(config.log_dir)+"/installer";
    assert(!mkdir(helper_dir.c_str(),0700));
    FILE *old=fopen((helper_dir+"/pending.jsonl").c_str(),"wb");assert(old);
    fputs("{\"ts\":\"fixture\",\"level\":\"info\",\"event\":\"stale_installer\",\"detail\":\"previous process\"}\n",old);assert(!fclose(old));
    old=fopen((helper_dir+"/pending.jsonl.active").c_str(),"wb");assert(old);fputs("1",old);assert(!fclose(old));
    muse::Worker worker;
    assert(worker.start(config));
    muse::Work work;
    work.type = muse::Task::speak;
    work.generation = 12;
    work.text = "The plan is ready.";
    work.path = "/replies/p1.wav";
    assert(worker.submit(work));
    auto result = await_result(worker);
    assert(result.rc<0&&result.error.find("Previous installer logs")!=std::string::npos);
    struct muse_client control;muse_client_init(&control,&config);unsigned char *response=nullptr;size_t length=0;
    assert(!muse_client_http(&control,config.server_url,"POST","/fixture/unblock-installer","application/json","{}",2,1024,&response,&length));free(response);
    assert(worker.submit(work));result=await_result(worker);
    if (result.rc) std::fprintf(stderr, "speech fallback: %s\n", result.error.c_str());
    assert(result.rc == 0 && result.work.generation == 12);
    struct stat info{};
    assert(!result.path.empty() && !stat(result.path.c_str(), &info) && info.st_size == 3244);
    for (auto action : {muse::Task::download, muse::Task::dispatch, muse::Task::install_start}) {
        work.type = action;
        work.id = "unapproved-job";
        work.path = "/tmp/unapproved.pkg";
        assert(worker.submit(work));
        result = await_result(worker);
        assert(result.rc && result.error.find("approved") != std::string::npos);
    }
    work.type = muse::Task::stop;
    assert(worker.submit(work));
    result = await_result(worker);
    assert(result.work.type == muse::Task::stop);
    worker.join();
    std::string marker = std::string(config.log_dir) + "/pending.jsonl.active";
    assert(access(marker.c_str(), F_OK) != 0);
    std::puts("actual native worker server-WAV fallback, unapproved artifact rejection and clean shutdown passed");
}

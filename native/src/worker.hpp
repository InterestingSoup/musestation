// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
extern "C" {
#include "client.h"
#include "client_install.h"
#include "gate.h"
#include "log.h"
#include "local_job.h"
}
#include "model_download.hpp"
#include <atomic>
#include <pthread.h>
#include <string>
#include <utility>
#include <vector>
namespace muse {
enum class Task {
  init,
  record,
  phone,
  decision,
  transcript,
  plan_poll,
  plan_recover,
  plan_cancel,
  approve,
  poll,
  download,
  dispatch,
  install_start,
  install_poll,
  launch,
  speak,
  flush,
  retry,
  configure,
  model_download,
  local_create,
  local_load,
  local_list,
  local_approve,
  local_step,
  local_resume,
  local_pause,
  local_cancel,
  stop
};
struct Work {
  Task type = Task::init;
  uint64_t generation = 0;
  std::string id, revision, text, path;
  std::string title, plan, parent_plan;
  unsigned max_attempts = 4, max_seconds = 7200;
  muse_config configuration{};
  muse_build_status manifest{};
};
struct Result {
  Work work;
  int rc = 0;
  int http_status = 0;
  std::string error, path, job, intent, transcript, backend, speech_backend;
  muse_plan plan{};
  muse_build_status status{};
  muse_install_status install{};
  muse_local_job local_job{};
  std::vector<muse_local_job> local_jobs;
  int speech_chunks = 0;  // sentences already handed over for early playback
  bool coding_model_unavailable = false;
  bool needs_init = false, configuration_applied = false, plan_retryable = false;
};
class Worker {
public:
  bool start(const muse_config &);
  bool submit(const Work &);
  bool take(Result &);
  bool busy();
  /* Independent of the mailbox; observed by inference and validation callbacks. */
  void request_pause(bool cancel = false) { local_stop.store(cancel ? 2 : 1); }
  void set_context(const std::string &id, const std::string &revision,
                   const std::string &text);
  void join() { pthread_join(thread, nullptr); }
  std::atomic<uint64_t> phone_generation{0};
  static int phone_submit(void *context, const char *path);
  ModelTransfer model_transfer;
  // Neural speech sentences ready for early playback (filled on the worker
  // thread by the engine callback, drained by the UI thread).
  static void speech_chunk_ready(void *context, const char *path, int index);
  bool take_speech_chunk(uint64_t &generation, std::string &path);
  std::atomic<uint64_t> speech_generation{0};
  std::atomic<int> speech_chunks_streamed{0};
  // Folder install progress for the UI (written by the worker thread).
  std::atomic<uint64_t> install_done{0}, install_total{0};
  std::string install_stage();
  std::atomic<bool> recording{false};
  std::atomic<float> meter{0};

private:
  pthread_t thread{};
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t speech_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t stage_mutex = PTHREAD_MUTEX_INITIALIZER;
  std::string stage_text;
  static void folder_progress(void *context, uint64_t done, uint64_t total, const char *stage);
  std::vector<std::pair<uint64_t, std::string>> speech_ready;
  pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
  bool pending = false, running = false, ready = false;
  std::string context_id, context_revision, context_text;
  Work request;
  Result result;
  muse_client client{};
  muse_log journal{};
  void *engine = nullptr;
  muse_install *installer = nullptr;
  muse_local_store projects{};
  muse_local_job current_job{};
  bool projects_open = false;
  std::atomic<int> local_stop{0};
  static int stopped(void *p) { return static_cast<Worker *>(p)->local_stop.load() != 0; }
  int open_projects();
  void create_local(const Work &, Result &, const char *request, const char *plan);
  void local_action(const Work &, Result &);
  uint64_t approved_generation = 0, downloaded_generation = 0;
  std::string approved_job, downloaded_path;
  muse_build_status downloaded_manifest{};
  static void *entry(void *);
  void loop();
};
} // namespace muse

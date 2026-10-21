// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
namespace muse {
constexpr uint64_t model_download_limit = 1536ull * 1024 * 1024;
struct ModelTransfer {
  std::atomic<bool> cancel{false};
  std::atomic<uint64_t> received{0}, total{0};
};
struct ModelResponse {
  int status = 0;
  uint64_t length = 0, linked_size = 0;
  std::string location, sha, error;
};
using ModelSink = std::function<bool(const char *, size_t)>;
// One HTTPS request, no implicit redirects, no credentials. Implemented by PS5 transport.
ModelResponse model_https_once(const std::string &, bool headers_only, ModelTransfer &, const ModelSink &);
bool model_read_headers(const std::string &, ModelResponse &);
bool model_hf_url(const std::string &, std::string &normalized);
bool model_download_host(const std::string &);
const char *recommended_model_url();
// Download into persistent storage, verify exact bytes and GGUF metadata, then rename.
// Existing selected models are never overwritten. Returns a content-addressed path.
bool download_coding_model(const std::string &url, const std::string &directory,
                          ModelTransfer &, std::string &path, std::string &error);
bool model_gguf_compatible(const std::string &path, std::string &error);
}

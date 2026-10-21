// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary work must never call model HTTP; Local setup tests explicitly opt in.
#include "model_download.hpp"
extern "C" {
#include "client_sha256.h"
}
#include <cassert>
static bool enabled=false;
void test_enable_model_network(bool value){enabled=value;}
static void number(std::string &s,uint64_t n,unsigned count){for(unsigned i=0;i<count;i++)s+=char(n>>(i*8));}
static void text(std::string &s,const std::string &v){number(s,v.size(),8);s+=v;}
namespace muse {
ModelResponse model_https_once(const std::string &url,bool metadata,ModelTransfer &,const ModelSink &sink) {
  assert(enabled && "Ordinary local/hybrid work must not initiate model downloads");
  std::string body="GGUF";number(body,3,4);number(body,1,8);number(body,2,8);
  text(body,"general.architecture");number(body,8,4);text(body,"qwen2");
  text(body,"tokenizer.chat_template");number(body,8,4);text(body,"<|im_start|>user");
  body+=std::string(64,'x');
  ModelResponse out;
  if(metadata){char digest[65];muse_sha256_hex(body.data(),body.size(),digest);out.sha=digest;out.linked_size=body.size();out.status=302;}
  else if(url.find("huggingface.co/")!=std::string::npos){out.status=302;out.location="https://us.aws.cdn.hf.co/fixture";}
  else {out.status=200;if(!sink(body.data(),body.size()))out.error="fixture sink failed";}
  return out;
}
}

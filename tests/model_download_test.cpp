// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic network and GGUF fixtures; no console, internet, or inference claims.
#include "model_download.hpp"
extern "C" {
#include "client_sha256.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <unistd.h>
using namespace muse;
static std::string body,hash;
static int scenario=0;
static void integer(std::string &s,uint64_t n,unsigned bytes){for(unsigned i=0;i<bytes;i++)s+=char(n>>(8*i));}
static void str(std::string &s,const std::string &v){integer(s,v.size(),8);s+=v;}
static std::string fixture(const char *arch="qwen2"){
 std::string s="GGUF";integer(s,3,4);integer(s,1,8);integer(s,3,8);
 str(s,"general.architecture");integer(s,8,4);str(s,arch);
 str(s,"tokenizer.chat_template");integer(s,8,4);str(s,"<|im_start|>user");
 str(s,"tokenizer.ggml.tokens");integer(s,9,4);integer(s,8,4);integer(s,2,8);str(s,"hello");str(s,"world");
 s+=std::string(64,'x');return s;
}
static void rehash(){char hex[65];muse_sha256_hex(body.data(),body.size(),hex);hash=hex;}
namespace muse {
ModelResponse model_https_once(const std::string &url,bool headers,ModelTransfer &control,const ModelSink &sink){
 ModelResponse r;
 if(headers){r.status=302;r.linked_size=scenario==4?model_download_limit+1:body.size();r.sha=hash;return r;}
 if(url.find("huggingface.co/")!=std::string::npos){r.status=302;r.location=scenario==5?"http://127.0.0.1/private":"https://cas-bridge.xethub.hf.co/file";return r;}
 r.status=200;std::string bytes=body;
 if(scenario==1)bytes.back()^=1;
 if(scenario==2)bytes.pop_back();
 if(scenario==3)bytes+='x';
 if(scenario==6)control.cancel=true;
 if(!sink(bytes.data(),bytes.size()))r.error="sink stopped";
 return r;
}
}
int main(int argc,char **argv){
 if(argc==2){std::string error;bool ok=model_gguf_compatible(argv[1],error);if(!ok)fprintf(stderr,"%s\n",error.c_str());assert(ok);puts("Real recommended GGUF metadata accepted.");return 0;}
 std::string normalized,error,path;
 assert(model_hf_url("https://huggingface.co/owner/repo/blob/main/model.gguf",normalized));
 assert(normalized=="https://huggingface.co/owner/repo/resolve/main/model.gguf");
 for(const char *url:{"http://huggingface.co/o/r/resolve/main/f.gguf","https://huggingface.co/o/r","https://huggingface.co.evil/o/r/resolve/main/f.gguf","https://huggingface.co@evil/o/r/resolve/main/f.gguf","https://huggingface.co/o/r/resolve/main/file.safetensors","https://huggingface.co/o/r/resolve/main/../f.gguf","https://huggingface.co/o/r/resolve/main/f.gguf#secret"})assert(!model_hf_url(url,normalized));
 assert(model_download_host("https://cas-bridge.xethub.hf.co/file?signature=abc"));
 assert(!model_download_host("https://evil-hf.co/file"));
 assert(!model_download_host("https://cas-bridge.xethub.hf.co:80/file"));
 ModelResponse r;
 assert(model_read_headers("HTTP/1.1 302 Found\r\nX-Linked-Size: 1234\r\nX-Linked-Etag: \""+std::string(64,'a')+"\"\r\nLocation: https://cdn-lfs.hf.co/file\r\n",r));
 assert(r.linked_size==1234&&r.sha==std::string(64,'a'));
 for(const char *raw:{"Content-Length: 5\nContent-Length: 6\n","X-Linked-Size: -1\n","X-Linked-Size: 18446744073709551616\n","X-Linked-Etag: weak\n","Content-Encoding: gzip\n"}){ModelResponse bad;assert(!model_read_headers(raw,bad));}
 char tmp[]="/tmp/muse-model-test-XXXXXX";assert(mkdtemp(tmp));std::string dir=std::string(tmp)+"/models";
 body=fixture();rehash();ModelTransfer control;
 assert(download_coding_model("https://huggingface.co/o/r/resolve/main/f.gguf",dir,control,path,error));
 assert(path==dir+"/"+hash+".gguf"&&control.received==body.size());
 std::string previous=path;
 for(scenario=1;scenario<=6;scenario++){
  control.cancel=false;
  assert(!download_coding_model("https://huggingface.co/o/r/resolve/main/f.gguf",dir,control,path,error));
  assert(path.empty()&&!error.empty()&&!std::filesystem::exists(previous+".part"));
  std::ifstream f(previous,std::ios::binary);std::string retained((std::istreambuf_iterator<char>(f)),{});assert(retained==body);
 }
 scenario=0;control.cancel=false;body=fixture("llama");rehash();
 assert(!download_coding_model("https://huggingface.co/o/r/resolve/main/f.gguf",dir,control,path,error));
 assert(error.find("unsupported")!=std::string::npos);
 body="not a model";rehash();assert(!download_coding_model("https://huggingface.co/o/r/resolve/main/f.gguf",dir,control,path,error));
 body=fixture();body[16]=char(0xff);body[17]=char(0xff);rehash();
 assert(!download_coding_model("https://huggingface.co/o/r/resolve/main/f.gguf",dir,control,path,error));
 std::filesystem::remove_all(tmp);
 puts("Model URLs, redirect policy, bounded headers/GGUF, streaming hashes, truncation, cancellation and previous-model preservation passed.");
}

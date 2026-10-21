// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_download.hpp"
extern "C" {
#include "client_sha256.h"
}
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace muse {
namespace {
constexpr const char *recommended = "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf";
constexpr const char *recommended_sha = "cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046";
constexpr uint64_t recommended_bytes = 1117320768;
bool hex(const std::string &s) {
  return s.size()==64 && s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
bool number(const std::string &s,uint64_t &n) {
  n=0; if(s.empty())return false;
  for(char c:s) { if(c<'0'||c>'9'||n>(UINT64_MAX-9)/10)return false; n=n*10+unsigned(c-'0'); }
  return true;
}
std::string host(const std::string &url) {
  if(url.size()>4096 || url.compare(0,8,"https://"))return {};
  for(unsigned char c:url)if(c<=32||c>=127||c=='\\'||c=='#')return {};
  size_t end=url.find('/',8); if(end==std::string::npos)return {};
  std::string h=url.substr(8,end-8);
  if(h.empty()||h.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=std::string::npos)return {};
  return h;
}
bool suffix(const std::string &s,const char *tail) {
  size_t n=std::strlen(tail);return s.size()>n && s.compare(s.size()-n,n,tail)==0;
}
struct Reader {
  FILE *f; uint64_t used=0, size;
  bool take(void *p,size_t n) {
    if(n>16*1024*1024-used || n>size-used)return false;
    used+=n;return fread(p,1,n,f)==n;
  }
  bool integer(uint64_t &v,unsigned n) {
    unsigned char b[8];if(!take(b,n))return false;v=0;
    for(unsigned i=0;i<n;i++)v|=uint64_t(b[i])<<(8*i);return true;
  }
  bool string(std::string &s) {
    uint64_t n;if(!integer(n,8)||n>65536)return false;s.resize(size_t(n));return take(s.data(),size_t(n));
  }
  bool skip(uint64_t n) {
    if(n>16*1024*1024-used||n>size-used)return false;
    used+=n;return fseek(f,long(n),SEEK_CUR)==0;
  }
  bool value(uint64_t type,bool array=false) {
    static const unsigned widths[]={1,1,2,2,4,4,4,1,0,0,8,8,8};
    if(type>12)return false;
    if(type==8){std::string s;return string(s);}
    if(type==9){
      uint64_t subtype,count;if(array||!integer(subtype,4)||!integer(count,8)||count>1000000||subtype==9||subtype>12)return false;
      if(subtype!=8)return skip(count*widths[subtype]);
      while(count--)if(!value(subtype,true))return false;return true;
    }
    return skip(widths[type]);
  }
};
}
const char *recommended_model_url(){return recommended;}
bool model_download_host(const std::string &url) {
  auto h=host(url);return h=="huggingface.co" || suffix(h,".huggingface.co") || suffix(h,".hf.co");
}
bool model_hf_url(const std::string &url,std::string &normalized) {
  if(host(url)!="huggingface.co")return false;
  // Exactly owner/repository/{resolve,blob}/revision/file.
  std::string path=url.substr(std::strlen("https://huggingface.co/"));
  const auto first=path.find('/'),second=first==std::string::npos?first:path.find('/',first+1);
  if(first==0||first==std::string::npos||second==std::string::npos||second==first+1)return false;
  std::string tail=path.substr(second+1);
  if(tail.compare(0,5,"blob/")==0)tail.replace(0,5,"resolve/");
  if(tail.compare(0,8,"resolve/"))return false;
  auto rev=tail.find('/',8);if(rev==std::string::npos||rev==8)return false;
  std::string file=tail.substr(rev+1);auto query=file.find('?');if(query!=std::string::npos)file.resize(query);
  if(file.size()<6||file.compare(file.size()-5,5,".gguf")||file.find("..")!=std::string::npos)return false;
  normalized="https://huggingface.co/"+path.substr(0,second+1)+tail;return true;
}
bool model_read_headers(const std::string &raw,ModelResponse &out) {
  if(raw.size()>65536)return false;
  bool length=false,linked=false,sha=false,location=false;
  size_t at=0;
  while(at<raw.size()){
    auto end=raw.find('\n',at);if(end==std::string::npos)end=raw.size();
    auto line=raw.substr(at,end-at);at=end+1;
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    auto colon=line.find(':');if(colon==std::string::npos)continue;
    auto key=line.substr(0,colon),value=line.substr(colon+1);
    std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);});
    auto start=value.find_first_not_of(" \t");value=start==std::string::npos?"":value.substr(start);
    while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.pop_back();
    if(key=="location"){if(location)return false;location=true;out.location=value;}
    if(key=="content-length"){if(length||!number(value,out.length))return false;length=true;}
    if(key=="x-linked-size"){if(linked||!number(value,out.linked_size))return false;linked=true;}
    if(key=="x-linked-etag"){
      if(sha)return false;sha=true;
      if(value.size()==66&&value.front()=='"'&&value.back()=='"')value=value.substr(1,64);
      if(!hex(value))return false;out.sha=value;
    }
    if(key=="content-encoding"&&value!="identity")return false;
  }
  return true;
}
bool model_gguf_compatible(const std::string &path,std::string &error) {
  FILE *f=fopen(path.c_str(),"rb");if(!f){error="Cannot inspect downloaded model.";return false;}
  struct stat st{}; bool ok=fstat(fileno(f),&st)==0&&S_ISREG(st.st_mode)&&st.st_size>24&&uint64_t(st.st_size)<=model_download_limit;
  Reader r{f,0,ok?uint64_t(st.st_size):0};char magic[4];uint64_t version=0,tensors=0,keys=0;
  ok=ok&&r.take(magic,4)&&!memcmp(magic,"GGUF",4)&&r.integer(version,4)&&version==3&&r.integer(tensors,8)&&tensors>0&&tensors<=4096&&r.integer(keys,8)&&keys<=4096;
  std::string architecture,chat;
  bool split=false;
  for(uint64_t i=0;ok&&i<keys;i++){
    std::string key;uint64_t type;ok=r.string(key)&&key.size()<=256&&r.integer(type,4);if(!ok)break;
    if(key=="general.architecture"||key=="tokenizer.chat_template") {
      std::string &target=key=="general.architecture"?architecture:chat;
      ok=target.empty()&&type==8&&r.string(target);
    } else if(key=="split.count") {
      uint64_t count=0;ok=!split&&type==2&&r.integer(count,2)&&count==1;split=true;
    } else ok=r.value(type);
  }
  fclose(f);
  ok=ok&&(architecture=="qwen2"||architecture=="qwen3")&&chat.find("<|im_start|>")!=std::string::npos;
  if(!ok)error="Use a single GGUF v3 Qwen2/Qwen3 text model with ChatML, at most 1.5 GiB. Split, vision and other architectures are unsupported.";
  return ok;
}
bool download_coding_model(const std::string &input,const std::string &dir,ModelTransfer &control,std::string &path,std::string &error) {
  path.clear();error.clear();std::string url;
  if(!model_hf_url(input,url)){error="Enter a public Hugging Face GGUF file URL (resolve or blob), not a repository page.";return false;}
  control.received=0;control.total=0;
  auto metadata=model_https_once(url,true,control,{});
  if(!metadata.error.empty()){error=metadata.error;return false;}
  uint64_t expected=metadata.linked_size;std::string expected_sha=metadata.sha;
  if(url==recommended){expected=recommended_bytes;expected_sha=recommended_sha;}
  if(metadata.status!=200&&metadata.status!=302&&metadata.status!=307){error="Hugging Face refused this file. Use a public, ungated model.";return false;}
  if(!hex(expected_sha)||expected<24||expected>model_download_limit){error="Missing trusted Hugging Face SHA-256/size metadata, or model exceeds 1.5 GiB.";return false;}
  control.total=expected;
  if(dir.empty()||dir[0]!='/'||dir.size()>400){error="Invalid model storage directory.";return false;}
  struct stat st{};
  if(mkdir(dir.c_str(),0700)&&errno!=EEXIST){error="Cannot create persistent model storage.";return false;}
  if(lstat(dir.c_str(),&st)||!S_ISDIR(st.st_mode)){error="Model storage must be a real directory.";return false;}
  const std::string final=dir+"/"+expected_sha+".gguf",partial=final+".part";
  // A failed/killed download never becomes the selected model. Serial worker owns this directory.
  unlink(partial.c_str());
  int fd=open(partial.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if(fd<0){error="Cannot create model download. Check storage space and permissions.";return false;}
  muse_sha256 hash;muse_sha256_init(&hash);
  ModelSink sink=[&](const char *bytes,size_t n){
    uint64_t got=control.received.load();if(control.cancel||n>expected-got)return false;
    size_t at=0;while(at<n){ssize_t w=write(fd,bytes+at,n-at);if(w<0&&errno==EINTR)continue;if(w<=0)return false;at+=size_t(w);}
    muse_sha256_update(&hash,bytes,n);control.received=got+n;return true;
  };
  ModelResponse response;
  for(unsigned redirects=0;redirects<=5;redirects++){
    if(control.cancel){error="Model download cancelled.";break;}
    if(!model_download_host(url)){error="Download redirect leaves Hugging Face's HTTPS hosts.";break;}
    response=model_https_once(url,false,control,sink);
    if(!response.error.empty()){error=response.error;break;}
    if(response.status==301||response.status==302||response.status==303||response.status==307||response.status==308){url=response.location;continue;}
    if(response.status!=200)error="Model download did not return HTTP 200.";
    break;
  }
  char sha[65];muse_sha256_final(&hash,sha);
  bool ok=error.empty()&&!control.cancel&&response.status==200&&control.received==expected&&expected_sha==sha;
  if(!ok&&error.empty())error=control.cancel?"Model download cancelled.":"Incomplete model or SHA-256 mismatch. The previous model is unchanged.";
  if(ok&&fsync(fd)){ok=false;error="Cannot flush model to storage.";}
  if(close(fd)&&ok){ok=false;error="Cannot close model download.";}
  if(ok)ok=model_gguf_compatible(partial,error);
  if(ok&&rename(partial.c_str(),final.c_str())){ok=false;error="Cannot commit downloaded model.";}
  if(!ok)unlink(partial.c_str());else path=final;
  return ok;
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real native transport with platform API fixtures, not a live PS5.
#include "model_download.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
static int status=200,sends=0,reads=0,deleted=0,fail_tls=0;
static std::string headers="Content-Length: 3\r\n";
extern "C" {
int sceNetPoolCreate(const char *,int,int){return 1;}
int sceSslInit(size_t){return 2;}
int sceHttpInit(int,int,size_t){return 3;}
int sceHttpCreateTemplate(int,const char *,int,int){return 4;}
int sceHttpSetAutoRedirect(int,int enabled){assert(!enabled);return 0;}
int sceHttpSetResolveTimeOut(int,uint32_t){return 0;}
int sceHttpSetConnectTimeOut(int,uint32_t){return 0;}
int sceHttpSetSendTimeOut(int,uint32_t){return 0;}
int sceHttpSetRecvTimeOut(int,uint32_t){return 0;}
int sceHttpsEnableOption(int,uint32_t flags){assert(flags==(0x01|0x04|0x08|0x10|0x20|0x80));return fail_tls?-1:0;}
int sceHttpCreateConnectionWithURL(int,const char *,int){return 5;}
int sceHttpCreateRequestWithURL(int,int,const char *,uint64_t){return 6;}
int sceHttpAddRequestHeader(int,const char *key,const char *value,uint32_t){assert(!strcmp(key,"Accept-Encoding")&&!strcmp(value,"identity"));return 0;}
int sceHttpSendRequest(int,const void *,size_t){++sends;return 0;}
int sceHttpGetStatusCode(int,int *out){*out=status;return 0;}
int sceHttpGetAllResponseHeaders(int,char **out,size_t *size){*out=headers.data();*size=headers.size();return 0;}
int sceHttpReadData(int,void *out,size_t){if(reads++)return 0;memcpy(out,"abc",3);return 3;}
int sceHttpDeleteRequest(int){++deleted;return 0;}
int sceHttpDeleteConnection(int){++deleted;return 0;}
int sceHttpDeleteTemplate(int){++deleted;return 0;}
int sceHttpTerm(int){++deleted;return 0;}
int sceSslTerm(int){++deleted;return 0;}
int sceNetPoolDestroy(int){++deleted;return 0;}
}
int main(){
 muse::ModelTransfer control;std::string body;
 auto sink=[&](const char *p,size_t n){body.append(p,n);return true;};
 auto r=muse::model_https_once("https://huggingface.co/o/r/resolve/main/f.gguf",false,control,sink);
 assert(r.error.empty()&&r.status==200&&body=="abc"&&sends==1&&deleted==6);
 status=302;headers="Location: https://us.aws.cdn.hf.co/file\r\n";reads=0;
 r=muse::model_https_once("https://huggingface.co/o/r/resolve/main/f.gguf",true,control,sink);
 assert(r.error.empty()&&r.status==302&&reads==0&&deleted==12);
 fail_tls=1;int sent=sends;
 r=muse::model_https_once("https://huggingface.co/o/r/resolve/main/f.gguf",false,control,sink);
 assert(!r.error.empty()&&sends==sent&&deleted==16);
 fail_tls=0;status=200;headers="Content-Length: 3\r\nContent-Length: 4\r\n";
 r=muse::model_https_once("https://huggingface.co/o/r/resolve/main/f.gguf",false,control,sink);
 assert(!r.error.empty()&&reads==0);
 control.cancel=true;sent=sends;
 r=muse::model_https_once("https://huggingface.co/o/r/resolve/main/f.gguf",false,control,sink);
 assert(r.error=="Model download cancelled."&&sent==sends);
 puts("Native HTTPS fixtures: TLS options fail closed, redirects explicit, metadata does not stream bodies, cancellation and resource cleanup passed.");
}

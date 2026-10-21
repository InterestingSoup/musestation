// SPDX-License-Identifier: GPL-3.0-or-later
// Native HTTPS API setup adapted from ProsperoStore, Copyright (C) 2026
// BlackBearReloaded, revision 82ea4a232ce3074893d9a61725d1b9f8706c6556.
// https://github.com/blackbearreloaded/ProsperoStore/blob/82ea4a232ce3074893d9a61725d1b9f8706c6556/src/platform/ps5/store_http.cpp
#include "model_download.hpp"
#include <cstdio>
#include <vector>
extern "C" {
int sceNetPoolCreate(const char *,int,int);
int sceNetPoolDestroy(int);
int sceSslInit(size_t);
int sceSslTerm(int);
int sceHttpInit(int,int,size_t);
int sceHttpTerm(int);
int sceHttpCreateTemplate(int,const char *,int,int);
int sceHttpDeleteTemplate(int);
int sceHttpSetAutoRedirect(int,int);
int sceHttpSetResolveTimeOut(int,uint32_t);
int sceHttpSetConnectTimeOut(int,uint32_t);
int sceHttpSetSendTimeOut(int,uint32_t);
int sceHttpSetRecvTimeOut(int,uint32_t);
int sceHttpsEnableOption(int,uint32_t);
int sceHttpCreateConnectionWithURL(int,const char *,int);
int sceHttpDeleteConnection(int);
int sceHttpCreateRequestWithURL(int,int,const char *,uint64_t);
int sceHttpDeleteRequest(int);
int sceHttpAddRequestHeader(int,const char *,const char *,uint32_t);
int sceHttpSendRequest(int,const void *,size_t);
int sceHttpGetStatusCode(int,int *);
int sceHttpGetAllResponseHeaders(int,char **,size_t *);
int sceHttpReadData(int,void *,size_t);
}
namespace muse {
ModelResponse model_https_once(const std::string &url,bool headers_only,ModelTransfer &control,const ModelSink &sink) {
  ModelResponse out;
  if(!model_download_host(url)){out.error="Only Hugging Face HTTPS download hosts are allowed.";return out;}
  struct Resources {
    int pool=-1,ssl=-1,http=-1,tmpl=-1,connection=-1,request=-1;
    ~Resources(){
      if(request>=0)sceHttpDeleteRequest(request);
      if(connection>=0)sceHttpDeleteConnection(connection);
      if(tmpl>=0)sceHttpDeleteTemplate(tmpl);
      if(http>=0)sceHttpTerm(http);
      if(ssl>=0)sceSslTerm(ssl);
      if(pool>=0)sceNetPoolDestroy(pool);
    }
  } r;
  int rc=r.pool=sceNetPoolCreate("MuseStationModels",1024*1024,0);
  if(rc>=0)rc=r.ssl=sceSslInit(2*1024*1024);
  if(rc>=0)rc=r.http=sceHttpInit(r.pool,r.ssl,4*1024*1024);
  if(rc>=0)rc=r.tmpl=sceHttpCreateTemplate(r.http,"MuseStation/01.006.000",2,0);
  if(rc>=0)rc=sceHttpSetAutoRedirect(r.tmpl,0);
  if(rc>=0)rc=sceHttpSetResolveTimeOut(r.tmpl,20000000);
  if(rc>=0)rc=sceHttpSetConnectTimeOut(r.tmpl,20000000);
  if(rc>=0)rc=sceHttpSetSendTimeOut(r.tmpl,20000000);
  if(rc>=0)rc=sceHttpSetRecvTimeOut(r.tmpl,20000000);
  // Verify CA, hostname, dates and signatures. Never disable TLS verification.
  if(rc>=0)rc=sceHttpsEnableOption(r.tmpl,0x01|0x04|0x08|0x10|0x20|0x80);
  if(rc>=0)rc=r.connection=sceHttpCreateConnectionWithURL(r.tmpl,url.c_str(),0);
  if(rc>=0)rc=r.request=sceHttpCreateRequestWithURL(r.connection,0,url.c_str(),0);
  if(rc>=0)rc=sceHttpAddRequestHeader(r.request,"Accept-Encoding","identity",0);
  if(rc>=0&&!control.cancel)rc=sceHttpSendRequest(r.request,nullptr,0);
  if(rc>=0&&!control.cancel)rc=sceHttpGetStatusCode(r.request,&out.status);
  char *headers=nullptr;size_t size=0;
  if(rc>=0&&!control.cancel)rc=sceHttpGetAllResponseHeaders(r.request,&headers,&size);
  if(rc>=0&&!control.cancel&&(!headers||size>65536||!model_read_headers(std::string(headers,size),out))) {
    out.error="Invalid or oversized model download headers.";return out;
  }
  if(out.length>model_download_limit && out.status==200){out.error="Model exceeds the 1.5 GiB download limit.";return out;}
  std::vector<char> bytes(64*1024);uint64_t received=0;
  while(rc>=0&&out.status==200&&!headers_only&&!control.cancel) {
    int n=sceHttpReadData(r.request,bytes.data(),bytes.size());
    if(!n)break;
    if(n<0){rc=n;break;}
    if(uint64_t(n)>model_download_limit-received||!sink||!sink(bytes.data(),size_t(n))){out.error="Download stopped: cancelled, insufficient storage, or unexpected length.";break;}
    received+=unsigned(n);
  }
  if(control.cancel)out.error="Model download cancelled.";
  else if(rc<0){char message[180];std::snprintf(message,sizeof message,"Console HTTPS failed (0x%08x). Check internet access, console time and certificate availability.",unsigned(rc));out.error=message;}
  return out;
}
}

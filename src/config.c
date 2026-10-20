/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return -1;}
static int path_ok(const char *s,size_t cap){
    size_t len=strnlen(s,cap);if(!len||len==cap||s[0]!='/')return 0;
    for(size_t i=0;i<len;i++)if((unsigned char)s[i]<32)return 0;
    const char *p=s;while(*p){while(*p=='/')p++;const char *a=p;while(*p&&*p!='/')p++;if(p-a==2&&a[0]=='.'&&a[1]=='.')return 0;}return 1;
}
static int origin_ok(const char *s,size_t cap){
    if(strnlen(s,cap)==cap||strncmp(s,"http://",7))return 0;
    const char *p=s+7,*start=p;char host[128];
    if(*p=='['){start=++p;while(*p&&*p!=']')p++;if(*p!=']')return 0;}
    else while(*p&&*p!=':'&&*p!='/')p++;
    size_t n=(size_t)(p-start);if(!n||n>=sizeof(host))return 0;memcpy(host,start,n);host[n]=0;
    unsigned char addr[16];if(strcmp(host,"localhost")&&inet_pton(AF_INET,host,addr)!=1&&inet_pton(AF_INET6,host,addr)!=1)return 0;
    if(*p==']')p++;
    if(*p==':'){p++;unsigned port=0;const char *a=p;while(*p>='0'&&*p<='9'){port=port*10+(unsigned)(*p++-'0');if(port>65535)return 0;}if(p==a||!port)return 0;}
    if(*p=='/')p++;return !*p;
}
int muse_config_token_valid(const char *token){
    if(strnlen(token,65)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return 0;
    return 1;
}
int muse_config_private_origin(const char *url){
    if(!url||!origin_ok(url,256))return 0;
    char host[128];const char *a=url+7,*end;
    if(*a=='['){a++;end=strchr(a,']');}else {end=a;while(*end&&*end!=':'&&*end!='/')end++;}
    size_t n=(size_t)(end-a);if(n>=sizeof host)return 0;memcpy(host,a,n);host[n]=0;
    if(!strcmp(host,"localhost"))return 1;
    unsigned char ip[16];
    if(inet_pton(AF_INET,host,ip)==1)return ip[0]==10||ip[0]==127||(ip[0]==172&&(ip[1]>=16&&ip[1]<=31))||(ip[0]==192&&ip[1]==168)||(ip[0]==169&&ip[1]==254);
    if(inet_pton(AF_INET6,host,ip)!=1)return 0;
    static const unsigned char loopback[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    return !memcmp(ip,loopback,16)||(ip[0]&0xfe)==0xfc||(ip[0]==0xfe&&(ip[1]&0xc0)==0x80);
}
int muse_config_console_origin(const struct muse_config *c,const char *url){
    if(!muse_config_private_origin(url))return 0;
    char host[128];const char *a=url+7,*end;if(*a=='['){a++;end=strchr(a,']');}else {end=a;while(*end&&*end!=':'&&*end!='/')end++;}
    size_t n=(size_t)(end-a);if(n>=sizeof host)return 0;memcpy(host,a,n);host[n]=0;
    if(!strcmp(host,"localhost"))return 1;
    unsigned char ip[16],console[16];
    if(inet_pton(AF_INET,host,ip)==1)return ip[0]==127||(inet_pton(AF_INET,c->console_ip,console)==1&&!memcmp(ip,console,4));
    static const unsigned char loopback[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    return inet_pton(AF_INET6,host,ip)==1&&(!memcmp(ip,loopback,16)||(inet_pton(AF_INET6,c->console_ip,console)==1&&!memcmp(ip,console,16)));
}
int muse_config_local(const struct muse_config *c){return !strcmp(c->build_mode,"local");}
void muse_config_defaults(struct muse_config *c){memset(c,0,sizeof(*c));strcpy(c->build_mode,"hybrid");strcpy(c->hybrid_provider,"muse");strcpy(c->shadowmount_url,"http://127.0.0.1:10101");c->timeout_ms=10000;strcpy(c->inference_backend,"auto");strcpy(c->speech_mode,"neural");c->loader_port=9021;c->phone_port=0;strcpy(c->firmware_reported,"12.40");strcpy(c->elfldr_status,"unknown");strcpy(c->etahen_status,"unknown");strcpy(c->kstuff_lite_status,"unknown");strcpy(c->shadowmountplus_status,"unknown");strcpy(c->ftpsrv_status,"unknown");}
int muse_config_validate(const struct muse_config *c,char *e,size_t n){
    unsigned char addr[16];
    if(strcmp(c->build_mode,"local")&&strcmp(c->build_mode,"hybrid"))return fail(e,n,"Build mode must be local or hybrid");
    if(strcmp(c->hybrid_provider,"muse")&&strcmp(c->hybrid_provider,"codex"))return fail(e,n,"Hybrid provider must be muse or codex");
    if(strnlen(c->console_ip,sizeof(c->console_ip))==sizeof(c->console_ip)||
       ((!muse_config_local(c)||*c->console_ip)&&inet_pton(AF_INET,c->console_ip,addr)!=1&&inet_pton(AF_INET6,c->console_ip,addr)!=1))return fail(e,n,"Console IP must be a numeric IPv4 or IPv6 address");
    if(((!muse_config_local(c)||*c->server_url)&&!origin_ok(c->server_url,sizeof(c->server_url)))||(c->ps5lm_url[0]&&!origin_ok(c->ps5lm_url,sizeof(c->ps5lm_url)))||(c->speech_url[0]&&!origin_ok(c->speech_url,sizeof(c->speech_url))))return fail(e,n,"Endpoints require HTTP numeric LAN IP origins (or localhost); HTTPS and DNS names are unsupported");
    if(*c->server_url&&!muse_config_private_origin(c->server_url))return fail(e,n,"Relay HTTP requires a private LAN, loopback, or link-local IP; public origins are blocked");
    if((!muse_config_local(c)||*c->relay_token)&&!muse_config_token_valid(c->relay_token))return fail(e,n,"Enter the relay console token: exactly 64 lowercase hexadecimal characters");
    if((!muse_config_local(c)||*c->shadowmount_url)&&!muse_config_console_origin(c,c->shadowmount_url))return fail(e,n,"ShadowMount URL must point to this console: loopback or its configured LAN IP");
    if(!path_ok(c->log_dir,sizeof(c->log_dir))||!path_ok(c->artifact_dir,sizeof(c->artifact_dir))||!path_ok(c->model_dir,sizeof(c->model_dir))||(*c->audio_in_module&&!path_ok(c->audio_in_module,sizeof(c->audio_in_module))))return fail(e,n,"Paths must be absolute without parent traversal or control characters");
    if(c->install_helper_path[0]&&!path_ok(c->install_helper_path,sizeof(c->install_helper_path)))return fail(e,n,"Install helper path must be absolute without parent traversal");
    if((c->installer_artifact_dir[0]&&!path_ok(c->installer_artifact_dir,sizeof(c->installer_artifact_dir)))||(c->installer_log_dir[0]&&!path_ok(c->installer_log_dir,sizeof(c->installer_log_dir))))return fail(e,n,"Installer path overrides must be absolute without parent traversal");
    if(strnlen(c->model,sizeof(c->model))==sizeof(c->model))return fail(e,n,"Model name too long");
    if(strcmp(c->inference_backend,"auto")&&strcmp(c->inference_backend,"cpu")&&strcmp(c->inference_backend,"vulkan_required"))return fail(e,n,"Inference backend must be auto, cpu, or vulkan_required");
    if(strcmp(c->speech_mode,"neural")&&strcmp(c->speech_mode,"espeak"))return fail(e,n,"Speech mode must be neural or espeak");
    if(strcmp(c->firmware_reported,"12.40"))return fail(e,n,"This build requires reported firmware 12.40");
    if(c->timeout_ms<1000||c->timeout_ms>60000||!c->loader_port)return fail(e,n,"Invalid timeout (1000..60000 ms) or ELF loader port");
    if(e&&n)*e=0;return 0;
}
int muse_config_load(const char *path,struct muse_config *c,char *e,size_t n){
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Configuration unavailable; run setup");
    char data[16385];size_t len=fread(data,1,sizeof(data),f);int read_error=ferror(f);fclose(f);
    if(read_error||len>=sizeof(data))return fail(e,n,"Configuration is unreadable or exceeds 16 KiB");
    struct muse_json j;if(muse_json_parse(&j,data,len)||j.tokens[0].type!='{')return fail(e,n,"Invalid configuration JSON");
    muse_config_defaults(c);
    /* Absence means a pre-MuseStation installation: retain its hybrid behavior. */
    int mode=muse_json_get(&j,0,"build_mode");
    if(mode<0)c->phone_port=8088;
    else if(muse_json_string(&j,mode,c->build_mode,sizeof c->build_mode))return fail(e,n,"Invalid build mode");
    mode=muse_json_get(&j,0,"hybrid_provider");if(mode>=0&&muse_json_string(&j,mode,c->hybrid_provider,sizeof c->hybrid_provider))return fail(e,n,"Invalid hybrid provider");
#define FIELD(name) if(muse_json_string(&j,muse_json_get(&j,0,#name),c->name,sizeof(c->name)))return fail(e,n,"Missing/invalid configuration: " #name)
    FIELD(console_ip);FIELD(server_url);FIELD(ps5lm_url);FIELD(speech_url);FIELD(log_dir);FIELD(artifact_dir);FIELD(model_dir);FIELD(audio_in_module);FIELD(firmware_reported);
#undef FIELD
    int smp=muse_json_get(&j,0,"shadowmount_url");if(smp>=0&&muse_json_string(&j,smp,c->shadowmount_url,sizeof(c->shadowmount_url)))return fail(e,n,"Invalid ShadowMount URL");
    int secret=muse_json_get(&j,0,"relay_token");if(secret>=0&&muse_json_string(&j,secret,c->relay_token,sizeof(c->relay_token)))return fail(e,n,"Invalid relay token");
    int t=muse_json_get(&j,0,"model");if(t>=0&&muse_json_string(&j,t,c->model,sizeof(c->model)))return fail(e,n,"Invalid model");
    t=muse_json_get(&j,0,"install_helper_path");if(t>=0&&muse_json_string(&j,t,c->install_helper_path,sizeof(c->install_helper_path)))return fail(e,n,"Invalid install helper path");
    t=muse_json_get(&j,0,"installer_artifact_dir");if(t>=0&&muse_json_string(&j,t,c->installer_artifact_dir,sizeof(c->installer_artifact_dir)))return fail(e,n,"Invalid installer artifact directory");
    t=muse_json_get(&j,0,"installer_log_dir");if(t>=0&&muse_json_string(&j,t,c->installer_log_dir,sizeof(c->installer_log_dir)))return fail(e,n,"Invalid installer log directory");
    t=muse_json_get(&j,0,"inference_backend");if(t>=0&&muse_json_string(&j,t,c->inference_backend,sizeof(c->inference_backend)))return fail(e,n,"Invalid inference backend");
    t=muse_json_get(&j,0,"speech_mode");if(t>=0&&muse_json_string(&j,t,c->speech_mode,sizeof(c->speech_mode)))return fail(e,n,"Invalid speech mode");
    t=muse_json_get(&j,0,"voice_gate_verified");if(t>=0&&muse_json_bool(&j,t,&c->voice_gate_verified))return fail(e,n,"Invalid voice verification flag");
    uint64_t num;t=muse_json_get(&j,0,"timeout_ms");if(t>=0){if(muse_json_u64(&j,t,&num)||num>60000)return fail(e,n,"Invalid timeout");c->timeout_ms=(int)num;}
    t=muse_json_get(&j,0,"loader_port");if(t>=0){if(muse_json_u64(&j,t,&num)||num>65535)return fail(e,n,"Invalid loader port");c->loader_port=(unsigned short)num;}
    t=muse_json_get(&j,0,"phone_port");if(t>=0){if(muse_json_u64(&j,t,&num)||num>65535)return fail(e,n,"Invalid phone port");c->phone_port=(unsigned short)num;}
    t=muse_json_get(&j,0,"schema_version");if(t<0||muse_json_u64(&j,t,&num)||num!=1)return fail(e,n,"Unsupported configuration schema");
    int payloads=muse_json_get(&j,0,"payloads");
#define PAYLOAD(name) do { int entry=muse_json_get(&j,payloads,#name); int v=muse_json_get(&j,entry,"user_reported"); if(v>=0&&muse_json_string(&j,v,c->name##_status,sizeof(c->name##_status)))return fail(e,n,"Invalid payload status"); } while(0)
    PAYLOAD(elfldr);PAYLOAD(etahen);PAYLOAD(kstuff_lite);PAYLOAD(shadowmountplus);PAYLOAD(ftpsrv);
#undef PAYLOAD
    return muse_config_validate(c,e,n);
}
int muse_config_save(const char *path,const struct muse_config *c,char *e,size_t n){
    if(muse_config_validate(c,e,n))return -1;
    char temp[1024];if(snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))return fail(e,n,"Configuration path too long");
    int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return fail(e,n,"Cannot create temporary configuration (remove stale .tmp if needed)");
    FILE *f=fdopen(fd,"wb");if(!f){close(fd);unlink(temp);return fail(e,n,"Cannot open configuration stream");}
    int bad=0;char q[3074];
    if(fputs("{\n  \"schema_version\": 1",f)<0)bad=1;
#define FIELD(name) do{if(muse_json_quote(c->name,q,sizeof(q))||fprintf(f,",\n  \"" #name "\": %s",q)<0)bad=1;}while(0)
    FIELD(build_mode);FIELD(hybrid_provider);FIELD(console_ip);FIELD(server_url);FIELD(relay_token);FIELD(shadowmount_url);FIELD(ps5lm_url);FIELD(speech_url);FIELD(log_dir);FIELD(artifact_dir);FIELD(model_dir);FIELD(audio_in_module);FIELD(install_helper_path);FIELD(installer_artifact_dir);FIELD(installer_log_dir);FIELD(inference_backend);FIELD(speech_mode);FIELD(model);FIELD(firmware_reported);
#undef FIELD
    if(fputs(",\n  \"payloads\": {",f)<0)bad=1;
#define PAYLOAD(name, comma) do { if(muse_json_quote(c->name##_status,q,sizeof(q))||fprintf(f,comma "\"" #name "\":{\"user_reported\":%s,\"verified\":false}",q)<0)bad=1; } while(0)
    PAYLOAD(elfldr,"");PAYLOAD(etahen,",");PAYLOAD(kstuff_lite,",");PAYLOAD(shadowmountplus,",");PAYLOAD(ftpsrv,",");
#undef PAYLOAD
    if(fputs("}",f)<0)bad=1;
    if(fprintf(f,",\n  \"voice_gate_verified\": %s,\n  \"timeout_ms\": %d,\n  \"loader_port\": %u,\n  \"phone_port\": %u,\n  \"provider\": \"muse\"\n}\n",c->voice_gate_verified?"true":"false",c->timeout_ms,c->loader_port,c->phone_port)<0)bad=1;
    if(fflush(f)||fsync(fd))bad=1;if(fclose(f))bad=1;
    if(bad||rename(temp,path)){unlink(temp);return fail(e,n,"Cannot commit configuration");}return 0;
}

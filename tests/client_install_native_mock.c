/* Test-only native API adapter: actual helper/IPC/client code remains unchanged. */
#define _POSIX_C_SOURCE 200809L
#include "appinst_abi.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static char title[10],content[48];static int polls,launched;
static unsigned load_calls,resolve_calls,init_calls,terminate_calls,exists_calls,install_calls,status_calls,launch_calls;
static const char *failure(void){const char *value=getenv("MUSE_TEST_APPINST_FAILURE");return value?value:"";}
static void audit(void){
    const char *path=getenv("MUSE_TEST_APPINST_AUDIT");if(!path)return;FILE *f=fopen(path,"wb");assert(f);
    assert(fprintf(f,"{\"load\":%u,\"resolve\":%u,\"initialize\":%u,\"terminate\":%u,\"exists\":%u,\"install\":%u,\"status\":%u,\"launch\":%u}\n",load_calls,resolve_calls,init_calls,terminate_calls,exists_calls,install_calls,status_calls,launch_calls)>0);assert(!fclose(f));
}
int sceAppInstUtilInitialize(void){assert(load_calls==1&&resolve_calls==5);init_calls++;audit();return !strcmp(failure(),"init")?-2011:0;}
int sceAppInstUtilTerminate(void){assert(init_calls==1&&strcmp(failure(),"init"));terminate_calls++;audit();return 0;}
int sceAppInstUtilAppExists(const char *id,int *flag){assert(init_calls==1);exists_calls++;audit();strcpy(title,id);*flag=!strcmp(id,"PPSA99990");return 0;}
int sceAppInstUtilInstallByPackage(const muse_pkg_metadata *m,muse_pkg_info *info,muse_playgo_info *playgo){
    assert(init_calls==1&&exists_calls==1);install_calls++;audit();
    (void)playgo;assert(m->uri[0]=='/'&&!m->content_id[0]);if(!strcmp(title,"PPSA99992"))return (int)0x80b21106;
    FILE *f=fopen(m->uri,"rb");assert(f);assert(!fseek(f,0x20040,SEEK_SET));assert(fread(content,1,48,f)==48);fclose(f);
    strcpy(info->content_id,!strcmp(title,"PPSA99993")?"IV0000-PPSA99998_00-MUSEAPP000000001":content);return 0;
}
int sceAppInstUtilGetInstallStatus(const char *cid,muse_native_install_status *s){
    assert(install_calls==1);status_calls++;audit();
    assert(!strcmp(cid,content));memset(s,0,sizeof(*s));
    if(!strcmp(title,"PPSA99994")){strcpy(s->status,"error");s->error_info.error_code=(int)0x80b21106;return 0;}
    strcpy(s->status,++polls>=2?"completed":"installing");s->total_size=100;s->downloaded_size=polls>=2?100:50;
    if(!strcmp(title,"PPSA99997")&&polls>=2&&polls<4){strcpy(s->status,"playable");s->downloaded_size=60;}
    if(!strcmp(title,"PPSA99998")&&polls>=3){strcpy(s->status,"installing");s->downloaded_size=50;}
    return 0;
}
int sceUserServiceGetForegroundUser(uint32_t *id){*id=1;return 0;}
int sceSystemServiceLaunchApp(const char *id,const char **args,muse_launch_context *ctx){launch_calls++;audit();assert(strcmp(title,"PPSA99998")!=0);if(!strcmp(title,"PPSA99997"))assert(polls>=4);assert(!strcmp(id,title)&&args&&!args[0]&&ctx->structsize==32);if(!strcmp(title,"PPSA99995"))return (int)0x80940005;launched=1;return 0;}
int sceSystemServiceGetAppIdOfRunningBigApp(void){return launched?42:7;}
int sceLncUtilGetAppTitleId(uint32_t id,char *out){strcpy(out,id==42&&strcmp(title,"PPSA99996")?title:"PPSA00001");return 0;}
int sceKernelLoadStartModule(const char *path,size_t argc,const void *argv,unsigned flags,void *option,int *result){
    assert(!strcmp(path,"/system/common/lib/libSceAppInstUtil.sprx")&&!argc&&!argv&&!flags&&!option&&!result);
    assert(!load_calls&&!init_calls);load_calls++;audit();return !strcmp(failure(),"load")?-2012:42;
}
intptr_t kernel_dynlib_dlsym(pid_t pid,uint32_t handle,const char *name){
    static const char *const names[]={"sceAppInstUtilInitialize","sceAppInstUtilTerminate","sceAppInstUtilAppExists","sceAppInstUtilInstallByPackage","sceAppInstUtilGetInstallStatus"};
    assert(pid==getpid()&&handle==42&&load_calls==1&&!init_calls&&resolve_calls<5&&!strcmp(name,names[resolve_calls]));resolve_calls++;audit();
    if(!strncmp(failure(),"symbol:",7)&&!strcmp(failure()+7,name))return 0;
#define SYMBOL(symbol) if(!strcmp(name,#symbol))return (intptr_t)&symbol
    SYMBOL(sceAppInstUtilInitialize);SYMBOL(sceAppInstUtilTerminate);SYMBOL(sceAppInstUtilAppExists);SYMBOL(sceAppInstUtilInstallByPackage);SYMBOL(sceAppInstUtilGetInstallStatus);
#undef SYMBOL
    assert(0);return 0;
}
int muse_install_helper_serve(int,const unsigned char[32]);
int main(int argc,char **argv){
    assert(argc==3);audit();unsigned char token[32];assert(strlen(argv[2])==64);for(int i=0;i<32;i++){unsigned x;assert(sscanf(argv[2]+i*2,"%2x",&x)==1);token[i]=(unsigned char)x;}
    int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((unsigned short)atoi(argv[1]));assert(!connect(fd,(struct sockaddr*)&a,sizeof(a)));return muse_install_helper_serve(fd,token);
}

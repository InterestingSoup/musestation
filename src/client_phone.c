/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "client_phone.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

struct muse_phone {
    int fd,stop;
    pthread_t thread;
    pthread_mutex_t lock;
    char token[33],directory[512];
    int (*submit)(void *,const char *);
    void *context;
};
static const char page[]=
"<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Muse · Phone microphone</title>"
"<style>:root{color-scheme:dark;font:18px system-ui}body{background:#090e1a;color:#edf4ff;max-width:600px;margin:8vh auto;padding:24px}h1{font-size:48px;letter-spacing:-2px}p{line-height:1.6;color:#afbdd4}input,button{font:inherit;padding:16px;border-radius:14px;border:1px solid #35496a;background:#172238;color:inherit;box-sizing:border-box;width:100%;margin:8px 0}button{background:#76ffd5;color:#071911;font-weight:700;touch-action:none}button:disabled{opacity:.4}.tag{color:#76ffd5;text-transform:uppercase;letter-spacing:3px;font-size:13px}#status{min-height:4em}</style>"
"<div class=tag>Muse on PS5</div><h1>Your voice. Your console.</h1><p>Enter the pairing code shown on your PS5. Audio follows the console's local speech path, with Muse as the configured fallback. Approval stays on the console.</p>"
"<label>Pairing code<input id=pair autocomplete=off autocapitalize=off spellcheck=false maxlength=32 placeholder='32-character code from PS5'></label><button id=talk>Hold to talk</button>"
"<p id=secure></p><label>Or select a WAV recording<input id=file type=file accept='.wav,audio/wav'></label><p id=status role=status>Ready to pair.</p>"
"<script>const $=id=>document.getElementById(id);let context,stream,node,source,chunks=[],rate,recording=false;const status=s=>$('status').textContent=s;"
"async function send(blob){if($('pair').value.length!==32){status('Enter the pairing code from your console.');return}status('Sending audio to the console…');try{const r=await fetch('/audio',{method:'POST',headers:{'Content-Type':'audio/wav','X-Muse-Pair':$('pair').value},body:blob});status(await r.text())}catch(e){status('Connection failed. Check the console and its LAN address.')}}"
"function wav(){let n=chunks.reduce((n,a)=>n+a.length,0),all=new Float32Array(n),at=0;for(const a of chunks){all.set(a,at);at+=a.length}const count=Math.floor(n*16000/rate),buffer=new ArrayBuffer(44+count*2),v=new DataView(buffer);function text(p,s){for(let i=0;i<s.length;i++)v.setUint8(p+i,s.charCodeAt(i))}text(0,'RIFF');v.setUint32(4,36+count*2,true);text(8,'WAVE');text(12,'fmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);v.setUint32(24,16000,true);v.setUint32(28,32000,true);v.setUint16(32,2,true);v.setUint16(34,16,true);text(36,'data');v.setUint32(40,count*2,true);for(let i=0;i<count;i++){const pos=i*rate/16000,j=Math.floor(pos),f=pos-j,x=all[j]*(1-f)+(all[Math.min(j+1,n-1)]||0)*f;v.setInt16(44+i*2,Math.round(Math.max(-1,Math.min(1,x))*32767),true)}return new Blob([buffer],{type:'audio/wav'})}"
"async function start(e){e.preventDefault();if(recording)return;if($('pair').value.length!==32){status('Enter the pairing code first.');return}recording=true;chunks=[];status('Opening microphone…');try{stream=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true}});if(!recording){stream.getTracks().forEach(t=>t.stop());return}context=new AudioContext();rate=context.sampleRate;source=context.createMediaStreamSource(stream);node=context.createScriptProcessor(4096,1,1);node.onaudioprocess=e=>{if(recording){chunks.push(new Float32Array(e.inputBuffer.getChannelData(0)));if(chunks.length*4096/rate>45)stop()}};source.connect(node);node.connect(context.destination);await context.resume();status('Listening… Release to send.')}catch(e){recording=false;status('Microphone unavailable: '+e.message)}}"
"async function stop(){if(!recording)return;recording=false;if(!context)return;node.disconnect();source.disconnect();stream.getTracks().forEach(t=>t.stop());await context.close();context=null;if(chunks.length)send(wav());else status('No audio recorded.')}"
"if(!window.isSecureContext||!navigator.mediaDevices){$('talk').disabled=true;$('secure').textContent='This browser blocks microphone capture over LAN HTTP. Use a trusted HTTPS reverse proxy for hold-to-talk, or upload a WAV recording below. The DualSense microphone remains the primary input.'}"
"$('talk').addEventListener('pointerdown',e=>{e.target.setPointerCapture(e.pointerId);start(e)});$('talk').addEventListener('pointerup',stop);$('talk').addEventListener('pointercancel',stop);$('file').onchange=()=>{const f=$('file').files[0];if(f){if(f.size>8*1024*1024)status('WAV exceeds the 8 MiB limit.');else send(f)}};</script></html>";
static int64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static ssize_t receive(int fd,void *data,size_t n,int64_t deadline){
    for(;;){int64_t left=deadline-ms();if(left<=0)return -1;struct pollfd p={fd,POLLIN,0};int rc=poll(&p,1,(int)left);if(rc<0&&errno==EINTR)continue;if(rc<=0)return -1;ssize_t got=recv(fd,data,n,0);if(got<0&&(errno==EINTR||errno==EAGAIN))continue;return got;}
}
static int line(int fd,char *data,size_t size,int64_t deadline){
    size_t n=0;while(n+1<size){if(receive(fd,data+n,1,deadline)!=1)return -1;n++;if(n>=2&&data[n-2]=='\r'&&data[n-1]=='\n'){data[n-2]=0;return (int)n;}}return -1;
}
static void response(int fd,int status,const char *type,const char *data){
    char header[512];int n=snprintf(header,sizeof(header),"HTTP/1.1 %d Result\r\nConnection: close\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",status,type,strlen(data));
    const char *parts[]={header,data};size_t sizes[]={(size_t)n,strlen(data)};int64_t deadline=ms()+2000;
    for(int i=0;i<2;i++){size_t at=0;while(at<sizes[i]){int64_t left=deadline-ms();if(left<=0)return;struct pollfd p={fd,POLLOUT,0};if(poll(&p,1,(int)left)<=0)return;ssize_t sent=send(fd,parts[i]+at,sizes[i]-at,MSG_NOSIGNAL);if(sent<0&&(errno==EINTR||errno==EAGAIN))continue;if(sent<=0)return;at+=(size_t)sent;}}
}
static void handle(struct muse_phone *p,int fd){
    char header[8192];int64_t deadline=ms()+15000;int n=line(fd,header,sizeof(header),deadline);if(n<0)return;int get=!strcmp(header,"GET / HTTP/1.1")||!strcmp(header,"GET / HTTP/1.0");int post=!strcmp(header,"POST /audio HTTP/1.1")||!strcmp(header,"POST /audio HTTP/1.0");
    size_t total=(size_t)n,length=0;int length_seen=0,authorized=0,audio=0;
    for(;;){n=line(fd,header,sizeof(header),deadline);if(n<0||(total+=(size_t)n)>8192){response(fd,400,"text/plain","Invalid request headers.");return;}if(!*header)break;
        char *colon=strchr(header,':');if(!colon){response(fd,400,"text/plain","Invalid header.");return;}*colon++=0;while(*colon==' '||*colon=='\t')colon++;
        if(!strcasecmp(header,"X-Muse-Pair")){if(!strcmp(colon,p->token))authorized=1;}
        if(!strcasecmp(header,"Content-Type"))audio=!strcasecmp(colon,"audio/wav")||!strcasecmp(colon,"audio/x-wav");
        if(!strcasecmp(header,"Transfer-Encoding")){response(fd,400,"text/plain","Chunked uploads unsupported.");return;}
        if(!strcasecmp(header,"Content-Length")){if(length_seen++||!*colon){response(fd,400,"text/plain","Invalid length.");return;}for(;*colon;colon++){if(*colon<'0'||*colon>'9'||length>8u*1024*1024/10){response(fd,413,"text/plain","WAV exceeds 8 MiB.");return;}length=length*10+(size_t)(*colon-'0');}if(length>8u*1024*1024){response(fd,413,"text/plain","WAV exceeds 8 MiB.");return;}}
    }
    if(get){response(fd,200,"text/html; charset=utf-8",page);return;}
    if(!post){response(fd,404,"text/plain","Route unavailable.");return;}
    if(!authorized){response(fd,403,"text/plain","Pairing code incorrect. Read the current code on your PS5.");return;}
    if(!audio||length<44){response(fd,400,"text/plain","Upload a RIFF/WAVE audio file.");return;}
    unsigned char *body=malloc(length);if(!body){response(fd,503,"text/plain","Console memory unavailable.");return;}
    size_t used=0;while(used<length){ssize_t got=receive(fd,body+used,length-used,deadline);if(got<=0){free(body);response(fd,408,"text/plain","Audio upload timed out.");return;}used+=(size_t)got;}
    if(memcmp(body,"RIFF",4)||memcmp(body+8,"WAVE",4)){free(body);response(fd,400,"text/plain","Only RIFF/WAVE audio is accepted.");return;}
    char path[600];snprintf(path,sizeof(path),"%s/phone-XXXXXX",p->directory);int out=mkstemp(path);if(out<0){free(body);response(fd,500,"text/plain","Cannot save audio on console.");return;}
    used=0;int bad=0;while(used<length){ssize_t written=write(out,body+used,length-used);if(written<0&&errno==EINTR)continue;if(written<=0){bad=1;break;}used+=(size_t)written;}free(body);if(fsync(out))bad=1;if(close(out))bad=1;
    if(bad){unlink(path);response(fd,500,"text/plain","Cannot persist audio.");return;}
    if(p->submit(p->context,path)){unlink(path);response(fd,409,"text/plain","Muse is busy. Wait for the current request, then try again.");return;}
    response(fd,202,"text/plain","Audio queued on console. Review the plan on your PS5.");
}
static void *serve(void *arg){
    struct muse_phone *p=arg;for(;;){pthread_mutex_lock(&p->lock);int stop=p->stop;pthread_mutex_unlock(&p->lock);if(stop)break;struct pollfd fd={p->fd,POLLIN,0};if(poll(&fd,1,200)<=0)continue;int client=accept(p->fd,NULL,NULL);if(client<0)continue;if(!fcntl(client,F_SETFL,O_NONBLOCK))handle(p,client);close(client);}return NULL;
}
struct muse_phone *muse_phone_start(const struct muse_config *cfg,int(*submit)(void *,const char *),void *context,char *error,size_t cap){
    if(muse_config_local(cfg)){if(error&&cap)snprintf(error,cap,"Phone listener disabled in Local mode");return NULL;}
    struct muse_phone *p=calloc(1,sizeof(*p));if(!p){if(error&&cap)snprintf(error,cap,"Phone allocation failed");return NULL;}p->fd=-1;
    if(!submit||!cfg->phone_port||strlen(cfg->artifact_dir)>=sizeof(p->directory))goto fail;
    strcpy(p->directory,cfg->artifact_dir);struct stat st;if((mkdir(p->directory,0700)&&errno!=EEXIST)||stat(p->directory,&st)||!S_ISDIR(st.st_mode))goto fail;
    unsigned char random[16];int rng=open("/dev/urandom",O_RDONLY);if(rng<0)goto fail;size_t n=0;while(n<sizeof(random)){ssize_t got=read(rng,random+n,sizeof(random)-n);if(got<0&&errno==EINTR)continue;if(got<=0){close(rng);goto fail;}n+=(size_t)got;}close(rng);static const char hex[]="0123456789abcdef";for(size_t i=0;i<sizeof(random);i++){p->token[2*i]=hex[random[i]>>4];p->token[2*i+1]=hex[random[i]&15];}
    p->fd=socket(AF_INET,SOCK_STREAM,0);if(p->fd<0)goto fail;int one=1;(void)setsockopt(p->fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(cfg->phone_port);addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(p->fd,(struct sockaddr *)&addr,sizeof(addr))||listen(p->fd,4)||fcntl(p->fd,F_SETFL,O_NONBLOCK))goto fail;
    p->submit=submit;p->context=context;if(pthread_mutex_init(&p->lock,NULL))goto fail;if(pthread_create(&p->thread,NULL,serve,p)){pthread_mutex_destroy(&p->lock);goto fail;}return p;
fail:
    if(p->fd>=0)close(p->fd);free(p);if(error&&cap)snprintf(error,cap,"Phone listener unavailable: verify port and writable artifact directory");return NULL;
}
const char *muse_phone_token(const struct muse_phone *p){return p?p->token:"";}
void muse_phone_stop(struct muse_phone *p){if(!p)return;pthread_mutex_lock(&p->lock);p->stop=1;pthread_mutex_unlock(&p->lock);pthread_join(p->thread,NULL);close(p->fd);pthread_mutex_destroy(&p->lock);free(p);}

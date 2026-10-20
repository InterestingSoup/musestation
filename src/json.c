/* SPDX-License-Identifier: GPL-3.0-or-later
 * Small original bounded JSON parser; no third-party code. */
#include "json.h"
#include <string.h>
#include <limits.h>

static int hex(unsigned char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static int codepoint(const char *s,size_t n,size_t *p,unsigned *u) {
    if(*p+4>n)return -1;
    *u=0;
    for(int k=0;k<4;k++){int h=hex((unsigned char)s[(*p)++]);if(h<0)return -1;*u=(*u<<4)|(unsigned)h;}
    return 0;
}
static int decode(const char *s,size_t n,char *out,size_t cap) {
    size_t i=0,w=0;
    while(i<n){
        unsigned u=(unsigned char)s[i++];
        if(u<32||u=='"')return -1;
        if(u=='\\'){
            if(i==n)return -1;
            u=(unsigned char)s[i++];
            if(u=='b')u='\b'; else if(u=='f')u='\f'; else if(u=='n')u='\n';
            else if(u=='r')u='\r'; else if(u=='t')u='\t';
            else if(u=='u'){
                if(codepoint(s,n,&i,&u))return -1;
                if(u>=0xd800&&u<=0xdbff){
                    unsigned lo;
                    if(i+2>n||s[i++]!='\\'||s[i++]!='u'||codepoint(s,n,&i,&lo)||lo<0xdc00||lo>0xdfff)return -1;
                    u=0x10000+((u-0xd800)<<10)+lo-0xdc00;
                }else if(u>=0xdc00&&u<=0xdfff)return -1;
                if(!u)return -1; /* C APIs cannot represent embedded NUL. */
            }else if(u!='"'&&u!='\\'&&u!='/')return -1;
        }else if(u>=128){
            unsigned count,min;
            if(u>=0xc2&&u<=0xdf){count=1;min=0x80;u&=31;}
            else if(u>=0xe0&&u<=0xef){count=2;min=0x800;u&=15;}
            else if(u>=0xf0&&u<=0xf4){count=3;min=0x10000;u&=7;}
            else return -1;
            while(count--){if(i==n||((unsigned char)s[i]&0xc0)!=0x80)return -1;u=(u<<6)|((unsigned char)s[i++]&63);}
            if(u<min||u>0x10ffff||(u>=0xd800&&u<=0xdfff))return -1;
        }
        unsigned char b[4];size_t len;
        if(u<0x80){b[0]=(unsigned char)u;len=1;}
        else if(u<0x800){b[0]=0xc0|(u>>6);b[1]=0x80|(u&63);len=2;}
        else if(u<0x10000){b[0]=0xe0|(u>>12);b[1]=0x80|((u>>6)&63);b[2]=0x80|(u&63);len=3;}
        else{b[0]=0xf0|(u>>18);b[1]=0x80|((u>>12)&63);b[2]=0x80|((u>>6)&63);b[3]=0x80|(u&63);len=4;}
        if(out){if(w+len>=cap)return -1;memcpy(out+w,b,len);}w+=len;
    }
    if(out){if(w>=cap)return -1;out[w]=0;}return 0;
}
struct cursor {struct muse_json *j;size_t p;};
static void ws(struct cursor *c){while(c->p<c->j->length&&strchr(" \r\n\t",c->j->data[c->p]))c->p++;}
static int value(struct cursor *c,int depth){
    struct muse_json *j=c->j;const char *s=j->data;size_t n=j->length;
    ws(c);if(c->p>=n||depth>32||j->count>=512)return -1;
    int t=j->count++;j->tokens[t].start=c->p;char ch=s[c->p++];j->tokens[t].type=ch;
    if(ch=='"'){
        size_t start=c->p;
        while(c->p<n){if(s[c->p]=='"')break;if(s[c->p++]=='\\'){if(c->p==n)return -1;c->p++;}}
        if(c->p==n||decode(s+start,c->p-start,NULL,0))return -1;
        c->p++;
    }else if(ch=='{'||ch=='['){
        ws(c);char close=ch=='{'?'}':']';
        if(c->p<n&&s[c->p]==close)c->p++;
        else for(;;){
            if(ch=='{'){
                ws(c);if(c->p==n||s[c->p]!='"')return -1;
                int key=value(c,depth+1);char name[512];if(key<0||muse_json_string(j,key,name,sizeof(name)))return -1;
                for(int k=t+1;k<key;k=j->tokens[k+1].next){char prev[512];if(muse_json_string(j,k,prev,sizeof(prev))||!strcmp(prev,name))return -1;}
                ws(c);if(c->p==n||s[c->p++]!=':')return -1;
            }
            if(value(c,depth+1)<0)return -1;
            ws(c);if(c->p==n)return -1;
            if(s[c->p]==close){c->p++;break;}
            if(s[c->p++]!=',')return -1;
        }
    }else if(ch=='t'||ch=='f'||ch=='n'){
        const char *literal=ch=='t'?"true":ch=='f'?"false":"null";size_t len=strlen(literal);
        if(j->tokens[t].start+len>n||memcmp(s+j->tokens[t].start,literal,len))return -1;
        c->p=j->tokens[t].start+len;
    }else{
        j->tokens[t].type='0';size_t p=j->tokens[t].start;
        if(s[p]=='-')p++;
        if(p==n)return -1;
        if(s[p]=='0')p++;
        else{if(s[p]<'1'||s[p]>'9')return -1;while(p<n&&s[p]>='0'&&s[p]<='9')p++;}
        if(p<n&&s[p]=='.'){p++;size_t a=p;while(p<n&&s[p]>='0'&&s[p]<='9')p++;if(a==p)return -1;}
        if(p<n&&(s[p]=='e'||s[p]=='E')){p++;if(p<n&&(s[p]=='+'||s[p]=='-'))p++;size_t a=p;while(p<n&&s[p]>='0'&&s[p]<='9')p++;if(a==p)return -1;}
        c->p=p;
    }
    j->tokens[t].end=c->p;j->tokens[t].next=j->count;return t;
}
int muse_json_parse_prefix(struct muse_json *j,const void *data,size_t length,size_t *consumed){
    if(consumed)*consumed=0;memset(j,0,sizeof(*j));j->data=data;j->length=length;
    if(!data||!length||!consumed||memchr(data,0,length))return -1;
    struct cursor c={j,0};if(value(&c,0)<0)return -1;*consumed=c.p;return 0;
}
int muse_json_parse(struct muse_json *j,const void *data,size_t length){
    memset(j,0,sizeof(*j));j->data=data;j->length=length;
    if(!data||!length||memchr(data,0,length))return -1;
    struct cursor c={j,0};if(value(&c,0)<0)return -1;ws(&c);return c.p==length?0:-1;
}
int muse_json_string(const struct muse_json *j,int t,char *out,size_t n){
    if(t<0||t>=j->count||j->tokens[t].type!='"')return -1;
    const struct muse_json_token *p=&j->tokens[t];return decode(j->data+p->start+1,p->end-p->start-2,out,n);
}
int muse_json_get(const struct muse_json *j,int object,const char *key){
    if(object<0||object>=j->count||j->tokens[object].type!='{')return -1;
    for(int k=object+1;k<j->tokens[object].next;k=j->tokens[k+1].next){char buf[512];if(muse_json_string(j,k,buf,sizeof(buf)))return -1;if(!strcmp(buf,key))return k+1;}
    return -1;
}
int muse_json_u64(const struct muse_json *j,int t,uint64_t *out){
    if(t<0||t>=j->count||j->tokens[t].type!='0')return -1;
    uint64_t value=0;for(size_t p=j->tokens[t].start;p<j->tokens[t].end;p++){unsigned char c=j->data[p];if(c<'0'||c>'9'||value>(UINT64_MAX-(c-'0'))/10)return -1;value=value*10+c-'0';}*out=value;return 0;
}
int muse_json_bool(const struct muse_json *j,int t,int *out){if(t<0||t>=j->count)return -1;if(j->tokens[t].type=='t'){*out=1;return 0;}if(j->tokens[t].type=='f'){*out=0;return 0;}return -1;}
int muse_json_quote(const char *s,char *out,size_t cap){
    static const char hexes[]="0123456789abcdef";size_t n=0;if(!s||cap<3)return -1;out[n++]='"';
    for(;*s;s++){unsigned char c=(unsigned char)*s;size_t need=c<32?6:(c=='"'||c=='\\'?2:1);if(n+need+2>cap)return -1;
        if(c<32){out[n++]='\\';out[n++]='u';out[n++]='0';out[n++]='0';out[n++]=hexes[c>>4];out[n++]=hexes[c&15];}
        else{if(c=='"'||c=='\\')out[n++]='\\';out[n++]=(char)c;}}
    out[n++]='"';out[n]=0;return 0;
}

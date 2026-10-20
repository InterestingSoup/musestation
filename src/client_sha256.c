/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original SHA-256 implementation of FIPS 180-4, validated with published vectors. */
#include "client_sha256.h"
#include <stdint.h>
#include <string.h>
static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t r(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void block(uint32_t h[8],const unsigned char b[64]){
    uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=((uint32_t)b[4*i]<<24)|((uint32_t)b[4*i+1]<<16)|((uint32_t)b[4*i+2]<<8)|b[4*i+3];
    for(unsigned i=16;i<64;i++){uint32_t a=w[i-15],z=w[i-2];w[i]=w[i-16]+(r(a,7)^r(a,18)^(a>>3))+w[i-7]+(r(z,17)^r(z,19)^(z>>10));}
    uint32_t a=h[0],b0=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
    for(unsigned i=0;i<64;i++){uint32_t t=z+(r(e,6)^r(e,11)^r(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t q=(r(a,2)^r(a,13)^r(a,22))+((a&b0)^(a&c)^(b0&c));z=g;g=f;f=e;e=d+t;d=c;c=b0;b0=a;a=t+q;}
    h[0]+=a;h[1]+=b0;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
void muse_sha256_init(struct muse_sha256 *s){
    static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memset(s,0,sizeof(*s));memcpy(s->h,initial,sizeof(initial));
}
void muse_sha256_update(struct muse_sha256 *s,const void *data,size_t len){
    const unsigned char *p=data;s->length+=len;
    while(len){size_t n=64-s->used;if(n>len)n=len;memcpy(s->tail+s->used,p,n);s->used+=n;p+=n;len-=n;
        if(s->used==64){block(s->h,s->tail);s->used=0;}}
}
void muse_sha256_final(struct muse_sha256 *s,char out[65]){
    unsigned char tail[128]={0};memcpy(tail,s->tail,s->used);tail[s->used]=0x80;size_t count=s->used<56?64:128;uint64_t bits=s->length*8;
    for(unsigned i=0;i<8;i++)tail[count-1-i]=(unsigned char)(bits>>(8*i));block(s->h,tail);if(count==128)block(s->h,tail+64);
    static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;i++){unsigned byte=(s->h[i/4]>>(24-8*(i%4)))&255;out[2*i]=hex[byte>>4];out[2*i+1]=hex[byte&15];}out[64]=0;
}
void muse_sha256_hex(const void *data,size_t len,char out[65]){
    struct muse_sha256 s;muse_sha256_init(&s);muse_sha256_update(&s,data,len);muse_sha256_final(&s,out);
}

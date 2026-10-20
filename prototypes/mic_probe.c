/* SPDX-License-Identifier: GPL-3.0-or-later
 * API sequence adapted from RafaelNGP/PSBox, src/media/audio_in_ps5.cpp
 * (GPL-3.0-or-later). This is a hardware probe, not the gadget voice loop.
 * stdin: AudioIn module path, then destination WAV path (one per line).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int sceUserServiceInitialize(const void *);
int sceUserServiceGetInitialUser(int *);
static void le16(unsigned char *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void le32(unsigned char *p, unsigned v) {
    p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24;
}
static int line(char *p, size_t n) {
    if (!fgets(p, (int)n, stdin)) return -1;
    size_t len = strcspn(p, "\r\n");
    if (!len || len == n-1) return -1;
    p[len]=0;
    return p[0]=='/' ? 0 : -1;
}
int main(void) {
    char module[1024], output[1024];
    puts("AudioIn module absolute path, then output WAV absolute path:");
    fflush(stdout);
    if (line(module,sizeof module) || line(output,sizeof output)) return 2;
    void *lib=dlopen(module, RTLD_NOW);
    if (!lib) { puts("AudioIn module load failed"); return 3; }
    int (*init)(void)=(int (*)(void))dlsym(lib,"sceAudioInInit");
    int (*open_in)(int,int,int,unsigned,unsigned,unsigned)=
        (int (*)(int,int,int,unsigned,unsigned,unsigned))dlsym(lib,"sceAudioInOpen");
    int (*input)(int,void *)=(int (*)(int,void *))dlsym(lib,"sceAudioInInput");
    int (*close_in)(int)=(int (*)(int))dlsym(lib,"sceAudioInClose");
    int (*silent)(int)=(int (*)(int))dlsym(lib,"sceAudioInGetSilentState");
    if (!init || !open_in || !input || !close_in || !silent) {
        puts("AudioIn required symbol missing"); dlclose(lib); return 4;
    }
    int user=-1, rc=init();
    if (rc<0 && (uint32_t)rc!=0x8026000C && (uint32_t)rc!=0x8026000E) {
        printf("AudioIn init failed: %08x\n",(unsigned)rc); dlclose(lib); return 5;
    }
    if (sceUserServiceGetInitialUser(&user)<0) {
        uint32_t priority=0x2ff;
        sceUserServiceInitialize(&priority);
        if (sceUserServiceGetInitialUser(&user)<0) { dlclose(lib); return 6; }
    }
    int handle=open_in(user,0,0,256,48000,0);
    if (handle<0) handle=open_in(user,1,0,256,48000,0);
    if (handle<0) { printf("AudioIn open failed: %08x\n",(unsigned)handle); dlclose(lib); return 7; }
    /* Exclusive create avoids replacing any user file. */
    FILE *f=fopen(output,"wbx");
    if (!f) { close_in(handle); dlclose(lib); return 8; }
    unsigned char header[44]={0};
    memcpy(header,"RIFF",4); memcpy(header+8,"WAVEfmt ",8);
    le32(header+16,16); le16(header+20,1); le16(header+22,1);
    le32(header+24,48000); le32(header+28,96000);
    le16(header+32,2); le16(header+34,16); memcpy(header+36,"data",4);
    unsigned bytes=0;
    rc=fwrite(header,1,44,f)==44 ? 0 : -1;
    for (unsigned i=0; rc==0 && i<563; ++i) {
        int16_t samples[256];
        rc=input(handle,samples);
        if (rc<0) break;
        int state=silent(handle);
        /* Fail closed on unknown mute state; respect no-device and mute bits. */
        if (state<0 || (state&5)) memset(samples,0,sizeof samples);
        if (fwrite(samples,1,sizeof samples,f)!=sizeof samples) { rc=-1; break; }
        bytes+=sizeof samples;
        rc=0;
    }
    close_in(handle);
    le32(header+4,bytes+36); le32(header+40,bytes);
    if (fseek(f,0,SEEK_SET) || fwrite(header,1,44,f)!=44) rc=-1;
    if (fclose(f)) rc=-1;
    dlclose(lib);
    printf("mic probe: rc=%d bytes=%u; playback and microphone identity need hardware verification\n",rc,bytes);
    return rc<0 ? 9 : 0;
}

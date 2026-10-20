/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host-only capture API fixture: no PS5 microphone compatibility claim. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
static _Atomic unsigned blocks;
unsigned muse_test_audio_blocks(void){return atomic_load(&blocks);}
void muse_test_audio_reset(void){atomic_store(&blocks,0);}
int sceAudioInInit(void){return 0;}
int sceAudioInOpen(int user,int type,int index,unsigned frames,unsigned rate,unsigned format){
    (void)user;(void)type;(void)index;
    if(frames!=256||rate!=48000||format)return -1;
    atomic_store(&blocks,0);return 1;
}
int sceAudioInInput(int handle,void *buffer){
    if(handle!=1)return -1;
    memset(buffer,0,256*sizeof(int16_t));atomic_fetch_add(&blocks,1);usleep(1000);return 0;
}
int sceAudioInClose(int handle){return handle==1?0:-1;}
int sceAudioInGetSilentState(int handle){return handle==1?0:-1;}

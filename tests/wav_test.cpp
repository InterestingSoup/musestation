// SPDX-License-Identifier: GPL-3.0-or-later
#include "wav.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static void put(std::string &s, size_t at, uint32_t value, unsigned count) {
  for (unsigned i = 0; i < count; ++i) s[at + i] = char(value >> (8 * i));
}
static std::string wave(unsigned rate, unsigned channels,
                        const std::vector<int16_t> &samples) {
  std::string s(44 + samples.size() * 2, '\0');
  s.replace(0, 4, "RIFF"); s.replace(8, 4, "WAVE");
  s.replace(12, 4, "fmt "); s.replace(36, 4, "data");
  put(s,4,s.size()-8,4); put(s,16,16,4); put(s,20,1,2);
  put(s,22,channels,2); put(s,24,rate,4); put(s,28,rate*channels*2,4);
  put(s,32,channels*2,2); put(s,34,16,2); put(s,40,samples.size()*2,4);
  for (size_t i=0;i<samples.size();++i) put(s,44+2*i,uint16_t(samples[i]),2);
  return s;
}
static bool near(float a, float b) { return std::fabs(a-b)<0.000001f; }
int main() {
  std::vector<float> out;
  auto s=wave(24000,1,{-16384,16384,0});
  assert(muse::decode_speech_wav(s,out)); assert(out.size()==12);
  assert(near(out[0],-.5f)&&near(out[2],0)&&near(out[4],.5f));
  assert(near(out[6],.25f)&&near(out[8],0)&&near(out[10],0));
  for(size_t i=0;i<out.size();i+=2)assert(out[i]==out[i+1]);
  assert(muse::decode_speech_wav(wave(48000,2,{-32768,32767,8192,-8192}),out));
  assert(out.size()==4&&near(out[0],-1)&&near(out[1],32767.f/32768));
  assert(near(out[2],.25f)&&near(out[3],-.25f));
  assert(!muse::decode_speech_wav(wave(24000,1,{}),out)&&out.empty());
  assert(!muse::decode_speech_wav(wave(48000,2,{1,2,3}),out));
  assert(!muse::decode_speech_wav(s.substr(0,s.size()-1),out));
  auto bad=s; put(bad,28,1,4); assert(!muse::decode_speech_wav(bad,out));
  bad=s; put(bad,20,3,2); assert(!muse::decode_speech_wav(bad,out));
  bad=s; put(bad,40,0xffffffff,4); assert(!muse::decode_speech_wav(bad,out));
  assert(muse::decode_speech_wav(wave(96000,1,{1,2,3,4}),out)&&out.size()==4);
  puts("WAV: neural 24k interpolation, 48k stereo, framing and invalid audio PASS");
}

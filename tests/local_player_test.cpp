// SPDX-License-Identifier: GPL-3.0-or-later
#include "local_player.hpp"
#include "core/save_file.hpp"
extern "C" {
#include "client_sha256.h"
}
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
static std::string hash(const std::string &source) { char out[65];muse_sha256_hex(source.data(),source.size(),out);return out; }
static std::string saved(const std::string &path) {
  std::string bytes; assert(hui::save::read_file(path,&bytes,20000));
  auto data=hui::save::decode(hui::save::Kind::game,bytes);assert(data.ok&&data.version==1);return data.payload;
}
int main() {
  char temporary[]="/tmp/muse-player-XXXXXX";char *directory=mkdtemp(temporary);assert(directory);
  const std::string root=directory,path=root+"/state",seedpath=root+"/seed";
  const std::string source="local n;function update(dt,input) if n==nil then n=tonumber(state.get()) or 0 end if input.pressed.cross then n=n+1 end state.set(tostring(n)) end function draw()gfx.text(20,30,tostring(n),24,255,255,255)end";
  std::string error;muse::LocalPlayer player;hui::InputFrame input{};
  assert(!player.open(source,std::string(64,'0'),path,42,error)&&!player.active());
  assert(player.open(source,hash(source),path,42,error));
  input.connected=true;input.pressed=1u<<unsigned(hui::Action::confirm);
  assert(player.frame(.016,input,error));assert(player.save(error));assert(saved(path)=="1");player.close();
  assert(player.open(source,hash(source),path,42,error));input.pressed=0;
  assert(player.frame(.016,input,error));assert(player.save(error));assert(saved(path)=="1");
  input.pressed=1u<<unsigned(hui::Action::confirm);assert(player.frame(.016,input,error));
  assert(player.save(error));assert(saved(path)=="2");player.close();
  // A failed frame must not overwrite the previous successful state when closed.
  const std::string broken="function update()state.set('bad');error('intentional runtime failure')end function draw()end";
  assert(player.open(broken,hash(broken),path,42,error));assert(!player.frame(.016,input,error));player.close();assert(saved(path)=="2");
  const std::string seeded="function update()state.set(tostring(math.random()))end function draw()end";
  assert(player.open(seeded,hash(seeded),seedpath,7234,error));assert(player.frame(.016,input,error));assert(player.save(error));const auto expected=saved(seedpath);player.close();
  assert(player.open(seeded,hash(seeded),seedpath,7234,error));assert(player.frame(.016,input,error));assert(player.save(error));assert(saved(seedpath)==expected);player.close();
  assert(player.open(source,hash(source),root+"/missing/state",1,error));assert(player.frame(.016,input,error));assert(!player.save(error));player.close();assert(saved(path)=="2");
  {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"corrupt";}
  assert(!player.open(source,hash(source),path,42,error)&&!player.active());
  const std::string loop="while true do end";assert(!player.open(loop,hash(loop),root+"/unused",1,error)&&!player.active());
  unlink(path.c_str());unlink(seedpath.c_str());assert(!rmdir(root.c_str()));
  std::puts("PASS player: approved-source hash, controller input, save/reopen integrity, deterministic seed, runtime/write failures preserve prior save, corrupt save/source rejected");
}

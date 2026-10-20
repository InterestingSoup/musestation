// SPDX-License-Identifier: GPL-3.0-or-later
// Host software-rendered inspection of an actual Lua source revision.
#include "local_player.hpp"
#include "core/save_file.hpp"
extern "C" {
#include "client_sha256.h"
}
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace hui::sys {
int64_t monotonic_us() {
  timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
  return int64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
void log(const char *f, ...) {
  va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); fputc('\n', stderr);
}
}
static std::string quoted(const std::string &s) {
  std::string result="\"";
  for (unsigned char c:s) {
    if(c=='"'||c=='\\') {result+='\\';result+=char(c);}
    else if(c<32) {char escape[7];snprintf(escape,sizeof escape,"\\u%04x",c);result+=escape;}
    else result+=char(c);
  }
  return result+'"';
}
static void require(bool condition,const std::string &detail) {
  if(!condition) throw std::runtime_error(detail);
}
struct TemporaryState {
  char directory[64]="/tmp/muse-local-preview-XXXXXX";
  TemporaryState() {require(mkdtemp(directory)!=nullptr,"Cannot create isolated preview state directory");}
  ~TemporaryState() {unlink((std::string(directory)+"/state").c_str());rmdir(directory);}
};
int main(int argc,char **argv) {
  try {
    require(argc==4,"Usage: local-app-preview SOURCE.lua FONT_ASSET_DIRECTORY OUTPUT_DIRECTORY");
    std::string source,error;
    require(hui::save::read_file(argv[1],&source,MUSE_LOCAL_SOURCE_MAX),"Cannot read bounded Lua source");
    char source_sha[65];muse_sha256_hex(source.data(),source.size(),source_sha);
    muse_local_limits limits{};limits.seed=17;
    muse_local_validation validation{};char detail[512]{};
    int validation_result=muse_local_app_validate(source.data(),source.size(),&limits,60,&validation,detail,sizeof detail);
    require(validation_result==0,std::string("Lua validation failed: ")+detail);

    auto display=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
    EGLint major,minor;
    require(eglInitialize(display,&major,&minor)&&eglBindAPI(EGL_OPENGL_API),"EGL initialization failed");
    EGLint attributes[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,
      EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    auto context=eglCreateContext(display,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attributes);
    require(context!=EGL_NO_CONTEXT&&eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context),
            "Cannot create host OpenGL context");
    const char *driver=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string renderer_name=driver?driver:"unknown";
    require(renderer_name.find("llvmpipe")!=std::string::npos,"Preview must use the host llvmpipe software renderer");
    hui::gfx::Renderer renderer;require(renderer.init(),"Renderer initialization failed");
    std::array<hui::gfx::Font,4> fonts;hui::ui::Fonts font_refs;
    hui::ui::FontRef *refs[]={&font_refs.regular,&font_refs.semibold,&font_refs.display,&font_refs.mono};
    const char *names[]={"inter-regular.huifont","inter-semibold.huifont","montserrat-medium.huifont","dejavu-sans-mono.huifont"};
    for(unsigned i=0;i<fonts.size();i++) {
      std::string data;
      require(hui::save::read_file(std::string(argv[2])+"/fonts/"+names[i],&data)&&fonts[i].load(data),"Cannot load actual packaged font");
      *refs[i]={&fonts[i],renderer.batch().create_font_texture(fonts[i])};
    }
    GLuint texture,framebuffer;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1920,1080,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Preview framebuffer incomplete");

    TemporaryState temporary;muse::LocalPlayer player;
    if(!player.open(source,source_sha,std::string(temporary.directory)+"/state",17,error))
      throw std::runtime_error(error);
    std::ofstream snapshot(std::string(argv[3])+"/source.lua",std::ios::binary);
    snapshot.write(source.data(),source.size());snapshot.close();
    require(bool(snapshot),"Cannot preserve exact preview source");
    std::ofstream report(std::string(argv[3])+"/preview.json");require(bool(report),"Cannot create preview report");
    report<<"{\n  \"hardware_verified\": false,\n  \"host_software_preview\": true,\n  \"source\": "<<quoted(argv[1])
      <<",\n  \"source_sha256\": "<<quoted(source_sha)<<",\n  \"renderer\": "<<quoted(renderer_name)
      <<",\n  \"seed\": 17,\n  \"validation_frames\": "<<validation.ticks
      <<",\n  \"validation_peak_bytes\": "<<validation.peak_memory_bytes<<",\n  \"provably_hidden_text_commands\": "<<validation.provably_hidden_text_commands<<",\n  \"captures\": [\n";
    const char *shots[]={"initial","after-cross","after-circle"};
    unsigned frame=0;
    for(unsigned shot=0;shot<3;shot++) {
      hui::InputFrame input{};input.connected=true;
      if(shot) input.pressed=input.held=hui::action_bit(shot==1?hui::Action::confirm:hui::Action::back);
      if(!player.frame(1.0/60.0,input,error)) throw std::runtime_error(std::string(shots[shot])+": "+error);
      frame++;
      player.render(renderer,font_refs,1920,1080,framebuffer);
      glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
      std::vector<unsigned char> pixels(1920*1080*3);
      glReadPixels(0,0,1920,1080,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
      require(glGetError()==GL_NO_ERROR,"Host framebuffer readback failed");
      std::string name=std::string(shots[shot])+".ppm";
      std::ofstream image(std::string(argv[3])+"/"+name,std::ios::binary);
      image<<"P6\n1920 1080\n255\n";
      for(int y=1079;y>=0;y--) image.write(reinterpret_cast<const char*>(pixels.data()+size_t(y)*1920*3),1920*3);
      require(bool(image),"Cannot write preview image");
      char pixel_sha[65];muse_sha256_hex(pixels.data(),pixels.size(),pixel_sha);
      report<<(shot?",\n":"")<<"    {\"name\": "<<quoted(shots[shot])<<", \"frame\": "<<frame
        <<", \"bottom_up_rgb_sha256\": "<<quoted(pixel_sha)<<"}";
      // Release the previous button before the next edge. This adds one neutral
      // update between captured presses, exactly as separate controller taps do.
      if(shot==1) {input.pressed=input.held=0;if(!player.frame(1.0/60.0,input,error)) throw std::runtime_error(error);frame++;}
    }
    report<<"\n  ],\n  \"limitation\": \"Visual inspection only; this is not a PS5 execution or generic app-behavior pass.\"\n}\n";
    require(bool(report),"Cannot finish preview report");
    player.close();glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);
    std::printf("HOST SOFTWARE PREVIEW: %s; source SHA256 %s; 60 validation ticks; initial/Cross/Circle captured\n",argv[3],source_sha);
    return 0;
  } catch(const std::exception &error) {std::fprintf(stderr,"PREVIEW ERROR: %s\n",error.what());return 1;}
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "local_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char error[512];
static muse_local_app *app;
static void pass(const char*s){muse_local_validation r;int rc=muse_local_app_validate(s,strlen(s),NULL,60,&r,error,sizeof(error));if(rc)fprintf(stderr,"unexpected failure: %s\n",error);assert(!rc&&r.ticks==60&&r.peak_memory_bytes<=8u*1024u*1024u);}
static void fail(const char*s,const char*contains){muse_local_validation r;int rc=muse_local_app_validate(s,strlen(s),NULL,2,&r,error,sizeof(error));if(!rc||!strstr(error,contains))fprintf(stderr,"expected %s, got rc%d %s\n",contains,rc,error);assert(rc&&strstr(error,contains));}
static int cancel(void*p){return *(int*)p;}
int main(void){
 pass("local n=0 function update(dt,input) assert(dt>=0 and dt<=.1) if input.pressed.cross then n=n+1 end end function draw() gfx.clear(1,2,3) gfx.rect(10,20,30,40,255,0,0) gfx.circle(80,90,15,0,255,0) gfx.line(1,2,3,4,2,0,0,255) gfx.text(20,30,tostring(n),24,255,255,255) end");
 pass("assert(os==nil and io==nil and package==nil and debug==nil and coroutine==nil and load==nil and loadfile==nil and dofile==nil and require==nil and pcall==nil and xpcall==nil and setmetatable==nil and getmetatable==nil and collectgarbage==nil and print==nil) assert(string.dump==nil and ('abc').dump==nil and string.find==nil and string.rep==nil and string.format==nil) function update() end function draw()gfx.rect(10,10,20,20,255,0,0) end");
 pass("local t={1,2};table.insert(t,2,3);assert(t[2]==3);assert(table.remove(t,2)==3);assert(t[2]==2);assert(string.sub('abc',2)=='bc');assert(string.char(97,98)=='ab');assert(math.random(1,1)==1);function update()end function draw()gfx.rect(10,10,20,20,255,0,0)end");
 fail("function update()end function draw()end","no visible content");
 fail("function update()end function draw()gfx.clear(255,255,255)gfx.text(20,30,'hidden',24,255,255,255)end","no visible content");
 fail("function update()end function draw()gfx.clear(255,255,255)gfx.rect(20,30,40,50,255,255,255,128)end","no visible content");
 fail("function update()end function draw()gfx.clear(0,0,0)gfx.text(20,30,'covered',24,255,255,255)gfx.rect(0,0,WIDTH,HEIGHT,0,0,0)end","no visible content");
 fail("function update()end function draw()gfx.clear(0,0,0)gfx.text(20,30,'transparent',24,255,255,255,0)end","no visible content");
 pass("local n=0 function update()n=n+1 end function draw()gfx.clear(0,0,0)if n>30 then gfx.text(20,30,'delayed',24,255,255,255)end end");
 pass("function update()end function draw()gfx.clear(0,0,0)gfx.line(20,30,80,90,.1,255,255,255,1)end");
 pass("function update()end function draw()gfx.clear(0,0,0)gfx.text(20,30,'not fully covered',24,255,255,255)gfx.rect(0,0,WIDTH,HEIGHT,0,0,0,128)end");
 {const char*s="function update()end function draw()gfx.text(20,30,'covered',24,255,255,255)gfx.clear(0,0,0)gfx.text(20,30,'same',24,0,0,0)gfx.text(20,30,'alpha',24,255,255,255,0)gfx.rect(10,10,10,10,255,0,0)end";muse_local_validation r;assert(!muse_local_app_validate(s,strlen(s),NULL,2,&r,error,sizeof error));assert(r.provably_hidden_text_commands==6);}
 /* An inset panel is visibly nonuniform even with matching hidden text. The
  * conservative proof must not mistake that for an entirely blank frame. */
 pass("function update()end function draw()gfx.clear(0,0,0)gfx.rect(40,40,WIDTH-80,HEIGHT-80,255,255,255)gfx.text(100,100,'hidden',24,255,255,255)end");
 fail("while true do end","instruction budget");
 fail("function update()while true do end end function draw()end","instruction budget");
 fail("function update()end function draw()while true do end end","instruction budget");
 fail("local s='x' while true do s=s..s end","memory");
 fail("function update()end function draw()for i=1,513 do gfx.rect(0,0,1,1,0,0,0)end end","draw command limit");
 fail("function update()end function draw()gfx.rect(0/0,0,1,1,0,0,0)end","outside range");
 fail("function update()gfx.clear(0,0,0)end function draw()end","only available inside draw");
 fail("function update()end function draw()gfx.text(0,0,'a'..string.char(0),20,0,0,0)end","UTF-8");
 fail("function update()end function draw()gfx.text(0,0,string.char(0xc0,0x80),20,0,0,0)end","UTF-8");
 fail("function update()end function draw()local s='x' for i=1,8 do s=s..s end gfx.text(0,0,s,20,0,0,0)end","192 bytes");
 fail("function update()end function draw()local s='x' for i=1,14 do s=s..s end return string.lower(s)end","string input limit");
 fail("function update()state.set(string.char(255))end function draw()end","UTF-8");
 fail("function update()end function draw()local s='x' for i=1,15 do s=s..s end state.set(s)end","16384 bytes");
 fail("function update()end","define draw");
 fail("```lua\nfunction update()end function draw()end\n```","unexpected");
 const char bytecode[]={27,'L','u','a',0x54};
 assert(muse_local_app_create(bytecode,sizeof(bytecode),NULL,&app,error,sizeof(error))&&strstr(error,"binary"));
 char*big=malloc(MUSE_LOCAL_SOURCE_MAX+1);memset(big,' ',MUSE_LOCAL_SOURCE_MAX+1);assert(muse_local_app_create(big,MUSE_LOCAL_SOURCE_MAX+1,NULL,&app,error,sizeof(error)));free(big);
 const char*s="local first=true function update(dt,input) if first then assert(state.get()=='saved') first=false end assert(dt==.1) assert(input.lx==1 and input.ly==-1) state.set('changed') end function draw()gfx.clear(1,2,3)end";
 assert(!muse_local_app_create(s,strlen(s),NULL,&app,error,sizeof(error)));
 assert(!muse_local_app_set_state(app,"saved",5));const muse_local_draw_list*draw;
 muse_local_input in={0};in.lx=999;in.ly=-999;
 assert(!muse_local_app_frame(app,100,&in,&draw,error,sizeof(error)));assert(draw->count==1&&draw->commands[0].r==1);
 size_t length;assert(!strcmp(muse_local_app_get_state(app,&length),"changed")&&length==7);muse_local_app_destroy(app);
 int cancelled=1;muse_local_limits limits={0};limits.cancelled=cancel;limits.cancel_context=&cancelled;
 assert(muse_local_app_create("function update()end function draw()end",38,&limits,&app,error,sizeof(error)));
 cancelled=0;s="function update()end function draw()end";
 assert(!muse_local_app_create(s,strlen(s),&limits,&app,error,sizeof(error)));cancelled=1;
 assert(muse_local_app_frame(app,0,NULL,&draw,error,sizeof(error))&&strstr(error,"cancelled"));muse_local_app_destroy(app);
 /* Identical seed gives identical first random value in independent VMs. */
 s="function update()state.set(tostring(math.random()))end function draw()end";char saved[100];
 for(int i=0;i<2;i++){assert(!muse_local_app_create(s,strlen(s),NULL,&app,error,sizeof(error)));assert(!muse_local_app_frame(app,.01,NULL,&draw,error,sizeof(error)));if(!i)snprintf(saved,sizeof(saved),"%s",muse_local_app_get_state(app,NULL));else assert(!strcmp(saved,muse_local_app_get_state(app,NULL)));muse_local_app_destroy(app);}
 /* Failed frames are terminal and expose no partial draw list. */
 s="function update()end function draw()gfx.clear(1,2,3);error('stop')end";
 assert(!muse_local_app_create(s,strlen(s),NULL,&app,error,sizeof(error)));assert(muse_local_app_frame(app,.01,NULL,&draw,error,sizeof(error))&&draw==NULL);assert(muse_local_app_frame(app,.01,NULL,&draw,error,sizeof(error))&&strstr(error,"not running"));muse_local_app_destroy(app);
 puts("PASS local Lua: real draw/input/state, deterministic seed, 60-tick probes, source/bytecode, API isolation, instruction/memory/text/draw quotas, cancellation, terminal errors");return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "local_app.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HOOK_STEP 1000u
struct muse_local_app {
  lua_State *L;
  muse_local_limits limits;
  size_t memory, peak;
  unsigned remaining;
  int drawing, failed;
  uint64_t random;
  double dt;
  muse_local_input input;
  muse_local_draw_list list;
  char state[MUSE_LOCAL_STATE_MAX+1];
  size_t state_length;
};
static const char *buttons[MUSE_LOCAL_BUTTON_COUNT] = {
 "up","down","left","right","cross","circle","square","triangle",
 "l1","r1","l2","r2","options"};
static void message(char *dst, size_t cap, const char *src) {
  if (dst && cap) snprintf(dst,cap,"%s",src ? src : "Lua error");
}
static muse_local_app *owner(lua_State *L) { return *(muse_local_app **)lua_getextraspace(L); }
static void *allocate(void *context, void *ptr, size_t old, size_t size) {
  muse_local_app *a = context;
  if (!ptr) old=0; /* Lua uses osize for the object tag on new allocations. */
  if (!size) { free(ptr); a->memory-=old; return NULL; }
  if (size>a->limits.memory_bytes || a->memory-old>a->limits.memory_bytes-size) return NULL;
  void *next=realloc(ptr,size);
  if (next) { a->memory=a->memory-old+size; if(a->memory>a->peak)a->peak=a->memory; }
  return next;
}
static void hook(lua_State *L, lua_Debug *ar) {
  (void)ar; muse_local_app *a=owner(L);
  if(a->limits.cancelled && a->limits.cancelled(a->limits.cancel_context)) luaL_error(L,"app cancelled");
  if(a->remaining<HOOK_STEP) luaL_error(L,"instruction budget exceeded");
  a->remaining-=HOOK_STEP;
}
static int utf8(const char *s,size_t n) {
  size_t i=0;
  while(i<n) {
    unsigned c=(unsigned char)s[i++], value, need, minimum;
    if(c<0x80) { if(!c)return 0; continue; }
    if(c>=0xc2&&c<=0xdf){value=c&31;need=1;minimum=0x80;}
    else if(c>=0xe0&&c<=0xef){value=c&15;need=2;minimum=0x800;}
    else if(c>=0xf0&&c<=0xf4){value=c&7;need=3;minimum=0x10000;}
    else return 0;
    if(need>n-i)return 0;
    while(need--){c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return 0;value=(value<<6)|(c&63);}
    if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return 0;
  }
  return 1;
}
static float number(lua_State *L,int slot,double low,double high) {
  double v=luaL_checknumber(L,slot);
  if(!__builtin_isfinite(v)||v<low||v>high)luaL_error(L,"argument %d outside range",slot);
  return (float)v;
}
static muse_local_draw_command *command(lua_State *L, enum muse_local_draw_type type) {
  muse_local_app *a=owner(L);
  if(!a->drawing)luaL_error(L,"gfx is only available inside draw()");
  if(a->list.count==MUSE_LOCAL_DRAW_MAX)luaL_error(L,"draw command limit exceeded");
  muse_local_draw_command *c=&a->list.commands[a->list.count++];
  memset(c,0,sizeof(*c)); c->type=type;c->a=255;return c;
}
static void color(lua_State *L,muse_local_draw_command *c,int slot) {
  c->r=(uint8_t)number(L,slot,0,255);c->g=(uint8_t)number(L,slot+1,0,255);
  c->b=(uint8_t)number(L,slot+2,0,255);
  if(!lua_isnoneornil(L,slot+3))c->a=(uint8_t)number(L,slot+3,0,255);
}
static int clear(lua_State *L){muse_local_draw_command*c=command(L,MUSE_LOCAL_CLEAR);color(L,c,1);return 0;}
static int rect(lua_State *L){muse_local_draw_command*c=command(L,MUSE_LOCAL_RECT);c->x=number(L,1,-8192,8192);c->y=number(L,2,-8192,8192);c->w=number(L,3,0,4096);c->h=number(L,4,0,4096);color(L,c,5);return 0;}
static int circle(lua_State *L){muse_local_draw_command*c=command(L,MUSE_LOCAL_CIRCLE_DRAW);c->x=number(L,1,-8192,8192);c->y=number(L,2,-8192,8192);c->radius=number(L,3,0,4096);color(L,c,4);return 0;}
static int line(lua_State *L){muse_local_draw_command*c=command(L,MUSE_LOCAL_LINE);c->x=number(L,1,-8192,8192);c->y=number(L,2,-8192,8192);c->w=number(L,3,-8192,8192);c->h=number(L,4,-8192,8192);c->thickness=number(L,5,0.1,64);color(L,c,6);return 0;}
static int text(lua_State *L){
  muse_local_draw_command*c=command(L,MUSE_LOCAL_TEXT);size_t n;
  c->x=number(L,1,-8192,8192);c->y=number(L,2,-8192,8192);
  const char*s=luaL_checklstring(L,3,&n);
  if(n>MUSE_LOCAL_TEXT_MAX||!utf8(s,n))return luaL_error(L,"text must be UTF-8, no NUL, at most 192 bytes");
  memcpy(c->text,s,n);c->text[n]=0;c->font_size=number(L,4,8,96);color(L,c,5);return 0;
}
static int state_get(lua_State *L){muse_local_app*a=owner(L);lua_pushlstring(L,a->state,a->state_length);return 1;}
static int state_set(lua_State *L){muse_local_app*a=owner(L);size_t n;const char*s=luaL_checklstring(L,1,&n);if(n>MUSE_LOCAL_STATE_MAX||!utf8(s,n))return luaL_error(L,"state must be UTF-8, at most 16384 bytes");memcpy(a->state,s,n);a->state[n]=0;a->state_length=n;return 0;}
static uint64_t random64(muse_local_app*a){uint64_t z=(a->random+=UINT64_C(0x9e3779b97f4a7c15));z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);return z^(z>>31);}
static int random_seed(lua_State*L){owner(L)->random=(uint64_t)luaL_checkinteger(L,1);return 0;}
static int random_number(lua_State*L){
  uint64_t value=random64(owner(L));int count=lua_gettop(L);
  if(!count){lua_pushnumber(L,(lua_Number)(value>>11)*(1.0/9007199254740992.0));return 1;}
  lua_Integer low=count==1?1:luaL_checkinteger(L,1), high=luaL_checkinteger(L,count==1?1:2);
  if(count>2||low>high||low < -1000000000||high > 1000000000)return luaL_error(L,"random range must be within +/-1000000000");
  lua_pushinteger(L,low+(lua_Integer)(value%(uint64_t)(high-low+1)));return 1;
}
/* Restrict all exported string-library calls to small inputs/results. No pattern
 * engine, packing, formatting, or string repetition is exposed (C code bypasses hooks). */
static int bounded_string(lua_State*L){
  int count=lua_gettop(L);if(count>256)return luaL_error(L,"string argument limit exceeded");
  for(int i=1;i<=count;i++)if(lua_type(L,i)==LUA_TSTRING && lua_rawlen(L,i)>8192)return luaL_error(L,"string input limit exceeded");
  lua_pushvalue(L,lua_upvalueindex(1));lua_insert(L,1);lua_call(L,count,LUA_MULTRET);
  return lua_gettop(L);
}
static int table_insert(lua_State*L){
  luaL_checktype(L,1,LUA_TTABLE);size_t n=lua_rawlen(L,1);int argc=lua_gettop(L);lua_Integer at=argc==2?(lua_Integer)n+1:luaL_checkinteger(L,2);
  if(n>=2048||at<1||at>(lua_Integer)n+1||(argc!=2&&argc!=3))return luaL_error(L,"table.insert limit/range");
  for(lua_Integer i=(lua_Integer)n;i>=at;i--){lua_rawgeti(L,1,i);lua_rawseti(L,1,i+1);}lua_pushvalue(L,argc);lua_rawseti(L,1,at);return 0;
}
static int table_remove(lua_State*L){luaL_checktype(L,1,LUA_TTABLE);size_t n=lua_rawlen(L,1);lua_Integer at=luaL_optinteger(L,2,(lua_Integer)n);if(n>2048||at<1||at>(lua_Integer)n)return luaL_error(L,"table.remove limit/range");lua_rawgeti(L,1,at);for(lua_Integer i=at;i<(lua_Integer)n;i++){lua_rawgeti(L,1,i+1);lua_rawseti(L,1,i);}lua_pushnil(L);lua_rawseti(L,1,(lua_Integer)n);return 1;}
static void keep(lua_State*L,int source,int dest,const char*name){lua_getfield(L,source,name);lua_setfield(L,dest,name);}
static const luaL_Reg graphics[]={{"clear",clear},{"rect",rect},{"circle",circle},{"line",line},{"text",text},{NULL,NULL}};
static int initialize(lua_State*L){
  /* Open only these libraries, then replace the global table with a whitelist.
   * No registry, package loader, OS, IO, debug or coroutine handle is reachable. */
  luaL_requiref(L,"_G",luaopen_base,1);lua_pop(L,1);
  luaL_requiref(L,"math",luaopen_math,1);lua_pop(L,1);
  luaL_requiref(L,"string",luaopen_string,1);lua_pop(L,1);
  lua_pushglobaltable(L);int old=lua_gettop(L);lua_newtable(L);int safe=lua_gettop(L);
  const char*allowed[]={"assert","error","ipairs","next","pairs","select","tonumber","tostring","type","_VERSION",NULL};
  for(int i=0;allowed[i];i++)keep(L,old,safe,allowed[i]);
  lua_getfield(L,old,"math");lua_pushcfunction(L,random_number);lua_setfield(L,-2,"random");lua_pushcfunction(L,random_seed);lua_setfield(L,-2,"randomseed");lua_setfield(L,safe,"math");
  lua_getfield(L,old,"string");int original=lua_gettop(L);lua_newtable(L);int strings=lua_gettop(L);
  const char*strs[]={"byte","char","len","lower","reverse","sub","upper",NULL};
  for(int i=0;strs[i];i++){lua_getfield(L,original,strs[i]);lua_pushcclosure(L,bounded_string,1);lua_setfield(L,strings,strs[i]);}
  /* The string metatable must point to the restricted table too. */
  lua_pushliteral(L,"");lua_getmetatable(L,-1);lua_pushvalue(L,strings);lua_setfield(L,-2,"__index");lua_pop(L,2);
  lua_pushvalue(L,strings);lua_setfield(L,safe,"string");lua_pop(L,2);
  lua_newtable(L);lua_pushcfunction(L,table_insert);lua_setfield(L,-2,"insert");lua_pushcfunction(L,table_remove);lua_setfield(L,-2,"remove");lua_setfield(L,safe,"table");
  luaL_newlib(L,graphics);lua_setfield(L,safe,"gfx");
  lua_newtable(L);lua_pushcfunction(L,state_get);lua_setfield(L,-2,"get");lua_pushcfunction(L,state_set);lua_setfield(L,-2,"set");lua_setfield(L,safe,"state");
  lua_pushinteger(L,MUSE_LOCAL_WIDTH);lua_setfield(L,safe,"WIDTH");lua_pushinteger(L,MUSE_LOCAL_HEIGHT);lua_setfield(L,safe,"HEIGHT");
  lua_pushvalue(L,safe);lua_setfield(L,safe,"_G");lua_pushvalue(L,safe);lua_rawseti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
  return 0;
}
static int check_functions(lua_State*L){lua_getglobal(L,"update");if(!lua_isfunction(L,-1))return luaL_error(L,"define update(dt,input)");lua_getglobal(L,"draw");if(!lua_isfunction(L,-1))return luaL_error(L,"define draw()");return 0;}
static double axis(float v){return !__builtin_isfinite(v)?0:v < -1?-1:v > 1?1:v;}
static int tick(lua_State*L){
  muse_local_app*a=owner(L);lua_getglobal(L,"update");lua_pushnumber(L,a->dt);lua_newtable(L);
  for(int i=0;i<MUSE_LOCAL_BUTTON_COUNT;i++){lua_pushboolean(L,(a->input.down>>i)&1);lua_setfield(L,-2,buttons[i]);}
  lua_newtable(L);for(int i=0;i<MUSE_LOCAL_BUTTON_COUNT;i++){lua_pushboolean(L,(a->input.pressed>>i)&1);lua_setfield(L,-2,buttons[i]);}lua_setfield(L,-2,"pressed");
  const char*axes[]={"lx","ly","rx","ry"};float values[]={a->input.lx,a->input.ly,a->input.rx,a->input.ry};for(int i=0;i<4;i++){lua_pushnumber(L,axis(values[i]));lua_setfield(L,-2,axes[i]);}
  lua_call(L,2,0);a->drawing=1;lua_getglobal(L,"draw");lua_call(L,0,0);a->drawing=0;return 0;
}
static int protected_call(muse_local_app*a,lua_CFunction fn,char*error,size_t cap){
  lua_pushcfunction(a->L,fn);int rc=lua_pcall(a->L,0,0,0);
  if(rc!=LUA_OK){message(error,cap,(lua_type(a->L,-1)==LUA_TSTRING?lua_tostring(a->L,-1):"Lua raised a non-string error"));lua_pop(a->L,1);a->failed=1;return -1;}return 0;
}
int muse_local_app_create(const char*source,size_t length,const muse_local_limits*limits,muse_local_app**out,char*error,size_t cap){
  if(out)*out=NULL;message(error,cap,"");
  if(!out||!source||!length||length>MUSE_LOCAL_SOURCE_MAX||memchr(source,0,length)){message(error,cap,"source must be nonempty text, at most 65536 bytes, without NUL");return -1;}
  muse_local_app*a=calloc(1,sizeof(*a));if(!a){message(error,cap,"host allocation failed");return -1;}
  if(limits)a->limits=*limits;
  if(!a->limits.memory_bytes)a->limits.memory_bytes=8u*1024u*1024u;
  if(!a->limits.instructions)a->limits.instructions=200000;
  if(a->limits.memory_bytes<256u*1024u||a->limits.memory_bytes>16u*1024u*1024u||a->limits.instructions<1000||a->limits.instructions>1000000){message(error,cap,"invalid runtime limits");free(a);return -1;}
  a->random=a->limits.seed;a->remaining=a->limits.instructions;
  if(a->limits.cancelled&&a->limits.cancelled(a->limits.cancel_context)){message(error,cap,"app cancelled");free(a);return -1;}
  a->L=lua_newstate(allocate,a);if(!a->L){message(error,cap,"Lua allocation failed");free(a);return -1;}
  *(muse_local_app**)lua_getextraspace(a->L)=a;lua_sethook(a->L,hook,LUA_MASKCOUNT,HOOK_STEP);
  if(protected_call(a,initialize,error,cap))goto fail;
  if(luaL_loadbufferx(a->L,source,length,"app.lua","t")!=LUA_OK||lua_pcall(a->L,0,0,0)!=LUA_OK){message(error,cap,(lua_type(a->L,-1)==LUA_TSTRING?lua_tostring(a->L,-1):"Lua raised a non-string error"));goto fail;}
  if(protected_call(a,check_functions,error,cap))goto fail;
  *out=a;return 0;
fail:muse_local_app_destroy(a);return -1;
}
int muse_local_app_frame(muse_local_app*a,double dt,const muse_local_input*input,const muse_local_draw_list**draw,char*error,size_t cap){
  if(draw)*draw=NULL;message(error,cap,"");
  if(!a||!draw||a->failed){message(error,cap,"app is not running");return -1;}
  if(a->limits.cancelled&&a->limits.cancelled(a->limits.cancel_context)){message(error,cap,"app cancelled");a->failed=1;return -1;}
  a->dt=!__builtin_isfinite(dt)||dt<0?0:dt>0.1?0.1:dt;a->input=input?*input:(muse_local_input){0};a->list.count=0;a->remaining=a->limits.instructions;a->drawing=0;
  if(protected_call(a,tick,error,cap)){a->list.count=0;a->drawing=0;return -1;}
  *draw=&a->list;return 0;
}
int muse_local_app_set_state(muse_local_app*a,const char*s,size_t n){if(!a||!s||n>MUSE_LOCAL_STATE_MAX||!utf8(s,n))return -1;memcpy(a->state,s,n);a->state[n]=0;a->state_length=n;return 0;}
const char*muse_local_app_get_state(const muse_local_app*a,size_t*n){if(n)*n=a?a->state_length:0;return a?a->state:NULL;}
size_t muse_local_app_peak_memory(const muse_local_app*a){return a?a->peak:0;}
void muse_local_app_destroy(muse_local_app*a){if(a){if(a->L)lua_close(a->L);free(a);}}
/* Conservative display proof, not a rasterizer or an app-quality score. The
 * player begins each frame on a uniform background. Only complete opaque fills
 * can prove that earlier varied content was covered. Unknown geometry/colors
 * count as potentially visible, so thin/translucent graphics are not rejected. */
static int potentially_visible(const muse_local_draw_list*draw,size_t*hidden_text){
  int uniform=1,known=0;uint8_t r=0,g=0,b=0;size_t pending_text=0;
  for(size_t i=0;i<draw->count;i++){
    const muse_local_draw_command*c=&draw->commands[i];
    int text_command=c->type==MUSE_LOCAL_TEXT&&c->text[0];
    if(!c->a){if(text_command)(*hidden_text)++;continue;}
    if(c->type==MUSE_LOCAL_TEXT&&!c->text[0])continue;
    int full=c->type==MUSE_LOCAL_CLEAR||(c->type==MUSE_LOCAL_RECT&&
      c->x<=0&&c->y<=0&&c->x+c->w>=MUSE_LOCAL_WIDTH&&c->y+c->h>=MUSE_LOCAL_HEIGHT);
    int same=known&&c->r==r&&c->g==g&&c->b==b;
    if(text_command){if(uniform&&same)(*hidden_text)++;else pending_text++;}
    if(full&&c->a==255){*hidden_text+=pending_text;pending_text=0;uniform=known=1;r=c->r;g=c->g;b=c->b;}
    else if(full&&uniform){if(!same)known=0;}
    else if(uniform&&!same){uniform=0;known=0;}
  }
  return !uniform;
}
int muse_local_app_validate(const char*s,size_t n,const muse_local_limits*limits,unsigned ticks,muse_local_validation*report,char*error,size_t cap){
  if(report)memset(report,0,sizeof(*report));if(!report||!ticks||ticks>120){message(error,cap,"validation ticks must be1..120");return -1;}
  muse_local_app*a=NULL;if(muse_local_app_create(s,n,limits,&a,error,cap))return -1;
  int rc=0,visible=0;for(unsigned i=0;i<ticks;i++){
    muse_local_input in={0};if(i){in.down=1u<<((i-1)%MUSE_LOCAL_BUTTON_COUNT);in.pressed=in.down;in.lx=(i&1)?-1:1;in.ly=-in.lx;in.rx=in.ly;in.ry=in.lx;}
    const muse_local_draw_list*draw=NULL;if(muse_local_app_frame(a,1.0/60.0,&in,&draw,error,cap)){rc=-1;break;}
    report->ticks++;if(draw->count>report->maximum_draw_commands)report->maximum_draw_commands=draw->count;
    visible|=potentially_visible(draw,&report->provably_hidden_text_commands);
  }
  if(!rc&&!visible){message(error,cap,"no visible content: every validation frame was empty or a uniform background; draw contrasting text or graphics");rc=-1;}
  report->peak_memory_bytes=a->peak;muse_local_app_destroy(a);return rc;
}
const char*muse_local_app_prompt_contract(void){return
 "Write only complete Lua 5.4 source, no markdown or prose, at most 65536 bytes. "
 "The host already provides gfx, state, WIDTH and HEIGHT; never redefine or shadow them. "
 "Never declare local state or assign state/gfx. Store state.get() in your own differently named variable. "
 "Only implement your app: global update(dt,input) and draw(). Persistent ordinary variables/tables hold app state. "
 "input exists ONLY as the parameter of update, never as a global. Save values needed by draw in app variables. "
 "Canvas WIDTH=1280 HEIGHT=720. dt seconds clamped0..0.1. "
 "input has boolean up,down,left,right,cross,circle,square,triangle,l1,r1,l2,r2,options; "
 "input.pressed contains the same names for new presses. input.lx,ly,rx,ry are -1..1. "
 "Only draw() may call gfx.clear(r,g,b), gfx.rect(x,y,w,h,r,g,b,a), "
 "gfx.circle(x,y,radius,r,g,b,a), gfx.line(x1,y1,x2,y2,width,r,g,b,a), "
 "gfx.text(x,y,text,font_size,r,g,b,a). Example: gfx.text(20,30,\"Hello\",24,255,255,255). "
 "The fourth text argument is font_size, from8 to96. Alpha a is optional and defaults to255. Colors0..255; text<=192 UTF-8 bytes; "
 "maximum512 commands per frame. Use ordinary Lua operators, math (including deterministic "
 "random/randomseed), string.byte/char/len/lower/reverse/sub/upper, table.insert/remove "
 "(arrays<=2048), assert,error,ipairs,next,pairs,select,tonumber,tostring,type. "
 "No string.format, patterns, table.concat/sort/unpack, OS,io,require,package,debug,load, "
 "coroutines,metatables,pcall,network,files,or native code. 8MiB memory,200000instructions/frame. "
 "state.get() returns a persisted opaque string (empty initially); state.set(str) updates it "
 "(UTF-8<=16384 bytes). Read restored state on first update, not at top level. "
 "Never overwrite update/draw with values other than functions. Show visible contrasting text or graphics, not a blank uniform display. Start playable immediately. "
 "Respect all buttons, but the host may reserve options to exit. No external assets.";}

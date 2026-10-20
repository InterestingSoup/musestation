// Real Linux CPU/model/runtime integration; never labeled console execution.
#include "muse_inference.h"
#include "local_app.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct progress_state {
    muse_inference *engine = nullptr;
    unsigned tokens = 0, stop_at = 0;
    bool immediate_stop = false, speech_checked = false, speech_rejected = false;
    std::string pieces;
};
static int stop(void *context) {
    auto &s = *static_cast<progress_state *>(context);
    return s.immediate_stop || (s.stop_at && s.tokens >= s.stop_at);
}
static void progress(void *context,const char *piece,size_t n,uint32_t tokens) {
    auto &s = *static_cast<progress_state *>(context);
    s.tokens = tokens; s.pieces.append(piece,n);
    if (!s.speech_checked) {
        s.speech_checked = true;
        char error[256]{};
        s.speech_rejected = muse_inference_speak(s.engine,"Must not speak during generation.",
            "/work/dist/codegen-must-not-exist.wav",error,sizeof(error)) != 0 && std::strstr(error,"busy");
    }
}
static bool save(const std::string &path,const char *source) {
    FILE *f=std::fopen(path.c_str(),"wb");
    if(!f)return false;
    const size_t n=std::strlen(source);
    bool ok=std::fwrite(source,1,n,f)==n;
    return std::fclose(f)==0 && ok;
}
static std::string drawn_text(const muse_local_draw_list *draw) {
    std::string result;
    for(size_t i=0;i<draw->count;++i)if(draw->commands[i].type==MUSE_LOCAL_TEXT)
        result+=std::string(draw->commands[i].text)+"\n";
    return result;
}
static bool visible_text(const muse_local_draw_list *draw,std::string &why) {
    float minimum_font=96;
    for(size_t i=0;i<draw->count;++i)if(draw->commands[i].type==MUSE_LOCAL_TEXT && draw->commands[i].text[0])
        minimum_font=std::fmin(minimum_font,draw->commands[i].font_size);
    for(size_t i=0;i<draw->count;++i) {
        const auto &text=draw->commands[i];
        if(text.type!=MUSE_LOCAL_TEXT || !text.text[0])continue;
        int r=-1,g=-1,b=-1;
        for(size_t j=0;j<i;++j) {
            const auto &back=draw->commands[j];
            if(back.type==MUSE_LOCAL_CLEAR || (back.type==MUSE_LOCAL_RECT && back.a==255 &&
               text.x>=back.x && text.y>=back.y && text.x<back.x+back.w && text.y<back.y+back.h)) {
                r=back.r;g=back.g;b=back.b;
            }
        }
        auto luminance=[](double rr,double gg,double bb) {
            auto linear=[](double c) {c/=255;return c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4);};
            return 0.2126*linear(rr)+0.7152*linear(gg)+0.0722*linear(bb);
        };
        double contrast=21;
        if(r>=0) {
            const double alpha=text.a/255.0;
            const double foreground=luminance(text.r*alpha+r*(1-alpha),text.g*alpha+g*(1-alpha),text.b*alpha+b*(1-alpha));
            const double background=luminance(r,g,b);
            contrast=(std::fmax(foreground,background)+0.05)/(std::fmin(foreground,background)+0.05);
        }
        if(!text.a || contrast<4.5) {
            why="Drawing check failed: text at ("+std::to_string(text.x)+","+std::to_string(text.y)+
                ") has foreground RGB("+std::to_string(text.r)+","+std::to_string(text.g)+","+std::to_string(text.b)+
                ") and alpha "+std::to_string(text.a)+" over opaque background RGB("+std::to_string(r)+","+
                std::to_string(g)+","+std::to_string(b)+"). Measured contrast is "+std::to_string(contrast)+
                ":1; this check requires at least4.5:1. "+
                (r+g+b>382 ? "Use black text RGB(0,0,0) with alpha255 on this light panel." :
                             "Use white text RGB(255,255,255) with alpha255 on this dark panel.");
            if(minimum_font<24)why+=" The smallest instruction text is "+std::to_string(minimum_font)+
                " logical pixels; increase all instruction text to at least24 logical pixels for couch readability.";
            return false;
        }
    }
    if(minimum_font<24) {
        why="Drawing check failed: instruction font_size is "+std::to_string(minimum_font)+
            " logical pixels; increase all instruction text to at least24 logical pixels for couch readability.";
        return false;
    }
    return true;
}
static bool validate_counter(const char *source,std::string &why) {
    why.clear();
    char error[1024]{};
    muse_local_limits limits{};
    muse_local_validation report{};
    if(muse_local_app_validate(source,std::strlen(source),&limits,60,&report,error,sizeof(error))) {
        why=error;return false;
    }
    muse_local_app *app=nullptr;
    if(muse_local_app_create(source,std::strlen(source),&limits,&app,error,sizeof(error))) {
        why=error;return false;
    }
    const muse_local_draw_list *draw=nullptr;
    muse_local_input input{};
    std::vector<std::string> screens;
    bool ok=true;
    for(unsigned step=0;step<7;++step) {
        input={};
        if(step==1 || step==3)input.down=input.pressed=1u<<MUSE_LOCAL_CROSS;
        if(step==2 || step==4)input.down=1u<<MUSE_LOCAL_CROSS;
        if(step==5)input.down=input.pressed=1u<<MUSE_LOCAL_CIRCLE;
        if(muse_local_app_frame(app,1.0/60,&input,&draw,error,sizeof(error))) {ok=false;why=error;break;}
        if(!visible_text(draw,why)){ok=false;break;}
        screens.push_back(drawn_text(draw));
    }
    muse_local_app_destroy(app);
    if(ok) {
        bool exact_count=false;
        for(size_t at=0;at<screens[0].size();++at)if(screens[0][at]=='0') {
            std::string one=screens[0],two=screens[0];one[at]='1';two[at]='2';
            if(screens[1]==one && screens[2]==one && screens[3]==two && screens[4]==two &&
               screens[5]==screens[0] && screens[6]==screens[0])exact_count=true;
        }
        if(!exact_count) {
            why="Behavior test failed: displayed integer must be 0 initially, 1 after the first Cross press, 2 after the second, unchanged while held without a new press, and 0 after Circle; keep other text static.";
            ok=false;
        }
    }
    if(ok)std::printf("COUNTER runtime/behavior PASS: ticks=%u peak=%zu max_draw=%zu\n",report.ticks,report.peak_memory_bytes,report.maximum_draw_commands);
    return ok;
}
static bool validate_movement(const char *source,std::string &why) {
    why.clear();
    char error[1024]{};muse_local_limits limits{};muse_local_validation report{};
    if(muse_local_app_validate(source,std::strlen(source),&limits,60,&report,error,sizeof(error))) {
        why=error;return false;
    }
    muse_local_app *app=nullptr;
    if(muse_local_app_create(source,std::strlen(source),&limits,&app,error,sizeof(error))) {
        why=error;return false;
    }
    bool ok=true;float x=620,y=340;unsigned ticks=0;
    auto frame=[&](uint32_t down,uint32_t pressed) {
        if(!ok)return;
        muse_local_input input{};input.down=down;input.pressed=pressed;
        if(down&(1u<<MUSE_LOCAL_RIGHT))x=std::fmin(1240.0f,x+24);
        if(down&(1u<<MUSE_LOCAL_LEFT))x=std::fmax(0.0f,x-24);
        if(down&(1u<<MUSE_LOCAL_DOWN))y=std::fmin(680.0f,y+24);
        if(down&(1u<<MUSE_LOCAL_UP))y=std::fmax(0.0f,y-24);
        const muse_local_draw_list *draw=nullptr;
        ++ticks;
        if(muse_local_app_frame(app,0.1,&input,&draw,error,sizeof(error))) {ok=false;why=error;return;}
        if(!visible_text(draw,why)){ok=false;return;}
        const muse_local_draw_command *square=nullptr;
        for(size_t i=0;i<draw->count;++i) {
            const auto &c=draw->commands[i];
            if(c.type==MUSE_LOCAL_RECT && std::fabs(c.w-40)<0.01 && std::fabs(c.h-40)<0.01)square=&c;
        }
        if(!square || std::fabs(square->x-x)>0.01 || std::fabs(square->y-y)>0.01) {
            why="Movement check failed at tick "+std::to_string(ticks)+": expected 40x40 square at ("+
                std::to_string(x)+","+std::to_string(y)+"). Use continuous input direction booleans, 240 pixels/second * dt, and clamp top-left x to0..1240/y to0..680.";
            if(square)why+=" Actual ("+std::to_string(square->x)+","+std::to_string(square->y)+").";
            ok=false;
        }
    };
    frame(0,0);
    frame(1u<<MUSE_LOCAL_RIGHT,1u<<MUSE_LOCAL_RIGHT);
    frame(1u<<MUSE_LOCAL_RIGHT,0);
    frame(1u<<MUSE_LOCAL_LEFT,1u<<MUSE_LOCAL_LEFT);
    frame(1u<<MUSE_LOCAL_UP,1u<<MUSE_LOCAL_UP);
    frame(1u<<MUSE_LOCAL_DOWN,1u<<MUSE_LOCAL_DOWN);
    for(unsigned i=0;i<60;++i)frame((1u<<MUSE_LOCAL_RIGHT)|(1u<<MUSE_LOCAL_DOWN),0);
    for(unsigned i=0;i<60;++i)frame((1u<<MUSE_LOCAL_LEFT)|(1u<<MUSE_LOCAL_UP),0);
    frame(0,0);
    muse_local_app_destroy(app);
    if(ok)std::printf("MOVEMENT runtime/held-controls/four-bounds PASS: ticks=%u peak=%zu\n",ticks,report.peak_memory_bytes);
    return ok;
}
int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    const bool movement=argc==6 && std::strcmp(argv[5],"movement")==0;
    const bool repair_only=argc==7 && std::strcmp(argv[5],"repair-counter")==0;
    if(argc!=5 && !movement && !repair_only){std::fprintf(stderr,"usage: %s qwen.gguf whisper.bin espeak-data-parent output-prefix [movement | repair-counter actual-source.lua]\n",argv[0]);return 2;}
    unsigned maximum_attempts=5;
    if(const char *value=std::getenv("MUSE_CHECK_ATTEMPTS")) {
        char *end=nullptr;const unsigned long n=std::strtoul(value,&end,10);
        if(!value[0] || !end || *end || n<1 || n>5) {
            std::fprintf(stderr,"MUSE_CHECK_ATTEMPTS must be1..5\n");return 2;
        }
        maximum_attempts=static_cast<unsigned>(n);
    }
    muse_inference_config cfg{argv[1],argv[2],argv[3],"en",2};
    cfg.backend_policy=MUSE_BACKEND_CPU;
    char error[1024]{};
    auto *engine=muse_inference_create(&cfg,error,sizeof(error));
    if(!engine){std::fprintf(stderr,"init: %s\n",error);return 1;}
    int failures=0;
    auto check=[&](bool ok,const char *name){std::printf("%s: %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};
    std::vector<char> source(MUSE_LOCAL_SOURCE_MAX+1);
    muse_codegen_stats stats{};
    progress_state state;state.engine=engine;
    muse_codegen_request request{};
    request.system_prompt=muse_local_app_prompt_contract();
    request.request="Create a simple counter utility. The integer starts at zero. Each input.pressed.cross adds exactly one. input.pressed.circle resets it to zero. Display the count and a short static instruction. No animation or other features.";
    request.approved_plan="Make a fullscreen counter. Cross increments once per press; Circle resets. Draw a large count and short instructions.";
    request.max_output_tokens=1200;request.max_duration_ms=120000;request.seed=17;
    request.should_stop=stop;request.progress=progress;request.userdata=&state;
    auto run=[&](){
        int rc=muse_inference_codegen(engine,&request,source.data(),source.size(),&stats,error,sizeof(error));
        std::printf("CODEGEN result=%d prompt=%u generated=%u context=%u elapsed_ms=%llu bytes=%zu error=%s\n",
            rc,stats.prompt_tokens,stats.generated_tokens,stats.context_tokens,
            static_cast<unsigned long long>(stats.elapsed_ms),std::strlen(source.data()),error);
        std::fflush(stdout);return rc;
    };
    if(movement || repair_only) {
        std::string previous,diagnostic;
        if(movement) {
            request.request="Make a simple D-pad square app. Draw one filled 40 by 40 pixel square with its initial top-left at x = 620, y = 340 on a contrasting background. Holding left/right/up/down moves it continuously at 240 pixels per second using dt. Diagonal holds move on both axes. Clamp its top-left to x in 0..1240, y in 0..680. Draw short static instructions. No motion without input and no other features.";
            request.approved_plan="Show a 40-pixel square starting in the center. The D-pad moves it at 240 pixels per second; keep the entire square inside 1280x720 bounds. Show static controls.";
        } else {
            FILE *f=std::fopen(argv[6],"rb");
            if(!f){std::fprintf(stderr,"cannot open actual generated candidate\n");muse_inference_destroy(engine);return 1;}
            const size_t bytes=std::fread(source.data(),1,source.size(),f);
            bool read_ok=!std::ferror(f) && bytes<source.size();std::fclose(f);
            if(!read_ok){std::fprintf(stderr,"actual candidate exceeds source bound or read failed\n");muse_inference_destroy(engine);return 1;}
            source[bytes]=0;previous=source.data();
            check(!validate_counter(source.data(),diagnostic),"actual generated candidate fails drawing/behavior check");
            std::printf("ACTUAL CANDIDATE DIAGNOSTIC: %s\n",diagnostic.c_str());
        }
        bool valid=false;
        for(unsigned attempt=0;attempt<maximum_attempts && !valid;++attempt) {
            request.previous_source=previous.empty()?nullptr:previous.c_str();
            request.diagnostics=diagnostic.empty()?nullptr:diagnostic.c_str();
            request.seed=(movement?53:71)+attempt;
            int rc=run();
            check(save(std::string(argv[4])+"-attempt-"+std::to_string(attempt)+".lua",source.data()),"save actual generated candidate");
            previous=source.data();
            if(rc==MUSE_CODEGEN_COMPLETE)valid=movement?validate_movement(source.data(),diagnostic):validate_counter(source.data(),diagnostic);
            else diagnostic=error;
            std::printf("VALIDATOR attempt=%u %s %s\n",attempt,valid?"PASS":"FAIL",diagnostic.c_str());
        }
        check(valid,movement?"real model generated a D-pad app with tested movement and bounds":"real model repaired actual drawing issue with held-button behavior preserved");
        if(valid)check(save(std::string(argv[4])+"-valid.lua",source.data()),"save validated source");
        muse_inference_destroy(engine);return failures?1:0;
    }
    state.immediate_stop=true;
    check(run()==MUSE_CODEGEN_STOPPED && stats.generated_tokens==0 && !source[0],"stop before any generation");
    state.immediate_stop=false;
    request.context_tokens=8193;
    check(run()==MUSE_CODEGEN_ERROR && !source[0],"reject oversized context");
    request.context_tokens=8192;
    std::string too_long;for(int i=0;i<9000;++i)too_long+="word ";
    request.previous_source=too_long.c_str();
    check(run()==MUSE_CODEGEN_LIMIT && stats.prompt_tokens>8192 && !source[0],"actual model token budget rejection");
    request.previous_source=nullptr;request.max_output_tokens=1;
    check(run()==MUSE_CODEGEN_LIMIT && stats.generated_tokens==1 && source[0],"one output token is not successful completion");
    check(state.speech_checked && state.speech_rejected,"reentrant speech is rejected during generation");
    state.pieces.clear();state.tokens=0;state.stop_at=8;request.max_output_tokens=8;
    check(run()==MUSE_CODEGEN_STOPPED && stats.generated_tokens==8 && state.pieces==source.data(),"streaming stop at token limit preserves exact partial source");
    state.stop_at=0;state.pieces.clear();state.tokens=0;
    request.max_output_tokens=1200;
    request.max_duration_ms=1;
    check(run()==MUSE_CODEGEN_LIMIT && std::strstr(error,"duration"),"cooperative duration limit");
    request.max_duration_ms=120000;
    char tiny[2]{};
    int rc=muse_inference_codegen(engine,&request,tiny,sizeof(tiny),&stats,error,sizeof(error));
    check(rc==MUSE_CODEGEN_LIMIT && std::memchr(tiny,0,sizeof(tiny)),"small output buffer terminates and cannot succeed");

    std::string previous,diagnostic;
    bool valid=false;
    for(unsigned attempt=0;attempt<maximum_attempts && !valid;++attempt) {
        state.pieces.clear();state.tokens=0;request.seed=17+attempt;
        request.previous_source=previous.empty()?nullptr:previous.c_str();
        request.diagnostics=diagnostic.empty()?nullptr:diagnostic.c_str();
        rc=run();
        check(save(std::string(argv[4])+"-attempt-"+std::to_string(attempt)+".lua",source.data()),"save actual generated candidate");
        previous=source.data();
        if(rc==MUSE_CODEGEN_COMPLETE)valid=validate_counter(source.data(),diagnostic);
        else diagnostic=error;
        std::printf("VALIDATOR attempt=%u %s %s\n",attempt,valid?"PASS":"FAIL",diagnostic.c_str());
    }
    check(valid,"real local model generated a runnable counter with tested controls");
    if(valid) {
        check(save(std::string(argv[4])+"-valid.lua",source.data()),"save validated source");
        // A deliberate source fault proves that repair consumes an actual
        // parser diagnostic; it is labeled separately from model mistakes.
        previous=std::string(source.data())+"\nfunction deliberately_incomplete(\n";
        muse_local_validation report{};muse_local_limits limits{};
        check(muse_local_app_validate(previous.c_str(),previous.size(),&limits,10,&report,error,sizeof(error))!=0,
              "injected syntax fault is actually rejected");
        diagnostic=std::string("Repair the syntax error while preserving counter behavior: ")+error;
        bool repaired=false;std::string repair_error;
        for(unsigned attempt=0;attempt<3 && !repaired;++attempt) {
            request.previous_source=previous.c_str();request.diagnostics=diagnostic.c_str();request.seed=41+attempt;
            rc=run();
            repaired=rc==MUSE_CODEGEN_COMPLETE && validate_counter(source.data(),repair_error);
            if(rc!=MUSE_CODEGEN_COMPLETE)repair_error=error;
            std::printf("INJECTED-FAULT REPAIR attempt=%u: %s %s\n",attempt,repaired?"PASS":"FAIL",repair_error.c_str());
            check(save(std::string(argv[4])+"-repair-attempt-"+std::to_string(attempt)+".lua",source.data()),"save actual fault-repair candidate");
            previous=source.data();diagnostic=repair_error;
        }
        check(repaired,"real model repaired an injected syntax fault from real parser feedback");
        check(save(std::string(argv[4])+"-repair.lua",source.data()),"save actual repair output");
    }
    char draft[4096]{};
    check(muse_inference_draft(engine,"Make a simple clock",draft,sizeof(draft),error,sizeof(error))==0,
          "normal planning still works after code generation and cancellation");
    muse_inference_destroy(engine);
    return failures?1:0;
}

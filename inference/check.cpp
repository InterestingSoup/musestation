// Linux Docker validation harness. This is never shipped as a PS5 executable.
#include "muse_inference.h"
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
static bool complete_plan(const char *text) {
    size_t n=std::strlen(text),words=0,sentences=0;
    bool word=false;
    for(size_t i=0;i<n;++i) {
        unsigned char c=static_cast<unsigned char>(text[i]);
        if(std::isspace(c))word=false;
        else if(!word){++words;word=true;}
        if(c=='.' || c=='!' || c=='?')++sentences;
    }
    while(n && std::isspace(static_cast<unsigned char>(text[n-1])))--n;
    return n>=40 && n<=700 && words<=90 && sentences>=2 && sentences<=4 &&
        (text[n-1]=='.' || text[n-1]=='!' || text[n-1]=='?') &&
        !std::strstr(text,"```") && !std::strstr(text,"<think>");
}
static unsigned le32(const unsigned char *p) {
    return unsigned(p[0]) | unsigned(p[1])<<8 | unsigned(p[2])<<16 | unsigned(p[3])<<24;
}
static bool spoken_roundtrip(muse_inference *engine,const char *path,char *error,size_t error_cap) {
    if(muse_inference_speak(engine,"Your plan is ready. Press cross to approve.",path,error,error_cap))return false;
    FILE *f=std::fopen(path,"rb");unsigned char header[44]{};
    if(!f)return false;
    bool ok=std::fread(header,1,sizeof header,f)==sizeof header;
    const unsigned bytes=le32(header+40),rate=le32(header+24);
    ok=ok && !std::memcmp(header,"RIFF",4) && !std::memcmp(header+8,"WAVEfmt ",8) &&
       !std::memcmp(header+36,"data",4) && header[20]==1 && header[22]==1 && header[34]==16 &&
       bytes>0 && !(bytes%2) && bytes<=8*1024*1024 && rate>=8000 && rate<=48000;
    std::vector<int16_t> pcm(ok?bytes/2:0);
    if(ok)ok=std::fread(pcm.data(),2,pcm.size(),f)==pcm.size() && std::fgetc(f)==EOF;
    std::fclose(f);
    if(!ok){std::snprintf(error,error_cap,"generated WAV failed bounded PCM checks");return false;}
    char text[4096]{};
    if(muse_inference_transcribe(engine,pcm.data(),pcm.size(),rate,text,sizeof text,error,error_cap))return false;
    std::string normalized;
    for(const unsigned char *at=reinterpret_cast<const unsigned char *>(text);*at;++at)
        if(std::isalnum(*at))normalized+=static_cast<char>(std::tolower(*at));
    std::printf("VOICE-ONLY TRANSCRIPT: %s\n",text);
    if(normalized!="yourplanisreadypresscrosstoapprove") {
        std::snprintf(error,error_cap,"actual speech did not transcribe to the expected sentence");return false;
    }
    return true;
}
static int voice_only_check(char **argv) {
    int failures=0;char error[1024]{},text[4096]{},status[2048]{};
    auto check=[&](bool good,const char *name){std::printf("%s: %s\n",good?"PASS":"FAIL",name);if(!good)++failures;};
    const char *backbone=std::getenv("MUSE_CHECK_NEURAL_BACKBONE");
    const char *projector=std::getenv("MUSE_CHECK_NEURAL_PROJECTOR");
    if(!backbone || !projector){std::fprintf(stderr,"voice-only regression requires actual neural model paths\n");return 2;}
    for(unsigned variant=0;variant<2;++variant) {
        muse_inference_config cfg{variant?"":nullptr,argv[2],argv[3],"en",2};
        cfg.backend_policy=MUSE_BACKEND_CPU;
        if(!variant){cfg.neural_backbone=backbone;cfg.neural_projector=projector;}
        auto *engine=muse_inference_create(&cfg,error,sizeof error);
        check(engine!=nullptr,variant?"empty LLM path initializes actual eSpeak/Whisper voice":"null LLM path initializes actual neural/Whisper voice");
        if(!engine){std::fprintf(stderr,"voice-only init: %s\n",error);continue;}
        check(!muse_inference_status(engine,status,sizeof status) && std::strstr(status,"Qwen: disabled (voice-only)"),"status distinguishes absent coder from loaded model");
        const std::string before=status;
        std::strcpy(text,"old output");
        check(muse_inference_draft(engine,"Make Tetris",text,sizeof text,error,sizeof error)!=0 && !text[0] && std::strstr(error,"language model is not configured"),"voice-only draft fails explicitly with empty output");
        muse_local_intent intent=MUSE_INTENT_APPROVE;
        check(muse_inference_classify(engine,"approve this plan","Server plan",&intent,error,sizeof error)!=0 && intent==MUSE_INTENT_UNKNOWN && std::strstr(error,"language model is not configured"),"voice-only model classification fails closed");
        muse_codegen_request request{};request.system_prompt="Write Lua source.";request.request="Make a counter.";
        muse_codegen_stats stats{};std::strcpy(text,"old output");
        check(muse_inference_codegen(engine,&request,text,sizeof text,&stats,error,sizeof error)==MUSE_CODEGEN_ERROR && !text[0] && !stats.generated_tokens && !stats.prompt_tokens && std::strstr(error,"language model is not configured"),"voice-only codegen fails before tokens or partial source");
        check(!muse_inference_status(engine,status,sizeof status) && before==status,"rejected language operations preserve the resident voice engines");
        const std::string path=std::string(argv[4])+(variant?".espeak.wav":".neural.wav");
        const bool speech=spoken_roundtrip(engine,path.c_str(),error,sizeof error);
        if(!speech)std::fprintf(stderr,"actual voice roundtrip: %s\n",error);
        check(speech,variant?"actual eSpeak WAV transcribed by Whisper without coder":"actual neural WAV transcribed after Whisper reload without coder");
        check(!muse_inference_status(engine,status,sizeof status) && std::strstr(status,"Qwen: disabled (voice-only)") &&
              (variant || std::strstr(status,"Qwen3-TTS synthesized")),"speech retains truthful voice-only/backend status");
        std::printf("VOICE-ONLY BACKEND: %s\n",status);
        std::vector<int16_t> silence(16000,0);
        check(muse_inference_transcribe(engine,silence.data(),silence.size(),16000,text,sizeof text,error,sizeof error)!=0 && !text[0],"voice-only silent input is rejected");
        check(muse_inference_transcribe(engine,silence.data(),silence.size(),7999,text,sizeof text,error,sizeof error)!=0,"voice-only invalid sample rate is rejected");
        std::string oversized(4097,'x');
        check(muse_inference_speak(engine,oversized.c_str(),path.c_str(),error,sizeof error)!=0,"voice-only speech input length remains bounded");
        muse_inference_destroy(engine);
    }
    // Omission is intentional voice-only mode; a configured missing coder must
    // still fail so an All Local misinstallation cannot silently downgrade.
    muse_inference_config missing{"/muse-deliberately-missing-coder.gguf",argv[2],argv[3],"en",2};
    missing.backend_policy=MUSE_BACKEND_CPU;
    auto *engine=muse_inference_create(&missing,error,sizeof error);
    check(!engine,"nonempty missing coder still fails initialization");
    if(engine)muse_inference_destroy(engine);
    missing.llm_model=nullptr;missing.whisper_model="/muse-deliberately-missing-whisper.bin";
    engine=muse_inference_create(&missing,error,sizeof error);
    check(!engine,"voice-only missing Whisper still fails initialization");
    if(engine)muse_inference_destroy(engine);
    std::printf("VOICE-ONLY RESULT: failures=%d\n",failures);
    return failures?1:0;
}
int main(int argc, char **argv) {
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    if (argc != 5) { std::fprintf(stderr, "usage: %s qwen.gguf whisper.bin espeak-data-parent output.wav\n", argv[0]); return 2; }
    if(!std::strcmp(argv[1],"--voice-only"))return voice_only_check(argv);
    muse_inference_config cfg = { argv[1], argv[2], argv[3], "en", 2 };
    cfg.neural_backbone = std::getenv("MUSE_CHECK_NEURAL_BACKBONE");
    cfg.neural_projector = std::getenv("MUSE_CHECK_NEURAL_PROJECTOR");
    char error[512], result[4096];
    muse_inference *engine = muse_inference_create(&cfg, error, sizeof(error));
    if (!engine) { std::fprintf(stderr, "init: %s\n", error); return 1; }
    int failures = 0;
    if (muse_inference_draft(engine, "Make me Tetris", result, sizeof(result), error, sizeof(error))) {
        std::fprintf(stderr, "draft: %s\n", error); ++failures;
    } else {
        std::printf("DRAFT: %s\n", result);
        if(!complete_plan(result)){std::fprintf(stderr,"draft is not a complete 2–4 sentence plan\n");++failures;}
    }
    const char *utterances[] = {"Approve this plan.", "Cancel this", "Add a two player mode", "What colors will it use?",
                               "Do not approve this plan", "Approve this plan?", "yes", "approve after adding multiplayer"};
    const muse_local_intent expected[] = {MUSE_INTENT_APPROVE, MUSE_INTENT_CANCEL, MUSE_INTENT_DETAIL, MUSE_INTENT_UNKNOWN,
                                        MUSE_INTENT_UNKNOWN, MUSE_INTENT_UNKNOWN, MUSE_INTENT_UNKNOWN, MUSE_INTENT_UNKNOWN};
    for (int i = 0; i < 8; ++i) {
        muse_local_intent intent = MUSE_INTENT_UNKNOWN;
        if (muse_inference_classify(engine, utterances[i], "Build Tetris with DualSense controls and a score display.", &intent, error, sizeof(error))) {
            std::fprintf(stderr, "classify: %s\n", error); ++failures;
        } else {
            std::printf("INTENT: %s => %d (expected %d)\n", utterances[i], intent, expected[i]);
            // Cancel/detail are safe outcomes for negation, but no other
            // non-command may turn into approval.
            if (i < 4 ? intent != expected[i] : intent == MUSE_INTENT_APPROVE) ++failures;
        }
    }
    if (muse_inference_speak(engine, "Your plan is ready. Press cross to approve.", argv[4], error, sizeof(error))) {
        std::fprintf(stderr, "speak: %s\n", error); ++failures;
    } else {
        std::printf("WAV: %s\n", argv[4]);
        char status[2048]{};
        if (muse_inference_status(engine,status,sizeof(status))) ++failures;
        std::printf("AFTER SPEAK: %s\n",status);
        if (cfg.neural_backbone && (!std::strstr(status,"Qwen3-TTS synthesized") ||
            !std::strstr(status,"unloaded for memory budget"))) ++failures;
        FILE *wav = std::fopen(argv[4], "rb");
        unsigned char header[44];
        if (!wav || std::fread(header, 1, 44, wav) != 44) { ++failures; }
        else {
            unsigned sample_rate = header[24] | header[25] << 8 | header[26] << 16 | header[27] << 24;
            unsigned bytes = header[40] | header[41] << 8 | header[42] << 16 | header[43] << 24;
            std::vector<int16_t> pcm(bytes / 2);
            if (std::fread(pcm.data(), 2, pcm.size(), wav) != pcm.size() ||
                muse_inference_transcribe(engine, pcm.data(), pcm.size(), sample_rate, result, sizeof(result), error, sizeof(error))) {
                std::fprintf(stderr, "transcribe generated speech: %s\n", error); ++failures;
            } else {
                std::printf("TRANSCRIPT: %s\n", result);
                if (!std::strstr(result, "plan")) ++failures;
            }
            std::vector<int16_t> silence(16000, 0);
            if (muse_inference_transcribe(engine, silence.data(), silence.size(), 16000, result, sizeof(result), error, sizeof(error)) == 0) {
                std::fprintf(stderr, "silence unexpectedly produced a transcript\n"); ++failures;
            } else std::printf("SILENCE rejected: %s\n", error);
        }
        if (wav) std::fclose(wav);
    }
    if (cfg.neural_backbone) {
        if (muse_inference_draft(engine,"Make a clock",result,sizeof(result),error,sizeof(error))) {
            std::fprintf(stderr,"reload after neural speech: %s\n",error); ++failures;
        } else {
            std::printf("RELOADED DRAFT: %s\n",result);
            if(!complete_plan(result)){std::fprintf(stderr,"reloaded draft is incomplete\n");++failures;}
        }
    }
    char status[2048]{};
    if (muse_inference_status(engine,status,sizeof(status))) ++failures;
    std::printf("FINAL BACKEND: %s\n",status);
    muse_inference_destroy(engine);
    return failures ? 1 : 0;
}

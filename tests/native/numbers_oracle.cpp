// Test-only C++ oracle; never linked into native Wood outputs.
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../../include/wood/json.hpp"
#include "../../include/wood/filesystem.hpp"
static std::string error_message;
extern "C" {
#include "../../runtime/native/managed.h"
void *wd_platform_alloc(WSize n) { return std::malloc(n); }
void wd_platform_free(void *p,WSize) { std::free(p); }
void wd_rt_error(void *ctx,const char*,WInt,const char *p,WInt n) { ++*static_cast<int*>(ctx); error_message.assign(p,n); }
void wd_rt_error_value(void *ctx,const char *c,WInt n,const WStr *s) { wd_rt_error(ctx,c,n,(const char*)s->data,s->size); }
WInt wd_rt_unwinding(void *ctx) { return *static_cast<int*>(ctx)!=0; }
WInt wd_file_open(const void*,WInt,WInt) { std::abort(); }
WInt wd_file_read(WInt,void*,WInt) { std::abort(); }
WInt wd_file_write(WInt,const void*,WInt) { std::abort(); }
WInt wd_file_close(WInt) { std::abort(); }
WInt wd_file_seek(WInt,WInt,WInt) { std::abort(); }
}
int main() {
    std::uint64_t state=0x32acfe21876d0921ULL; unsigned checked=0;
    for(unsigned i=0;i<20000;++i) {
        state^=state<<13; state^=state>>7; state^=state<<17;
        double value=std::bit_cast<double>(state);
        if(!std::isfinite(value)) continue;
        char buffer[128]; auto end=std::to_chars(buffer,buffer+128,value).ptr;
        std::string expected(buffer,end);
        if(expected.find_first_of(".eE")==std::string::npos) expected+=".0";
        int errors=0; WStr out={}; wd_m_float_str(&errors,&out,value);
        std::string actual((char*)out.data,out.size);
        if(errors || actual!=expected) {
            std::fprintf(stderr,"format %016llx expected %s actual %s errors %d\n",(unsigned long long)state,expected.c_str(),actual.c_str(),errors); wd_m_release(out.owner); return 1;
        }
        WStr input={(unsigned char*)expected.data(),(WInt)expected.size(),nullptr};
        double parsed=wd_m_str_float(&errors,&input);
        if(errors || std::bit_cast<std::uint64_t>(parsed)!=state) { std::fprintf(stderr,"parse %s\n",expected.c_str()); wd_m_release(out.owner); return 1; }
        wd_m_release(out.owner); ++checked;
    }
    for(const char *text:{"5e-324","2.2250738585072014e-308","1.7976931348623157e308","-0.0","9007199254740993.0","0.1000000000000000055511151231257827021181583404541015625"}) {
        int errors=0; WStr s={(unsigned char*)text,(WInt)std::strlen(text),nullptr};
        double actual=wd_m_str_float(&errors,&s),expected=std::strtod(text,nullptr);
        if(errors || std::bit_cast<std::uint64_t>(actual)!=std::bit_cast<std::uint64_t>(expected)) return 2;
    }
    unsigned json_checked=0;
    for(const std::string &text: {std::string("null"),std::string("{\"z\":[true,false,null,-9223372036854775808,1.25],\"a\":\"\\uD83D\\uDE03\"}"),std::string("{\"a\":1,\"a\":2}"),std::string("[1,]"),std::string("01"),std::string("1e309"),std::string("1e-999"),std::string("\"\\uDC00\""),std::string("\"\\ud800z\""),std::string("\"\\x\""),std::string("{\"a\" 1}"),std::string("\"unterminated"),std::string("\"\xff\"",3),std::string(66,'[')+"0"+std::string(66,']')}) {
        std::string expected,expected_error;
        try { expected=wood::Json::parse(text).dump(); } catch(const std::exception &e) { expected_error=e.what(); }
        int errors=0; error_message.clear(); WStr s={(unsigned char*)text.data(),(WInt)text.size(),nullptr};
        WJson *value=(WJson*)wd_m_json_parse(&errors,&s); WStr dumped={};
        if(!errors) wd_m_json_dump(&errors,&dumped,value);
        std::string actual=dumped.size?std::string((char*)dumped.data,dumped.size):"";
        if(expected_error!=error_message || expected!=actual || bool(errors)!=!expected_error.empty()) {
            std::fprintf(stderr,"JSON %s expected [%s] error [%s] actual [%s] error [%s]\n",text.c_str(),expected.c_str(),expected_error.c_str(),actual.c_str(),error_message.c_str());
            wd_m_release(value); wd_m_release(dumped.owner); return 4;
        }
        wd_m_release(value); wd_m_release(dumped.owner); ++json_checked;
    }
    unsigned paths=0;
    for(const std::string &text:{"",".","..","/","//","a/","a//b","a/./b/../c","a/..","a/../","../a/..","../../a/../","/a/../../x","./a/.","a/b/..","a\\b"}) {
        WStr input={(unsigned char*)text.data(),(WInt)text.size(),nullptr}; int errors=0;
        for(int operation=0;operation<3;++operation) {
            WStr out={}; std::filesystem::path p(text);
            auto expected=operation==0?p.lexically_normal().string():operation==1?p.parent_path().string():p.filename().string();
            if(operation==0) wd_m_path_normalize(&errors,&out,&input);
            if(operation==1) wd_m_path_parent(&errors,&out,&input);
            if(operation==2) wd_m_path_name(&errors,&out,&input);
            std::string actual=out.size?std::string((char*)out.data,out.size):""; wd_m_release(out.owner);
            if(errors || actual!=expected) { std::fprintf(stderr,"path op%d [%s] expected [%s] actual [%s]\n",operation,text.c_str(),expected.c_str(),actual.c_str()); return 5; }
            ++paths;
        }
    }
    namespace fs=std::filesystem;
    fs::path fixture=fs::current_path()/"path-oracle-fixtures";
    if(!fs::create_directory(fixture)) { std::fprintf(stderr,"Refusing existing oracle fixture\n"); return 6; }
    fs::create_directory(fixture/"real"); fs::create_directory(fixture/"outside");
    { auto f=std::fopen((fixture/"real"/"file").string().c_str(),"wb"); if(!f) return 6; std::fputs("x",f); std::fclose(f); }
#ifndef _WIN32
    fs::create_directory_symlink("real",fixture/"alias");
    fs::create_symlink("missing",fixture/"dangling");
    fs::create_symlink("cycle",fixture/"cycle");
#endif
    for(const std::string &tail:{"real/file","real/../outside","alias/file","alias/../outside","missing/../real/file","dangling","cycle","real/file/..","real/file/"}) {
        std::string text=(fixture/tail).string(); WStr input={(unsigned char*)text.data(),(WInt)text.size(),nullptr};
        for(int missing=0;missing<=1;++missing) {
            std::string expected; bool failed=false;
            try { expected=wood::filesystem_support::text(wood::filesystem_support::resolve_path(text,missing)); } catch(const std::exception&) { failed=true; }
            int errors=0; WStr out={}; wd_m_path_resolve(&errors,&out,&input,missing);
            std::string actual=out.size?std::string((char*)out.data,out.size):""; wd_m_release(out.owner);
            if(bool(errors)!=failed || (!failed && actual!=expected)) { std::fprintf(stderr,"canonical missing%d [%s] expected [%s] failed%d actual [%s] errors%d\n",missing,tail.c_str(),expected.c_str(),failed,actual.c_str(),errors); return 7; }
            ++paths;
        }
    }
    if(wd_m_live_count()) return 3;
    std::printf("%u binary64 format/parse oracle cases passed\n",checked);
    std::printf("%u JSON parser/serializer oracle cases passed\n",json_checked);
    std::printf("%u lexical path oracle cases passed\n",paths);
}

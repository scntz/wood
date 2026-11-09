/* OS entropy only. The driver's 16-byte request retains its blocking policy;
   public requests fail closed if Linux entropy is not initialized. No cache. */
#ifdef _WIN32
WD_DLL void *LoadLibraryExW(const unsigned short*,void*,unsigned);
WD_DLL void *GetProcAddress(void*,const char*);
WD_DLL int FreeLibrary(void*);
#else
static WInt d_sys(WInt,WInt,WInt,WInt,WInt,WInt,WInt);
#endif
static int r_random(void *ctx,unsigned char *out,WInt size,int public_request,const char *code) {
    int ok=1;
#ifdef _WIN32
    const unsigned short name[]={'b','c','r','y','p','t','.','d','l','l',0};
    void *library=LoadLibraryExW(name,0,0x800); /* SYSTEM32, never application cwd. */
    if(!library) { fail(ctx,code,"cannot load system random provider"); return 0; }
    typedef int32_t (*Generate)(void*,unsigned char*,unsigned,unsigned);
    Generate generate=(Generate)GetProcAddress(library,"BCryptGenRandom");
    if(!generate) ok=0;
#endif
    WInt at=0;
    while(ok && at<size && !wd_rt_unwinding(ctx)) {
        if(public_request && wd_m_task_cancelled()) { fail(ctx,"R4022","task cancelled"); ok=0; break; }
        WInt count=size-at; if(count>256) count=256;
#ifdef _WIN32
        if(generate(0,out+at,(unsigned)count,2)!=0) { ok=0; break; }
        at+=count;
#else
        WInt n=d_sys(318,(WInt)(intptr_t)(out+at),count,public_request?1:0,0,0,0);
        if(n==-4) continue; /* EINTR; recheck cancellation before retrying. */
        if(n<=0 || n>count) { ok=0; break; }
        at+=n;
#endif
    }
#ifdef _WIN32
    FreeLibrary(library);
#endif
    if(!ok && !wd_rt_unwinding(ctx)) fail(ctx,code,"cannot obtain operating-system randomness");
    return ok && !wd_rt_unwinding(ctx);
}
void wd_m_random_bytes(void *ctx,WStr *out,WInt size) {
    *out=(WStr){0};
    if(wd_rt_unwinding(ctx)) return;
    if(size<0 || size>1048576) { fail(ctx,"R4003","random byte count must be in 0..1048576"); return; }
    if(size) wd_m_task_checkpoint(ctx);
    if(wd_rt_unwinding(ctx)) return;
    wd_m_str_new(ctx,out,size);
    if(size && out->owner) r_random(ctx,out->data,size,1,"R4008");
}

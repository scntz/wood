/* Optional system C libraries. Ordinary native programs remain freestanding.
   On glibc targets the driver selects the explicit hosted entry/loader only
   when a used service requires it. Never try to dlopen from a raw static image. */
#ifndef _WIN32
extern void *dlopen(const char *,int) __attribute__((weak));
extern void *dlsym(void *,const char *) __attribute__((weak));
extern int dlclose(void *) __attribute__((weak));
extern int pthread_create(WSize *,const void *,void *(*)(void *),void *) __attribute__((weak));
extern int pthread_join(WSize,void **) __attribute__((weak));
#endif
static void *optional_open(void *ctx,const char *variable,const char *name,const char *alternate,const char *code) {
    WStr directory={0},env=d_literal(variable),file={0},base=d_literal(name);
    d_environment(ctx,&directory,&env);
    if(wd_rt_unwinding(ctx)) return 0;
    if(directory.size && !wd_m_path_is_absolute(ctx,&directory)) {
        wd_m_str_drop(&directory); fail(ctx,code,"native library directory must be absolute and trusted"); return 0;
    }
    void *library=0;
    for(int attempt=0;attempt<2 && !library && !wd_rt_unwinding(ctx);++attempt) {
        if(attempt) { if(!alternate) break; base=d_literal(alternate); }
        if(directory.size) wd_m_path_join(ctx,&file,&directory,&base);
        else str_copy(&file,&base);
        if(!wd_rt_unwinding(ctx)) {
#ifdef _WIN32
        WOwner *owner=0; unsigned short *wide=p_wide(ctx,&file,&owner);
        if(wide) library=LoadLibraryExW(wide,0,directory.size?0x900:0x800);
        wd_m_release(owner);
#else
        WStr terminated={0}; p_copy(ctx,&terminated,file.data,file.size);
        if(terminated.owner && dlopen && dlsym && dlclose && pthread_create && pthread_join)
            library=dlopen((const char*)terminated.data,2); /* NOW | LOCAL */
        wd_m_str_drop(&terminated);
#endif
        }
        wd_m_str_drop(&file); file=(WStr){0};
    }
    wd_m_str_drop(&directory);
    if(!library && !wd_rt_unwinding(ctx)) fail(ctx,code,"required native library is unavailable");
    return library;
}
static void *optional_symbol(void *library,const char *name) {
#ifdef _WIN32
    return GetProcAddress(library,name);
#else
    return dlsym?dlsym(library,name):0;
#endif
}
static void optional_close(void *library) {
#ifdef _WIN32
    if(library) FreeLibrary(library);
#else
    if(library && dlclose) dlclose(library);
#endif
}

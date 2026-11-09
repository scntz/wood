/* Read-only environment access. Owned output; no borrowed process-memory escape.
   No mutation API: concurrent foreign mutation of the environment is unsupported. */
#ifdef _WIN32
WD_DLL void SetLastError(unsigned);
#endif
WInt wd_m_env_get(void *ctx,WStr *out,const WStr *name) {
    *out=(WStr){0};
    if(wd_rt_unwinding(ctx)) return 0;
    if(!name->size) { fail(ctx,"R4003","environment variable name must not be empty"); return 0; }
    for(WInt i=0;i<name->size;++i) if(!name->data[i] || name->data[i]=='=') {
        fail(ctx,"R4003","environment variable name must not contain NUL or '='"); return 0;
    }
#ifdef _WIN32
    if(name->size>INT32_MAX || !j_utf8(name)) { fail(ctx,"R4003","environment variable name must be valid UTF-8 on Windows"); return 0; }
    WOwner *key=0; unsigned short *wide=p_wide(ctx,name,&key); if(!wide) return 0;
    SetLastError(0);
    unsigned count=GetEnvironmentVariableW(wide,0,0),error=GetLastError();
    if(!count) {
        wd_m_release(key);
        if(error==203) return 0;
        if(error) { fail(ctx,"R4008","cannot read environment variable"); return 0; }
        return 1; /* Present empty value, distinct from an absent variable. */
    }
    WOwner *buffer=allocate(ctx,sizeof(WOwner)+(WInt)count*2,no_children);
    if(!buffer) { wd_m_release(key); return 0; }
    SetLastError(0);
    unsigned n=GetEnvironmentVariableW(wide,(unsigned short*)(buffer+1),count);
    error=GetLastError(); wd_m_release(key);
    if(n>=count || (!n && error)) fail(ctx,"R4008","environment changed or failed while reading");
    else {
        int bytes=n?WideCharToMultiByte(65001,128,(unsigned short*)(buffer+1),(int)n,0,0,0,0):0;
        if(n && !bytes) fail(ctx,"R4008","environment variable value is not valid Unicode");
        else {
            wd_m_str_new(ctx,out,bytes);
            if(bytes && out->owner && !WideCharToMultiByte(65001,128,(unsigned short*)(buffer+1),(int)n,(char*)out->data,bytes,0,0))
                fail(ctx,"R4008","cannot encode environment variable value as UTF-8");
        }
    }
    wd_m_release(buffer);
#else
    const unsigned char *value=d_environment_raw(name);
    if(!value) return 0;
    WInt size=0; while(value[size]) ++size;
    p_copy(ctx,out,value,size);
#endif
    return !wd_rt_unwinding(ctx);
}

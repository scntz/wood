/* Private sequential OS bridge for the Wood-written normal driver.
   Policy/frontends/backends remain in Wood. No hosted C/C++ library is used. */
static WStr d_response;
static WSeq *d_owned;
#ifdef _WIN32
/* Additive ABI 4: old objects still link. New LLVM cores supply strong context
   helpers; a private bridge request against an old core fails explicitly. */
WInt wd_native_context_missing_size(void) { return 0; }
void wd_native_context_missing_error(void *ctx,WStr *a,WStr *b) { (void)ctx; (void)a; (void)b; }
void wd_native_context_missing_clear(void *ctx) { (void)ctx; }
#pragma comment(linker,"/alternatename:wd_rt_context_size=wd_native_context_missing_size")
#pragma comment(linker,"/alternatename:wd_rt_context_error=wd_native_context_missing_error")
#pragma comment(linker,"/alternatename:wd_rt_context_clear=wd_native_context_missing_clear")
#endif
static WStr d_literal(const char *text) { WInt n=0; while(text[n]) ++n; return (WStr){(unsigned char*)text,n,0}; }
static int d_equal(const WStr *a,const char *b) { WStr text=d_literal(b); return !j_key(a,&text); }
#ifndef _WIN32
static WInt d_sys(WInt,WInt,WInt,WInt,WInt,WInt,WInt);
#else
WD_DLL unsigned GetEnvironmentVariableW(const unsigned short*,unsigned short*,unsigned);
WD_DLL unsigned GetModuleFileNameW(void*,unsigned short*,unsigned);
WD_DLL unsigned GetTempPathW(unsigned,unsigned short*);
WD_DLL void *LoadLibraryExW(const unsigned short*,void*,unsigned);
WD_DLL void *GetProcAddress(void*,const char*);
WD_DLL int FreeLibrary(void*);
WD_DLL int MoveFileExW(const unsigned short*,const unsigned short*,unsigned);
#endif
#ifndef _WIN32
/* Child-only additions; placed first so they take precedence over inherited values. */
static WSeq *d_env_extra;
#endif
#ifndef _WIN32
static const unsigned char *d_environment_raw(const WStr *name) {
    if(!p_argv) return 0;
    const unsigned char *const *env=p_argv+p_argc+1;
    for(WInt i=0;env[i];++i) {
        WInt n=0; while(env[i][n] && env[i][n]!='=') ++n;
        WStr candidate={(unsigned char*)env[i],n,0};
        if(!j_key(&candidate,name) && env[i][n]=='=') return env[i]+n+1;
    }
    return 0;
}
#endif
static void d_environment(void *ctx,WStr *out,const WStr *name) {
    *out=(WStr){0}; if(!p_valid(ctx,name)) return;
#ifdef _WIN32
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,name,&owner); if(!wide) return;
    unsigned count=GetEnvironmentVariableW(wide,0,0);
    if(count) {
        WOwner *buffer=allocate(ctx,sizeof(WOwner)+(WInt)count*2,no_children);
        if(buffer) {
            unsigned n=GetEnvironmentVariableW(wide,(unsigned short*)(buffer+1),count);
            if(n>=count) fail(ctx,"D5001","environment changed while reading");
            else p_utf8(ctx,out,(unsigned short*)(buffer+1),n);
            wd_m_release(buffer);
        }
    }
    wd_m_release(owner);
#else
    const unsigned char *value=d_environment_raw(name);
    if(value) { WInt size=0; while(value[size]) ++size; p_copy(ctx,out,value,size); }
#endif
}
static void d_environment_set(void *ctx,const WStr *name,const WStr *value) {
    if(!p_valid(ctx,name) || !p_valid(ctx,value)) return;
    if(!name->size) { fail(ctx,"D5001","invalid environment name"); return; }
    for(WInt i=0;i<name->size;++i) if(name->data[i]=='=' || !name->data[i]) { fail(ctx,"D5001","invalid environment name"); return; }
#ifdef _WIN32
    WOwner *a=0,*b=0; unsigned short *left=p_wide(ctx,name,&a),*right=p_wide(ctx,value,&b);
    /* Resolved at use so applications keep the qualified minimal import library. */
    const unsigned short module[]={'k','e','r','n','e','l','3','2','.','d','l','l',0};
    void *library=LoadLibraryExW(module,0,0x800);
    int (*setter)(const unsigned short*,const unsigned short*)=library?(int(*)(const unsigned short*,const unsigned short*))GetProcAddress(library,"SetEnvironmentVariableW"):0;
    if(left && right && (!setter || !setter(left,right))) fail(ctx,"D5001","cannot set child environment");
    wd_m_release(a); wd_m_release(b);
#else
    JBuffer text={ctx,0}; WStr entry={0}; jb_add(&text,name->data,name->size); jb_byte(&text,'='); jb_add(&text,value->data,value->size); jb_finish(&text,&entry);
    if(!d_env_extra && !wd_rt_unwinding(ctx)) d_env_extra=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop);
    if(d_env_extra && !wd_rt_unwinding(ctx)) wd_m_seq_append(ctx,&d_env_extra,&entry);
    wd_m_str_drop(&entry);
#endif
}
#include "driver_process.inc"
static void d_executable(void *ctx,WStr *out) {
    *out=(WStr){0};
#ifdef _WIN32
    WOwner *buffer=allocate(ctx,sizeof(WOwner)+65536,no_children); if(!buffer) return;
    unsigned n=GetModuleFileNameW(0,(unsigned short*)(buffer+1),32768);
    if(!n || n>=32768) fail(ctx,"D5001","cannot locate native compiler executable");
    else p_utf8(ctx,out,(unsigned short*)(buffer+1),n);
    wd_m_release(buffer);
#else
    for(WInt size=256;size<=1048576;size*=2) {
        wd_m_str_new(ctx,out,size); if(!out->owner) return;
        WInt n=d_sys(89,(WInt)(intptr_t)"/proc/self/exe",(WInt)(intptr_t)out->data,size,0,0,0);
        if(n>=0 && n<size) { out->size=n; out->data[n]=0; return; }
        wd_m_str_drop(out); *out=(WStr){0}; if(n<0) break;
    }
    fail(ctx,"D5001","cannot locate native compiler executable through /proc/self/exe");
#endif
}
static int d_random(void *ctx,unsigned char bytes[16]) {
    return r_random(ctx,bytes,16,0,"D5001");
}
static void d_temp(void *ctx,WStr *out,const WStr *parent) {
    *out=(WStr){0}; WStr base={0},absolute={0};
    if(parent->size) str_copy(&base,parent);
    else {
#ifdef _WIN32
        WOwner *buffer=allocate(ctx,sizeof(WOwner)+65536,no_children);
        if(buffer) { unsigned n=GetTempPathW(32768,(unsigned short*)(buffer+1)); if(n && n<32768) p_utf8(ctx,&base,(unsigned short*)(buffer+1),n); else fail(ctx,"D5001","cannot locate temporary directory"); wd_m_release(buffer); }
#else
        const char *keys[]={"TMPDIR","TMP","TEMP","TEMPDIR"};
        for(unsigned i=0;i<4 && !base.size;++i) { WStr key=d_literal(keys[i]); wd_m_str_drop(&base); d_environment(ctx,&base,&key); }
        if(!base.size && !wd_rt_unwinding(ctx)) { wd_m_str_drop(&base); base=d_literal("/tmp"); }
#endif
    }
    if(!wd_rt_unwinding(ctx)) wd_m_path_absolute(ctx,&absolute,&base);
    wd_m_str_drop(&base); base=(WStr){0};
    if(!wd_rt_unwinding(ctx)) wd_m_path_normalize(ctx,&base,&absolute); wd_m_str_drop(&absolute);
    if(!wd_rt_unwinding(ctx) && p_kind(ctx,&base,1)!=2) fail(ctx,"D5001","temporary parent must be a directory");
    for(unsigned attempt=0;attempt<128 && !wd_rt_unwinding(ctx);++attempt) {
        unsigned char random[16],name[37]={'w','o','o','d','-'}; const char *hex="0123456789abcdef";
        if(!d_random(ctx,random)) break;
        for(unsigned i=0;i<16;++i) { name[5+i*2]=(unsigned char)hex[random[i]>>4]; name[6+i*2]=(unsigned char)hex[random[i]&15]; }
        WStr leaf={name,37,0},candidate={0}; wd_m_path_join(ctx,&candidate,&base,&leaf);
        int made=0,collision=0;
#ifdef _WIN32
        WOwner *candidate_owner=0; unsigned short *candidate_wide=0;
#endif
        if(!wd_rt_unwinding(ctx)) {
#ifdef _WIN32
            candidate_wide=p_wide(ctx,&candidate,&candidate_owner);
            if(candidate_wide) { made=CreateDirectoryW(candidate_wide,0); collision=!made && GetLastError()==183; }
#else
            WInt result=d_sys(83,(WInt)(intptr_t)candidate.data,0700,0,0,0,0); made=result==0; collision=result==-17;
#endif
        }
        if(made) {
            if(!d_owned) d_owned=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop);
            if(d_owned) wd_m_seq_append(ctx,&d_owned,&candidate);
            if(!wd_rt_unwinding(ctx)) { *out=candidate; candidate=(WStr){0}; }
            else { /* Never leave an untracked created directory on OOM. */
#ifdef _WIN32
                RemoveDirectoryW(candidate_wide);
#else
                d_sys(84,(WInt)(intptr_t)candidate.data,0,0,0,0,0);
#endif
            }
#ifdef _WIN32
            wd_m_release(candidate_owner);
#endif
            wd_m_str_drop(&candidate); break;
        }
#ifdef _WIN32
        wd_m_release(candidate_owner);
#endif
        wd_m_str_drop(&candidate);
        if(!collision && !wd_rt_unwinding(ctx)) fail(ctx,"D5001","cannot create temporary directory");
    }
    wd_m_str_drop(&base);
    if(!out->owner && !wd_rt_unwinding(ctx)) fail(ctx,"D5001","temporary directory collision limit exceeded");
}
#ifndef _WIN32
static void d_clear_fd(WInt fd,unsigned depth) {
    if(depth>256) return;
    unsigned char bytes[4096];
    for(;;) {
        WInt n=d_sys(217,fd,(WInt)(intptr_t)bytes,sizeof bytes,0,0,0); if(n==-4) continue; if(n<=0) break;
        for(WInt pos=0;pos+20<=n;) {
            unsigned length=(unsigned)bytes[pos+16]|((unsigned)bytes[pos+17]<<8);
            if(length<20 || length>(unsigned)(n-pos)) return;
            unsigned char *name=bytes+pos+19; WInt size=0;
            while(size<(WInt)length-19 && name[size]) ++size;
            if(size==(WInt)length-19) return;
            if(!(size==1 && name[0]=='.') && !(size==2 && name[0]=='.' && name[1]=='.')) {
                WInt child=d_sys(257,fd,(WInt)(intptr_t)name,0xb0000,0,0,0);
                if(child>=0) { d_clear_fd(child,depth+1); d_sys(3,child,0,0,0,0,0); d_sys(263,fd,(WInt)(intptr_t)name,512,0,0,0); }
                else d_sys(263,fd,(WInt)(intptr_t)name,0,0,0,0);
            }
            pos+=length;
        }
    }
}
static void d_remove_tree(void *ctx,const WStr *path,unsigned depth) {
    (void)ctx; (void)depth;
    WInt fd=d_sys(2,(WInt)(intptr_t)path->data,0xb0000,0,0,0,0);
    if(fd>=0) { d_clear_fd(fd,0); d_sys(3,fd,0,0,0,0,0); d_sys(84,(WInt)(intptr_t)path->data,0,0,0,0,0); }
    else d_sys(87,(WInt)(intptr_t)path->data,0,0,0,0,0);
}
#else
static void d_remove_tree(void *ctx,const WStr *path,unsigned depth) {
    if(depth>256 || wd_rt_unwinding(ctx)) return;
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,path,&owner); if(!wide) return;
    /* Deny deletion while enumerating; inspect the opened reparse point itself. */
    void *dir=CreateFileW(wide,0,3,0,3,0x02200000,0); unsigned info[13]={0};
    if(dir!=(void*)(intptr_t)-1 && GetFileInformationByHandle(dir,info) && (info[0]&16) && !(info[0]&1024)) {
        WStr star=d_literal("*"),pattern={0}; wd_m_path_join(ctx,&pattern,path,&star);
        WOwner *search_owner=0; unsigned short *search=p_wide(ctx,&pattern,&search_owner); PFind entry;
        void *find=search?FindFirstFileW(search,&entry):(void*)(intptr_t)-1;
        if(find!=(void*)(intptr_t)-1) {
            do {
                WInt size=0; while(size<260 && entry.name[size]) ++size;
                if((size==1 && entry.name[0]=='.') || (size==2 && entry.name[0]=='.' && entry.name[1]=='.')) continue;
                WStr name={0},child={0}; p_utf8(ctx,&name,entry.name,size); wd_m_path_join(ctx,&child,path,&name);
                if(!wd_rt_unwinding(ctx)) d_remove_tree(ctx,&child,depth+1);
                wd_m_str_drop(&name); wd_m_str_drop(&child);
            } while(!wd_rt_unwinding(ctx) && FindNextFileW(find,&entry));
            FindClose(find);
        }
        wd_m_release(search_owner); wd_m_str_drop(&pattern);
    }
    if(dir!=(void*)(intptr_t)-1) CloseHandle(dir);
    unsigned attrs=GetFileAttributesW(wide);
    if(attrs!=UINT32_MAX) { if(attrs&16) RemoveDirectoryW(wide); else DeleteFileW(wide); }
    wd_m_release(owner);
}
#endif
static WInt d_owned_index(const WStr *path) {
    for(WInt i=0;i<wd_m_seq_len(d_owned);++i) if(!j_key(path,wd_m_seq_read(d_owned,i))) return i;
    return -1;
}
static void d_forget(WInt index) {
    WStr *items=(WStr*)data(d_owned); wd_m_str_drop(items+index);
    for(WInt i=index+1;i<d_owned->length;++i) items[i-1]=items[i];
    --d_owned->length; items[d_owned->length]=(WStr){0};
    if(!d_owned->length) { wd_m_release(d_owned); d_owned=0; }
}
static void d_release(void *ctx,const WStr *path) {
    WInt index=d_owned_index(path);
    if(index<0) { fail(ctx,"D5001","cannot release an unowned temporary directory"); return; }
    d_remove_tree(ctx,path,0); d_forget(index);
}
static void d_cleanup(void *ctx) {
    while(wd_m_seq_len(d_owned)) { WInt index=d_owned->length-1; d_remove_tree(ctx,wd_m_seq_read(d_owned,index),0); d_forget(index); }
    /* A failed first append can leave an allocated but empty ownership list. */
    if(d_owned) { wd_m_release(d_owned); d_owned=0; }
#ifndef _WIN32
    if(d_env_extra) { wd_m_release(d_env_extra); d_env_extra=0; }
#endif
}
static void d_move(void *ctx,const WStr *from,const WStr *to,int replace) {
    if(wd_rt_unwinding(ctx)) return;
#ifdef _WIN32
    WOwner *a=0,*b=0; unsigned short *left=p_wide(ctx,from,&a),*right=p_wide(ctx,to,&b);
    /* A virus scanner can briefly hold a new executable, or the old one may still be closing. */
    int moved=0;
    if(left && right) for(int attempt=0;attempt<50 && !moved;++attempt) {
        moved=MoveFileExW(left,right,8|(replace?1:0));
        if(moved) break;
        unsigned error=GetLastError();
        if(error!=5 && error!=32) break;
        Sleep(100);
    }
    if(left && right && !moved) fail(ctx,"D5001","cannot publish or rename output; check that the program is not still running");
    wd_m_release(a); wd_m_release(b);
#else
    WInt n=replace?d_sys(82,(WInt)(intptr_t)from->data,(WInt)(intptr_t)to->data,0,0,0,0):
        d_sys(316,-100,(WInt)(intptr_t)from->data,-100,(WInt)(intptr_t)to->data,1,0);
    if(n<0) fail(ctx,"D5001","cannot publish or rename output");
#endif
}
static int d_same(void *ctx,const WStr *a,const WStr *b) {
    WStr aa={0},ab={0},na={0},nb={0}; wd_m_path_absolute(ctx,&aa,a); wd_m_path_absolute(ctx,&ab,b);
    if(!wd_rt_unwinding(ctx)) { wd_m_path_normalize(ctx,&na,&aa); wd_m_path_normalize(ctx,&nb,&ab); }
    int same=!wd_rt_unwinding(ctx) && !j_key(&na,&nb);
    if(!same && !wd_rt_unwinding(ctx)) {
#ifdef _WIN32
        WOwner *oa=0,*ob=0; unsigned short *wa=p_wide(ctx,&na,&oa),*wb=p_wide(ctx,&nb,&ob);
        void *ha=wa?CreateFileW(wa,0,7,0,3,0x02000000,0):(void*)(intptr_t)-1;
        void *hb=wb?CreateFileW(wb,0,7,0,3,0x02000000,0):(void*)(intptr_t)-1;
        unsigned ia[13]={0},ib[13]={0};
        if(ha!=(void*)(intptr_t)-1 && hb!=(void*)(intptr_t)-1 && GetFileInformationByHandle(ha,ia) && GetFileInformationByHandle(hb,ib)) same=ia[7]==ib[7] && ia[11]==ib[11] && ia[12]==ib[12];
        if(ha!=(void*)(intptr_t)-1) CloseHandle(ha); if(hb!=(void*)(intptr_t)-1) CloseHandle(hb); wd_m_release(oa); wd_m_release(ob);
#else
        uint64_t ia[18],ib[18];
        if(d_sys(4,(WInt)(intptr_t)na.data,(WInt)(intptr_t)ia,0,0,0,0)>=0 && d_sys(4,(WInt)(intptr_t)nb.data,(WInt)(intptr_t)ib,0,0,0,0)>=0) same=ia[0]==ib[0] && ia[1]==ib[1];
#endif
    }
    wd_m_str_drop(&aa); wd_m_str_drop(&ab); wd_m_str_drop(&na); wd_m_str_drop(&nb); return same;
}
static void d_copy_new(void *ctx,const WStr *from,const WStr *to,unsigned depth) {
    if(wd_rt_unwinding(ctx)) return;
    if(depth>256) { fail(ctx,"D5001","copy nesting limit exceeded"); return; }
    WInt kind=p_kind(ctx,from,0);
    if(p_kind(ctx,to,0)!=0) { fail(ctx,"D5001","copy target already exists"); return; }
    if(kind==2) {
        if(p_mkdir(ctx,to)!=1) { if(!wd_rt_unwinding(ctx)) fail(ctx,"D5001","copy target already exists"); return; }
        WSeq *names=wd_m_fs_list(ctx,from,65536);
        for(WInt i=0;i<wd_m_seq_len(names) && !wd_rt_unwinding(ctx);++i) {
            WStr a={0},b={0}; const WStr *name=wd_m_seq_read(names,i);
            wd_m_path_join(ctx,&a,from,name); wd_m_path_join(ctx,&b,to,name); d_copy_new(ctx,&a,&b,depth+1); wd_m_str_drop(&a); wd_m_str_drop(&b);
        }
        wd_m_release(names);
    } else if(kind==1) {
        WStr content={0}; wd_m_read_text(ctx,&content,from);
        if(!wd_rt_unwinding(ctx)) {
            WInt fd=wd_file_open(to->data,to->size,3),at=0;
            if(fd<0) fail(ctx,"D5001","cannot create copy target");
            else {
                while(at<content.size && !wd_rt_unwinding(ctx)) { WInt n=wd_file_write(fd,content.data+at,content.size-at); if(n==-4) continue; if(n<=0 || n>content.size-at) fail(ctx,"D5001","cannot write copy target"); else at+=n; }
                if(wd_file_close(fd)<0 && !wd_rt_unwinding(ctx)) fail(ctx,"D5001","cannot close copy target");
            }
        }
        wd_m_str_drop(&content);
    } else fail(ctx,"D5001","copy source must be a regular file or directory");
}
static WJson *d_field(void *ctx,const WJson *request,const char *key) {
    WStr name=d_literal(key);
    if(!request || request->kind!=6 || wd_m_map_find(request->as.object,&name)<0) { fail(ctx,"D5001","invalid native driver request"); return 0; }
    return *(WJson**)wd_m_map_at(ctx,request->as.object,&name);
}
static WStr d_text(void *ctx,const WJson *request,const char *key) {
    WJson *value=d_field(ctx,request,key);
    if(!value || value->kind!=4) { fail(ctx,"D5001","invalid native driver string field"); return d_literal(""); }
    WStr text=value->as.string; p_valid(ctx,&text); return text;
}
static WInt d_integer(void *ctx,const WJson *request,const char *key) {
    WJson *value=d_field(ctx,request,key);
    if(!value || value->kind!=2) { fail(ctx,"D5001","invalid native driver integer field"); return 0; }
    return value->as.integer;
}
static void d_dispatch(void *ctx,JBuffer *out,const WJson *request) {
    WStr op=d_text(ctx,request,"op"); if(wd_rt_unwinding(ctx)) return;
    if(d_equal(&op,"assets")) {
        WStr exe={0},bin={0},prefix={0},root={0},suffix=d_literal("share/wood");
        d_executable(ctx,&exe); wd_m_path_parent(ctx,&bin,&exe); wd_m_path_parent(ctx,&prefix,&bin); wd_m_path_join(ctx,&root,&prefix,&suffix);
        jb_word(out,"{\"root\":"); j_quote(out,&root); jb_word(out," ,\"cxx\":\"\",\"native\":true,\"windows\":");
#ifdef _WIN32
        jb_word(out,"true}");
#else
        jb_word(out,"false}");
#endif
        wd_m_str_drop(&exe); wd_m_str_drop(&bin); wd_m_str_drop(&prefix); wd_m_str_drop(&root); return;
    }
    if(d_equal(&op,"environment") || d_equal(&op,"temp")) {
        WStr key=d_text(ctx,request,d_equal(&op,"temp")?"parent":"name"),value={0};
        if(!wd_rt_unwinding(ctx)) { if(d_equal(&op,"temp")) d_temp(ctx,&value,&key); else d_environment(ctx,&value,&key); }
        j_quote(out,&value); wd_m_str_drop(&value); return;
    }
    if(d_equal(&op,"executable")) {
        WStr value={0}; d_executable(ctx,&value);
        j_quote(out,&value); wd_m_str_drop(&value); return;
    }
    if(d_equal(&op,"environment_set")) {
        WStr name=d_text(ctx,request,"name"),value=d_text(ctx,request,"value");
        if(!wd_rt_unwinding(ctx)) d_environment_set(ctx,&name,&value);
        jb_word(out,"null"); return;
    }
    if(d_equal(&op,"cleanup")) { d_cleanup(ctx); jb_word(out,"null"); return; }
    if(d_equal(&op,"release")) { WStr path=d_text(ctx,request,"path"); if(!wd_rt_unwinding(ctx)) d_release(ctx,&path); jb_word(out,"null"); return; }
    if(d_equal(&op,"same")) {
        WStr a=d_text(ctx,request,"left"),b=d_text(ctx,request,"right"); jb_word(out,d_same(ctx,&a,&b)?"true":"false"); return;
    }
    if(d_equal(&op,"publish") || d_equal(&op,"rename") || d_equal(&op,"copy_new")) {
        WStr a=d_text(ctx,request,"from"),b=d_text(ctx,request,"to");
        if(!wd_rt_unwinding(ctx)) {
            if(d_equal(&op,"copy_new")) d_copy_new(ctx,&a,&b,0);
            else {
                WInt index=d_owned_index(&a);
                d_move(ctx,&a,&b,d_equal(&op,"publish"));
                if(d_equal(&op,"rename") && index>=0 && !wd_rt_unwinding(ctx)) d_forget(index);
            }
        }
        jb_word(out,"null"); return;
    }
    if(d_equal(&op,"atomic")) {
        WStr path=d_text(ctx,request,"path"),content={0},parent={0},stage={0},file={0},name=d_literal("output");
        WJson *payload=d_field(ctx,request,"content");
        if(payload && payload->kind==4) content=payload->as.string;
        else fail(ctx,"D5001","invalid atomic content field");
        if(!wd_rt_unwinding(ctx)) { wd_m_path_parent(ctx,&parent,&path); d_temp(ctx,&stage,&parent); }
        if(!wd_rt_unwinding(ctx)) { wd_m_path_join(ctx,&file,&stage,&name); wd_m_write_text(ctx,&file,&content); d_move(ctx,&file,&path,1); }
        if(stage.owner) d_release(ctx,&stage);
        wd_m_str_drop(&parent); wd_m_str_drop(&stage); wd_m_str_drop(&file); jb_word(out,"null"); return;
    }
    if(d_equal(&op,"process_stdin_policy")) { jb_word(out,"true"); return; }
    if(d_equal(&op,"process")) {
        WJson *input=d_field(ctx,request,"args"); WStr log=d_text(ctx,request,"log");
        WInt timeout=d_integer(ctx,request,"timeout"),limit=d_integer(ctx,request,"limit");
        WSeq *args=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop);
        if(!input || input->kind!=5) fail(ctx,"D5001","invalid process argument list");
        if(!wd_rt_unwinding(ctx)) for(WInt i=0;i<wd_m_seq_len(input->as.array) && !wd_rt_unwinding(ctx);++i) {
            WJson *item=*(WJson**)wd_m_seq_read(input->as.array,i);
            if(!item || item->kind!=4 || !p_valid(ctx,&item->as.string)) { fail(ctx,"D5001","invalid process argument"); break; }
            wd_m_seq_append(ctx,&args,&item->as.string);
        }
        WStr input_key=d_literal("inherit_stdin"); int inherit=1;
        if(wd_m_map_find(request->as.object,&input_key)>=0) {
            WJson *value=d_field(ctx,request,"inherit_stdin");
            if(value && value->kind==1) inherit=value->as.integer!=0;
            else fail(ctx,"D5001","invalid process stdin policy");
        }
        WInt status=d_process(ctx,args,&log,timeout,limit,inherit); WStr number={0};
        if(!wd_rt_unwinding(ctx)) wd_m_int_str(ctx,&number,status);
        jb_add(out,number.data,number.size); wd_m_str_drop(&number); wd_m_release(args); return;
    }
    fail(ctx,"D5001","unknown native driver OS operation");
}
/* Byte-exact stdio for protocols framed by length. Main thread only, like the bridge. */
#ifdef _WIN32
WD_DLL int ReadFile(void*,void*,unsigned,unsigned*,void*);
WD_DLL int WriteFile(void*,const void*,unsigned,unsigned*,void*);
WD_DLL void Sleep(unsigned);
#endif
WInt wood_driver_stdin_read(WInt *out,WInt size) {
    if(wd_m_worker_thread() || size<0 || (!out && size)) return -1;
    if(!size) return 0;
    unsigned char buffer[65536]; WInt wanted=size<(WInt)sizeof buffer?size:(WInt)sizeof buffer,got=0;
#ifdef _WIN32
    unsigned n=0;
    /* A closed pipe is end of input, like read returning zero on Linux. */
    if(ReadFile(GetStdHandle((unsigned)-10),buffer,(unsigned)wanted,&n,0)) got=n;
#else
    do { got=d_sys(0,0,(WInt)(intptr_t)buffer,wanted,0,0,0); } while(got==-4);
    if(got<0) return -1;
#endif
    for(WInt i=0;i<got;++i) out[i]=buffer[i];
    return got;
}
/* 1 when a read would not block (data or end of input), 0 when the wait expired. */
WInt wood_driver_stdin_ready(WInt timeout_ms) {
    if(wd_m_worker_thread() || timeout_ms<0) return -1;
#ifdef _WIN32
    static int (*peek)(void*,void*,unsigned,unsigned*,unsigned*,unsigned*);
    if(!peek) {
        /* Resolved at use so applications keep the qualified minimal import library. */
        const unsigned short module[]={'k','e','r','n','e','l','3','2','.','d','l','l',0};
        void *library=LoadLibraryExW(module,0,0x800);
        peek=library?(int(*)(void*,void*,unsigned,unsigned*,unsigned*,unsigned*))GetProcAddress(library,"PeekNamedPipe"):0;
        if(!peek) return 1;
    }
    void *input=GetStdHandle((unsigned)-10);
    for(WInt waited=0;;waited+=10) {
        unsigned available=0;
        /* Files and consoles are not pipes; a read is then the only way to find out. */
        if(!peek(input,0,0,0,&available,0) || available) return 1;
        if(waited>=timeout_ms) return 0;
        Sleep(10);
    }
#else
    struct { int fd; short events; short revents; } poll={0,1,0};
    WInt ready;
    do { ready=d_sys(7,(WInt)(intptr_t)&poll,1,timeout_ms,0,0,0); } while(ready==-4);
    return ready==0?0:1;
#endif
}
WInt wood_driver_stdout_write(const char *bytes,WInt size) {
    if(wd_m_worker_thread() || size<0 || (!bytes && size)) return -1;
    WInt done=0;
    while(done<size) {
#ifdef _WIN32
        unsigned n=0; WInt part=size-done>1048576?1048576:size-done;
        if(!WriteFile(GetStdHandle((unsigned)-11),bytes+done,(unsigned)part,&n,0)) return -1;
        done+=n;
#else
        WInt n=d_sys(1,1,(WInt)(intptr_t)(bytes+done),size-done,0,0,0);
        if(n==-4) continue;
        if(n<=0) return -1;
        done+=n;
#endif
    }
    return done;
}
WInt wood_driver_call(const char *bytes,WInt size) {
    if(wd_m_worker_thread()) return -1; /* Private transport is main-thread only. */
    wd_m_str_drop(&d_response); d_response=(WStr){0};
#ifndef _WIN32
    if(!wd_rt_context_size || !wd_rt_context_error || !wd_rt_context_clear) {
        d_response=d_literal("{\"ok\":false,\"code\":\"D5001\",\"message\":\"native driver requires the current ABI-4 LLVM core\"}"); return d_response.size;
    }
#endif
    WInt context_size=wd_rt_context_size();
    void *ctx=context_size>0?wd_platform_alloc((WSize)context_size):0;
    if(!ctx) { d_response=d_literal("{\"ok\":false,\"code\":\"D5001\",\"message\":\"native driver context unavailable\"}"); return d_response.size; }
    zero_bytes(ctx,(WSize)context_size);
    WJson *request=0; WStr text={(unsigned char*)bytes,size,0},value={0}; JBuffer body={ctx,0};
    if(size<0 || size>67108864 || (!bytes && size)) fail(ctx,"D5001","invalid native driver request size");
    else { JReader reader={ctx,&text,0}; request=jr_value(&reader,0); if(!wd_rt_unwinding(ctx)) { jr_space(&reader); if(reader.pos!=size) jr_bad(&reader,"trailing content"); } }
    if(!wd_rt_unwinding(ctx)) d_dispatch(ctx,&body,request);
    jb_finish(&body,&value); wd_m_release(request);
    JBuffer reply={ctx,0};
    if(wd_rt_unwinding(ctx)) {
        WStr code={0},message={0}; wd_rt_context_error(ctx,&code,&message); wd_rt_context_clear(ctx);
        if(code.size!=5 || code.data[0]!='D') { wd_m_str_drop(&code); code=d_literal("D5001"); }
        jb_word(&reply,"{\"ok\":false,\"code\":"); j_quote(&reply,&code); jb_word(&reply,",\"message\":"); j_quote(&reply,&message); jb_byte(&reply,'}');
        wd_m_str_drop(&code); wd_m_str_drop(&message);
    } else { jb_word(&reply,"{\"ok\":true,\"value\":"); jb_add(&reply,value.data,value.size); jb_byte(&reply,'}'); }
    jb_finish(&reply,&d_response); wd_m_str_drop(&value);
    wd_rt_context_clear(ctx); wd_platform_free(ctx,(WSize)context_size);
    if(!d_response.owner || d_response.size>65536) { wd_m_str_drop(&d_response); d_response=d_literal("{\"ok\":false,\"code\":\"D5001\",\"message\":\"native driver response allocation or size limit\"}"); }
    return d_response.size;
}
WInt wood_driver_response(WInt *out,WInt size) {
    if(wd_m_worker_thread()) return -1;
    if(size!=d_response.size || size<0 || (!out && size)) return -1;
    for(WInt i=0;i<size;++i) out[i]=d_response.data[i]; wd_m_str_drop(&d_response); d_response=(WStr){0}; return size;
}

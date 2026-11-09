/* Hosted-OS services without a hosted runtime. Linux x86-64 uses the kernel
   ABI; Windows x64 uses only Kernel32. Paths are explicit-length UTF-8. */
static int p_valid(void *ctx,const WStr *p) {
    for(WInt i=0;i<p->size;++i) if(!p->data[i]) { fail(ctx,"R4008","path contains a NUL byte"); return 0; }
    return 1;
}
static int p_sep(unsigned c) {
#ifdef _WIN32
    return c=='/' || c=='\\';
#else
    return c=='/';
#endif
}
#ifdef _WIN32
static const unsigned char p_preferred='\\';
#else
static const unsigned char p_preferred='/';
#endif
static WInt p_root_name(const WStr *p) {
#ifdef _WIN32
    if(p->size>=2 && p->data[1]==':' && ((p->data[0]>='A' && p->data[0]<='Z') || (p->data[0]>='a' && p->data[0]<='z'))) return 2;
    if(p->size>=3 && p_sep(p->data[0]) && p_sep(p->data[1]) && !p_sep(p->data[2])) {
        WInt i=2; while(i<p->size && !p_sep(p->data[i])) ++i; return i;
    }
#else
    (void)p;
#endif
    return 0;
}
static WInt p_root(const WStr *p) { WInt n=p_root_name(p); return n<p->size && p_sep(p->data[n])?n+1:n; }
WInt wd_m_path_is_absolute(void *ctx,const WStr *p) {
    if(!p_valid(ctx,p)) return 0;
#ifdef _WIN32
    return p_root_name(p)>0 && p_root(p)>p_root_name(p);
#else
    return p_root(p)>0;
#endif
}
static void p_copy(void *ctx,WStr *out,const unsigned char *p,WInt n) { wd_m_str_new(ctx,out,n); if(out->owner) copy_bytes(out->data,p,(WSize)n); }
static void p_error(void *ctx,const char *word,const WStr *p) {
    if(wd_rt_unwinding(ctx)) return;
    JBuffer b={ctx,0}; WStr message={0}; jb_word(&b,word); jb_add(&b,p->data,p->size); jb_finish(&b,&message);
    if(message.owner) { wd_rt_error_value(ctx,"R4008",5,&message); wd_m_str_drop(&message); }
}
void wd_m_path_join(void *ctx,WStr *out,const WStr *a,const WStr *b) {
    *out=(WStr){0}; if(!p_valid(ctx,a)||!p_valid(ctx,b)) return;
    WInt ar=p_root_name(a),br=p_root_name(b),start=br;
    if(wd_m_path_is_absolute(ctx,b) || (br && (ar!=br || j_key(&(WStr){a->data,ar,0},&(WStr){b->data,br,0})))) { str_copy(out,b); return; }
    WInt base=a->size;
    if(p_root(b)>br) { base=ar; start=0; }
    JBuffer result={ctx,0}; jb_add(&result,a->data,base);
    if(base>ar && !p_sep(a->data[base-1])) jb_byte(&result,p_preferred);
    jb_add(&result,b->data+start,b->size-start); jb_finish(&result,out);
}
void wd_m_path_name(void *ctx,WStr *out,const WStr *p) {
    *out=(WStr){0}; if(!p_valid(ctx,p)) return;
    WInt start=p->size,root=p_root(p); while(start>root && !p_sep(p->data[start-1])) --start;
    p_copy(ctx,out,p->data+start,p->size-start);
}
void wd_m_path_parent(void *ctx,WStr *out,const WStr *p) {
    *out=(WStr){0}; if(!p_valid(ctx,p)) return;
    WInt separators=0; while(separators<p->size && p_sep(p->data[separators])) ++separators;
    if(separators==p->size) { str_copy(out,p); return; }
    WInt end=p->size,root=p_root(p); while(end>root && !p_sep(p->data[end-1])) --end;
    while(end>root && p_sep(p->data[end-1])) --end;
    p_copy(ctx,out,p->data,end);
}
void wd_m_path_normalize(void *ctx,WStr *out,const WStr *p) {
    *out=(WStr){0}; if(!p_valid(ctx,p)) return;
#ifndef _WIN32
    WInt separators=0; while(separators<p->size && p_sep(p->data[separators])) ++separators;
    if(separators==p->size) { str_copy(out,p); return; }
#endif
    WInt root=p_root(p),rn=p_root_name(p); JBuffer b={ctx,0};
    for(WInt i=0;i<rn;++i) jb_byte(&b,p_sep(p->data[i])?p_preferred:p->data[i]);
    if(root>rn) jb_byte(&b,p_preferred);
    WSeq *starts=0; int trailing=0;
    for(WInt i=root;i<p->size && !wd_rt_unwinding(ctx);) {
        while(i<p->size && p_sep(p->data[i])) ++i;
        WInt begin=i; while(i<p->size && !p_sep(p->data[i])) ++i; WInt n=i-begin;
        if(!n) { trailing=1; break; }
        int dot=n==1 && p->data[begin]=='.',parent=n==2 && p->data[begin]=='.' && p->data[begin+1]=='.';
        trailing=dot || i<p->size;
        if(dot) continue;
        WInt count=wd_m_seq_len(starts),last=count?*(WInt*)wd_m_seq_read(starts,count-1):0;
        int previous_parent=count && wd_m_seq_len(b.seq)-last==2 && data(b.seq)[last]=='.' && data(b.seq)[last+1]=='.';
        if(parent && count && !previous_parent) { b.seq->length=last; if(last>root) --b.seq->length; --starts->length; trailing=1; continue; }
        if(parent && root>rn) continue;
        WInt length=wd_m_seq_len(b.seq);
        if(length>root) jb_byte(&b,p_preferred);
        length=wd_m_seq_len(b.seq);
        if(!starts) starts=wd_m_seq_new(ctx,sizeof(WInt),0,0,0,0);
        if(!starts) break;
        wd_m_seq_append(ctx,&starts,&length); jb_add(&b,p->data+begin,n);
        if(parent) trailing=0;
    }
    if(!wd_m_seq_len(b.seq) && p->size) jb_byte(&b,'.');
    WInt last=wd_m_seq_len(starts)?*(WInt*)wd_m_seq_read(starts,wd_m_seq_len(starts)-1):0;
    int last_parent=wd_m_seq_len(starts) && wd_m_seq_len(b.seq)-last==2 && data(b.seq)[last]=='.' && data(b.seq)[last+1]=='.';
    if(trailing && !last_parent && wd_m_seq_len(b.seq)>root && wd_m_seq_len(starts)) jb_byte(&b,p_preferred);
    wd_m_release(starts); jb_finish(&b,out);
}
#ifdef _WIN32
#define WD_DLL __declspec(dllimport)
WD_DLL unsigned short *GetCommandLineW(void);
WD_DLL int MultiByteToWideChar(unsigned,int,const char*,int,unsigned short*,int);
WD_DLL int WideCharToMultiByte(unsigned,unsigned,const unsigned short*,int,char*,int,const char*,int*);
WD_DLL unsigned GetFullPathNameW(const unsigned short*,unsigned,unsigned short*,unsigned short**);
WD_DLL unsigned GetFileAttributesW(const unsigned short*);
WD_DLL unsigned GetLastError(void);
WD_DLL void *CreateFileW(const unsigned short*,unsigned,unsigned,void*,unsigned,unsigned,void*);
WD_DLL int CloseHandle(void*);
WD_DLL unsigned GetFinalPathNameByHandleW(void*,unsigned short*,unsigned,unsigned);
WD_DLL void *FindFirstFileW(const unsigned short*,void*);
WD_DLL int FindNextFileW(void*,void*);
WD_DLL int FindClose(void*);
typedef struct { unsigned header[11]; unsigned short name[260],alternate[14]; } PFind;
static unsigned short *p_wide(void *ctx,const WStr *p,WOwner **owner) {
    *owner=0; if(!p_valid(ctx,p)) return 0;
    if(p->size>INT32_MAX) { fail(ctx,"R4008","invalid filesystem path"); return 0; }
    int n=p->size?MultiByteToWideChar(65001,8,(const char*)p->data,(int)p->size,0,0):0;
    if(p->size && !n) { fail(ctx,"R4008","invalid filesystem path"); return 0; }
    *owner=allocate(ctx,(WInt)sizeof(WOwner)+((WInt)n+1)*2,no_children); if(!*owner) return 0;
    unsigned short *wide=(unsigned short*)(*owner+1);
    if(n && !MultiByteToWideChar(65001,8,(const char*)p->data,(int)p->size,wide,n)) { wd_m_release(*owner); *owner=0; fail(ctx,"R4008","invalid filesystem path"); return 0; }
    wide[n]=0; return wide;
}
static void p_utf8(void *ctx,WStr *out,const unsigned short *p,WInt n) {
    *out=(WStr){0}; if(n>INT32_MAX) { fail(ctx,"R4008","cannot encode filesystem path as UTF-8"); return; }
    int count=n?WideCharToMultiByte(65001,128,p,(int)n,0,0,0,0):0;
    if(n && !count) { fail(ctx,"R4008","cannot encode filesystem path as UTF-8"); return; }
    wd_m_str_new(ctx,out,count); if(out->owner && count) WideCharToMultiByte(65001,128,p,(int)n,(char*)out->data,count,0,0);
}
void wd_m_path_absolute(void *ctx,WStr *out,const WStr *p) {
    *out=(WStr){0}; WOwner *owner=0; unsigned short *wide=p_wide(ctx,p,&owner); if(!wide) return;
    unsigned count=GetFullPathNameW(wide,0,0,0);
    WOwner *buffer=count?allocate(ctx,sizeof(WOwner)+(WInt)count*2,no_children):0;
    if(buffer) { unsigned short *result=(unsigned short*)(buffer+1); unsigned n=GetFullPathNameW(wide,count,result,0); if(n && n<count) p_utf8(ctx,out,result,n); else p_error(ctx,"cannot make path absolute: ",p); wd_m_release(buffer); }
    else if(!count) p_error(ctx,"cannot make path absolute: ",p);
    wd_m_release(owner);
}
static WInt p_kind(void *ctx,const WStr *p,int follow) {
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,p,&owner); if(!wide) return -1;
    unsigned attributes=GetFileAttributesW(wide),error=attributes==UINT32_MAX?GetLastError():0;
    if(attributes!=UINT32_MAX && (attributes&1024) && follow) {
        void *handle=CreateFileW(wide,0,7,0,3,0x02000000,0);
        if(handle==(void*)(intptr_t)-1) { error=GetLastError(); attributes=UINT32_MAX; } else { CloseHandle(handle); attributes&=~1024U; }
    }
    wd_m_release(owner);
    if(attributes==UINT32_MAX) { if(error==2 || error==3) return 0; p_error(ctx,"cannot query filesystem path: ",p); return -1; }
    return attributes&1024?3:attributes&16?2:1;
}
#else
#if defined(__wasm__)
static WInt p_sys(WInt n,WInt a,WInt b,WInt c,WInt d) { return wood_syscall(n,a,b,c,d,0,0); }
#else
static WInt p_sys(WInt n,WInt a,WInt b,WInt c,WInt d) {
    register WInt r10 __asm__("r10")=d; WInt out;
    __asm__ volatile("syscall":"=a"(out):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory"); return out;
}
#endif
static int p_terminated(void *ctx,WStr *out,const WStr *p) { *out=(WStr){0}; if(!p_valid(ctx,p)) return 0; p_copy(ctx,out,p->data,p->size); return out->owner!=0; }
void wd_m_path_absolute(void *ctx,WStr *out,const WStr *p) {
    *out=(WStr){0}; if(!p_valid(ctx,p)) return;
    if(!p->size) { p_error(ctx,"cannot make path absolute: ",p); return; }
    if(wd_m_path_is_absolute(ctx,p)) { str_copy(out,p); return; }
    WStr cwd={0};
    for(WInt capacity=256;capacity<=1048576;capacity*=2) {
        wd_m_str_new(ctx,&cwd,capacity); if(!cwd.owner) return;
        WInt n=p_sys(79,(WInt)(intptr_t)cwd.data,capacity,0,0);
        if(n>=0) { cwd.size=n-1; wd_m_path_join(ctx,out,&cwd,p); wd_m_str_drop(&cwd); return; }
        wd_m_str_drop(&cwd); cwd=(WStr){0}; if(n!=-34) break;
    }
    p_error(ctx,"cannot make path absolute: ",p);
}
static WInt p_kind(void *ctx,const WStr *p,int follow) {
    WStr terminated={0}; if(!p_terminated(ctx,&terminated,p)) return -1;
    uint64_t stat[18]; WInt n=p_sys(262,-100,(WInt)(intptr_t)terminated.data,(WInt)(intptr_t)stat,follow?0:256);
    wd_m_str_drop(&terminated);
    if(n<0) { if(n==-2 || n==-20) return 0; p_error(ctx,"cannot query filesystem path: ",p); return -1; }
    unsigned mode=((uint32_t*)stat)[6]&0170000;
    return mode==0120000?3:mode==0040000?2:mode==0100000?1:4;
}
#endif
WInt wd_m_path_exists(void *ctx,const WStr *p) { return p_kind(ctx,p,1)>0; }
#ifdef _WIN32
WD_DLL int CreateDirectoryW(const unsigned short*,void*);
WD_DLL int RemoveDirectoryW(const unsigned short*);
WD_DLL int DeleteFileW(const unsigned short*);
WD_DLL int GetFileSizeEx(void*,WInt*);
WD_DLL unsigned GetFileType(void*);
WD_DLL int GetFileInformationByHandle(void*,void*);
#endif
static WInt p_mkdir(void *ctx,const WStr *p) {
#ifdef _WIN32
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,p,&owner); if(!wide) return -1;
    int created=CreateDirectoryW(wide,0); unsigned error=GetLastError(); wd_m_release(owner);
    if(created) return 1;
    if(error==183 && p_kind(ctx,p,1)==2) return 0;
#else
    WStr terminated={0}; if(!p_terminated(ctx,&terminated,p)) return -1;
    WInt result=p_sys(83,(WInt)(intptr_t)terminated.data,0777,0,0); wd_m_str_drop(&terminated);
    if(!result) return 1;
    if(result==-17 && p_kind(ctx,p,1)==2) return 0;
#endif
    p_error(ctx,"cannot create directories: ",p); return -1;
}
WInt wd_m_fs_mkdirs(void *ctx,const WStr *p) {
    if(!p_valid(ctx,p)) return 0;
    if(!p->size) { p_error(ctx,"cannot create directories: ",p); return 0; }
    WInt root=p_root(p);
    for(WInt i=root+1;i<p->size;++i) if(p_sep(p->data[i])) {
        WStr prefix={p->data,i,0};
        if(p_mkdir(ctx,&prefix)<0) return 0;
    }
    WInt result=p_mkdir(ctx,p); return result>0;
}
WInt wd_m_fs_remove(void *ctx,const WStr *p) {
    WInt kind=p_kind(ctx,p,0); if(kind<=0) return 0;
#ifdef _WIN32
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,p,&owner); if(!wide) return 0;
    unsigned attributes=GetFileAttributesW(wide);
    int result=(attributes&16)?RemoveDirectoryW(wide):DeleteFileW(wide); wd_m_release(owner);
    if(result) return 1;
#else
    WStr terminated={0}; if(!p_terminated(ctx,&terminated,p)) return 0;
    WInt result=p_sys(kind==2?84:87,(WInt)(intptr_t)terminated.data,0,0,0); wd_m_str_drop(&terminated);
    if(!result) return 1;
    if(result==-2) return 0;
#endif
    p_error(ctx,"cannot remove path: ",p); return 0;
}
WInt wd_m_fs_size(void *ctx,const WStr *p) {
    if(!p_valid(ctx,p)) return 0;
#ifdef _WIN32
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,p,&owner); if(!wide) return 0;
    void *handle=CreateFileW(wide,0,7,0,3,0x02000000,0); wd_m_release(owner);
    unsigned info[13]={0}; WInt size=0;
    if(handle==(void*)(intptr_t)-1) { p_error(ctx,"file size requires a regular file: ",p); return 0; }
    int regular=GetFileType(handle)==1 && GetFileInformationByHandle(handle,info) && !(info[0]&16);
    int ok=regular && GetFileSizeEx(handle,&size); CloseHandle(handle);
    if(!regular) p_error(ctx,"file size requires a regular file: ",p);
    else if(!ok) p_error(ctx,"cannot read file size: ",p);
#else
    WStr terminated={0}; if(!p_terminated(ctx,&terminated,p)) return 0;
    uint64_t info[18]={0}; WInt size=0;
    WInt result=p_sys(4,(WInt)(intptr_t)terminated.data,(WInt)(intptr_t)info,0,0); wd_m_str_drop(&terminated);
    if(result<0 || ((uint32_t)info[3]&0170000)!=0100000) p_error(ctx,"file size requires a regular file: ",p);
    else size=(WInt)info[6];
#endif
    if(size<0) { p_error(ctx,"file size exceeds int range: ",p); return 0; }
    return wd_rt_unwinding(ctx)?0:size;
}
void wd_m_fs_kind(void *ctx,WStr *out,const WStr *p) {
    static const char *names[]={"missing","file","directory","symlink","other"};
    WInt kind=p_kind(ctx,p,0); *out=(WStr){0}; if(kind<0) return;
    const char *word=names[kind]; WInt n=0; while(word[n]) ++n; *out=(WStr){(unsigned char*)word,n,0};
}
void wd_m_path_resolve(void *ctx,WStr *out,const WStr *p,WInt allow_missing) {
    *out=(WStr){0}; WStr absolute={0}; wd_m_path_absolute(ctx,&absolute,p); if(wd_rt_unwinding(ctx)) return;
#ifdef _WIN32
    WStr current=absolute,suffix={0};
    int trailing_missing=0;
    if(allow_missing && current.size>p_root(&current) && p_sep(current.data[current.size-1])) {
        WStr trimmed={0}; WInt n=current.size;
        while(n>p_root(&current) && p_sep(current.data[n-1])) --n;
        p_copy(ctx,&trimmed,current.data,n);
        if(!wd_rt_unwinding(ctx) && p_kind(ctx,&trimmed,1)!=2) {
            wd_m_str_drop(&current); current=trimmed; trimmed=(WStr){0}; trailing_missing=1;
        }
        wd_m_str_drop(&trimmed);
    }
    for(;;) {
        WOwner *owner=0; unsigned short *wide=p_wide(ctx,&current,&owner); if(!wide) break;
        void *handle=CreateFileW(wide,0,7,0,3,0x02000000,0); unsigned error=GetLastError(); wd_m_release(owner);
        if(handle!=(void*)(intptr_t)-1) {
            unsigned count=GetFinalPathNameByHandleW(handle,0,0,0);
            WOwner *buffer=count?allocate(ctx,sizeof(WOwner)+(WInt)(count+1)*2,no_children):0;
            if(buffer) {
                unsigned short *result=(unsigned short*)(buffer+1); unsigned n=GetFinalPathNameByHandleW(handle,result,count+1,0);
                WStr canonical={0}; if(n && n<=count) p_utf8(ctx,&canonical,result,n); else p_error(ctx,"cannot resolve path: ",p);
                if(canonical.size>=4 && canonical.data[0]=='\\' && canonical.data[1]=='\\' && canonical.data[2]=='?' && canonical.data[3]=='\\') {
                    if(canonical.size>=8 && canonical.data[4]=='U' && canonical.data[5]=='N' && canonical.data[6]=='C' && canonical.data[7]=='\\') { for(WInt i=6;i<canonical.size;++i) canonical.data[i-6]=canonical.data[i]; canonical.data[0]='\\'; canonical.size-=6; }
                    else { for(WInt i=4;i<canonical.size;++i) canonical.data[i-4]=canonical.data[i]; canonical.size-=4; }
                }
                if(!wd_rt_unwinding(ctx)) {
                    if(!suffix.size) wd_m_path_normalize(ctx,out,&canonical);
                    else { WStr joined={0}; wd_m_path_join(ctx,&joined,&canonical,&suffix); wd_m_path_normalize(ctx,out,&joined); wd_m_str_drop(&joined); }
                }
                wd_m_str_drop(&canonical); wd_m_release(buffer);
            } else if(!count) p_error(ctx,"cannot resolve path: ",p);
            CloseHandle(handle); break;
        }
        if(!allow_missing || (error!=2 && error!=3)) { p_error(ctx,"cannot resolve path: ",p); break; }
        WStr parent={0},name={0},next={0}; wd_m_path_parent(ctx,&parent,&current); wd_m_path_name(ctx,&name,&current);
        if(j_key(&parent,&current)==0) { wd_m_str_drop(&parent); wd_m_str_drop(&name); p_error(ctx,"cannot resolve path: ",p); break; }
        if(suffix.size) wd_m_path_join(ctx,&next,&name,&suffix); else str_copy(&next,&name);
        wd_m_str_drop(&name); wd_m_str_drop(&suffix); suffix=next; wd_m_str_drop(&current); current=parent;
    }
    wd_m_str_drop(&current); wd_m_str_drop(&suffix);
    for(WInt i=0;i<out->size;++i) if(out->data[i]=='\\') out->data[i]='/';
    if(trailing_missing && !wd_rt_unwinding(ctx)) {
        WStr slash={(unsigned char*)"/",1,0},joined={0};
        wd_m_str_concat(ctx,&joined,out,&slash); wd_m_str_drop(out); *out=joined;
    }
#else
    WStr result={0},pending=absolute; p_copy(ctx,&result,(const unsigned char*)"/",1);
    unsigned links=0; WInt cursor=0; int missing=0;
    while(cursor<pending.size && !wd_rt_unwinding(ctx)) {
        while(cursor<pending.size && p_sep(pending.data[cursor])) ++cursor;
        WInt start=cursor; while(cursor<pending.size && !p_sep(pending.data[cursor])) ++cursor;
        WStr part={pending.data+start,cursor-start,0}; if(!part.size) break;
        if(part.size==1 && part.data[0]=='.') continue;
        if(part.size==2 && part.data[0]=='.' && part.data[1]=='.') { WStr next={0}; wd_m_path_parent(ctx,&next,&result); wd_m_str_drop(&result); result=next; continue; }
        WStr candidate={0}; wd_m_path_join(ctx,&candidate,&result,&part);
        WInt kind=missing?0:p_kind(ctx,&candidate,0);
        /* weakly_canonical preserves the lexical suffix beginning with a
           dangling link, rather than inventing its nonexistent target. */
        if(kind==3 && allow_missing && !p_kind(ctx,&candidate,1) && !wd_rt_unwinding(ctx)) kind=0;
        if(!kind) {
            if(!allow_missing) p_error(ctx,"cannot resolve path: ",p);
            else {
                WStr tail={pending.data+start,pending.size-start,0},joined={0};
                wd_m_path_join(ctx,&joined,&result,&tail); wd_m_str_drop(&result); result=joined;
                missing=1; wd_m_str_drop(&candidate); break;
            }
        }
        if(kind==3 && !wd_rt_unwinding(ctx)) {
            if(++links>40) { fail(ctx,"R4008","too many symbolic links in path"); wd_m_str_drop(&candidate); break; }
            WStr terminated={0},target={0}; p_terminated(ctx,&terminated,&candidate);
            wd_m_str_new(ctx,&target,4096);
            WInt n=target.owner?p_sys(267,-100,(WInt)(intptr_t)terminated.data,(WInt)(intptr_t)target.data,4096):-1;
            if(n<0 || n>=4096) p_error(ctx,"cannot resolve path: ",p);
            else {
                target.size=n; WStr rest={pending.data+cursor,pending.size-cursor,0},tail={0};
                /* The remaining separator must not replace a relative target. */
                while(rest.size && p_sep(rest.data[0])) { ++rest.data; --rest.size; }
                if(rest.size) wd_m_path_join(ctx,&tail,&target,&rest); else str_copy(&tail,&target);
                if(wd_m_path_is_absolute(ctx,&target)) { wd_m_str_drop(&result); p_copy(ctx,&result,(const unsigned char*)"/",1); }
                wd_m_str_drop(&pending); pending=tail; cursor=0;
            }
            wd_m_str_drop(&terminated); wd_m_str_drop(&target); wd_m_str_drop(&candidate); continue;
        }
        if(kind>0 && kind!=2 && cursor<pending.size) {
            if(!allow_missing) p_error(ctx,"cannot resolve path: ",p);
            else {
                WStr tail={pending.data+cursor,pending.size-cursor,0},joined={0};
                wd_m_str_concat(ctx,&joined,&candidate,&tail); wd_m_str_drop(&candidate); candidate=joined;
                missing=1; cursor=pending.size;
            }
        }
        wd_m_str_drop(&result); result=candidate;
    }
    wd_m_str_drop(&pending);
    if(!wd_rt_unwinding(ctx)) { /* weakly_canonical normalizes the missing suffix. */
        wd_m_path_normalize(ctx,out,&result);
    }
    wd_m_str_drop(&result);
#endif
}
void wd_m_path_identity(void *ctx,WStr *out,const WStr *p) {
    wd_m_path_resolve(ctx,out,p,0);
#ifdef _WIN32
    for(WInt i=0;i<out->size;++i) if(out->data[i]>='A' && out->data[i]<='Z') out->data[i]+='a'-'A';
#endif
}
WInt wd_m_path_within(void *ctx,const WStr *p,const WStr *root) {
    WStr target={0},base={0}; wd_m_path_resolve(ctx,&target,p,1);
    if(!wd_rt_unwinding(ctx)) wd_m_path_resolve(ctx,&base,root,0);
    int result=!wd_rt_unwinding(ctx) && target.size>=base.size;
    for(WInt i=0;result && i<base.size;++i) {
        unsigned a=target.data[i],b=base.data[i];
#ifdef _WIN32
        if(a>='A'&&a<='Z') a+='a'-'A'; if(b>='A'&&b<='Z') b+='a'-'A';
#endif
        if(a!=b) result=0;
    }
    if(result && target.size>base.size && base.size && !p_sep(base.data[base.size-1]) && !p_sep(target.data[base.size])) result=0;
    wd_m_str_drop(&target); wd_m_str_drop(&base); return result;
}
static void p_sort(WSeq *seq) {
    /* In-place heapsort: bounded stack and O(n log n), including hostile order. */
    WStr *values=(WStr*)data(seq); WInt count=wd_m_seq_len(seq);
    for(WInt start=count/2;start>0;) {
        --start; WInt root=start;
        while(root<count/2) { WInt child=root*2+1; if(child+1<count && j_key(&values[child],&values[child+1])<0) ++child; if(j_key(&values[root],&values[child])>=0) break; WStr swap=values[root]; values[root]=values[child]; values[child]=swap; root=child; }
    }
    for(WInt end=count-1;end>0;--end) {
        WStr swap=values[0]; values[0]=values[end]; values[end]=swap; WInt root=0;
        while(root<end/2) { WInt child=root*2+1; if(child+1<end && j_key(&values[child],&values[child+1])<0) ++child; if(j_key(&values[root],&values[child])>=0) break; swap=values[root]; values[root]=values[child]; values[child]=swap; root=child; }
    }
}
static void p_entry(void *ctx,WSeq **out,const WStr *name,WInt limit,const WStr *path) {
    if((name->size==1 && name->data[0]=='.') || (name->size==2 && name->data[0]=='.' && name->data[1]=='.')) return;
    if(wd_m_seq_len(*out)>=limit) { p_error(ctx,"directory entry limit exceeded: ",path); return; }
    wd_m_seq_append(ctx,out,name);
}
void *wd_m_fs_list(void *ctx,const WStr *path,WInt limit) {
    if(limit<0) { fail(ctx,"R4008","directory entry limit must be nonnegative"); return 0; }
    WSeq *out=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop); if(!out) return 0;
#ifdef _WIN32
    WStr star={(unsigned char*)"*",1,0},pattern={0}; wd_m_path_join(ctx,&pattern,path,&star);
    WOwner *owner=0; unsigned short *wide=wd_rt_unwinding(ctx)?0:p_wide(ctx,&pattern,&owner); PFind row;
    void *handle=wide?FindFirstFileW(wide,&row):(void*)(intptr_t)-1; wd_m_release(owner); wd_m_str_drop(&pattern);
    if(handle==(void*)(intptr_t)-1) { if(!wd_rt_unwinding(ctx)) p_error(ctx,"cannot list directory: ",path); }
    else { do { WInt n=0; while(n<260 && row.name[n]) ++n; WStr name={0}; p_utf8(ctx,&name,row.name,n); if(!wd_rt_unwinding(ctx)) p_entry(ctx,&out,&name,limit,path); wd_m_str_drop(&name); } while(!wd_rt_unwinding(ctx) && FindNextFileW(handle,&row)); if(!wd_rt_unwinding(ctx) && GetLastError()!=18) p_error(ctx,"cannot list directory: ",path); FindClose(handle); }
#else
    WStr terminated={0}; p_terminated(ctx,&terminated,path);
    WInt fd=wd_rt_unwinding(ctx)?-1:p_sys(257,-100,(WInt)(intptr_t)terminated.data,65536|524288,0); wd_m_str_drop(&terminated);
    if(fd<0) { if(!wd_rt_unwinding(ctx)) p_error(ctx,"cannot list directory: ",path); }
    else {
        unsigned char buffer[8192];
        while(!wd_rt_unwinding(ctx)) {
            WInt n=p_sys(217,fd,(WInt)(intptr_t)buffer,sizeof buffer,0); if(n==-4) continue; if(n==0) break;
            if(n<0) { p_error(ctx,"cannot list directory: ",path); break; }
            for(WInt pos=0;pos<n;) {
                if(n-pos<20) { p_error(ctx,"invalid directory entry: ",path); break; }
                unsigned len=(unsigned)buffer[pos+16]|((unsigned)buffer[pos+17]<<8);
                if(len<20 || len>(unsigned)(n-pos)) { p_error(ctx,"invalid directory entry: ",path); break; }
                WInt size=0; while(size<(WInt)len-19 && buffer[pos+19+size]) ++size;
                if(size==(WInt)len-19) { p_error(ctx,"invalid directory entry: ",path); break; }
                WStr name={0}; p_copy(ctx,&name,buffer+pos+19,size); if(!wd_rt_unwinding(ctx)) p_entry(ctx,&out,&name,limit,path); wd_m_str_drop(&name); pos+=len;
                if(wd_rt_unwinding(ctx)) break;
            }
        }
        wd_file_close(fd);
    }
#endif
    if(wd_rt_unwinding(ctx)) { wd_m_release(out); return 0; } p_sort(out); return out;
}
void wd_m_read_text(void *ctx,WStr *out,const WStr *path) {
    *out=(WStr){0};
    WStr terminated={0};
    if(!p_valid(ctx,path)) return;
    p_copy(ctx,&terminated,path->data,path->size); if(!terminated.owner) return;
    WInt fd=wd_file_open(terminated.data,terminated.size,0); wd_m_str_drop(&terminated);
    if(fd<0) { p_error(ctx,"cannot open file for reading: ",path); return; }
    JBuffer content={ctx,0}; unsigned char buffer[8192]; int failed=0;
    while(!wd_rt_unwinding(ctx)) {
        WInt n=wd_file_read(fd,buffer,sizeof buffer);
        if(n==-4) continue;
        if(n<0 || n>(WInt)sizeof buffer) { failed=1; break; }
        if(!n) break;
        jb_add(&content,buffer,n);
    }
    wd_file_close(fd);
    if(failed) p_error(ctx,"cannot read file: ",path);
    jb_finish(&content,out);
}
void wd_m_write_text(void *ctx,const WStr *path,const WStr *text) {
    if(!p_valid(ctx,path)) return;
    WStr terminated={0}; p_copy(ctx,&terminated,path->data,path->size); if(!terminated.owner) return;
    WInt fd=wd_file_open(terminated.data,terminated.size,1); wd_m_str_drop(&terminated);
    if(fd<0) { p_error(ctx,"cannot open file for writing: ",path); return; }
    WInt offset=0; int failed=0;
    while(offset<text->size) {
        WInt count=text->size-offset; if(count>16777216) count=16777216;
        WInt n=wd_file_write(fd,text->data+offset,count);
        if(n==-4) continue;
        if(n<=0 || n>count) { failed=1; break; } offset+=n;
    }
    if(wd_file_close(fd)<0) failed=1;
    if(failed) p_error(ctx,"cannot write file: ",path);
}
/* No wood code can catch a stack overflow, so this reports it and ends the program. Sanitizers keep
   their own fault handling. */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define WD_STACK_GUARD 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define WD_STACK_GUARD 0
#endif
#endif
#if !defined(WD_STACK_GUARD) && !defined(__wasm__)
#define WD_STACK_GUARD 1
static const char p_overflow[]="runtime error[R4028]: stack overflow: functions called each other too deeply\n  help: wood explain R4028\n";
#ifdef _WIN32
WD_DLL void *AddVectoredExceptionHandler(unsigned,long (*)(void *));
WD_DLL int SetThreadStackGuarantee(unsigned *);
WD_DLL void *GetStdHandle(unsigned);
WD_DLL int WriteFile(void*,const void*,unsigned,unsigned*,void*);
WD_DLL void ExitProcess(unsigned);
static long p_stack_fault(void *pointers) {
    if(**(unsigned **)pointers!=0xC00000FDu) return 0;
    unsigned written=0;
    WriteFile(GetStdHandle((unsigned)-12),p_overflow,(unsigned)sizeof(p_overflow)-1,&written,0);
    ExitProcess(70);
    return 0;
}
void wd_m_stack_guard(void) {
    unsigned reserve=65536;
    SetThreadStackGuarantee(&reserve);
    AddVectoredExceptionHandler(1,p_stack_fault);
}
#else
static unsigned char p_fault_stack[65536];
typedef struct { void *handler; unsigned long flags; void *restorer; unsigned long mask; } PSignalAction;
typedef struct { void *base; int flags; unsigned long size; } PSignalStack;
/* si_addr and the saved rsp in the x86-64 Linux siginfo and ucontext. A fault beside rsp is the stack. */
static void p_stack_fault(int signal,void *info,void *context) {
    unsigned long address=*(unsigned long *)((unsigned char *)info+16);
    unsigned long sp=*(unsigned long *)((unsigned char *)context+160);
    if(address+65536>=sp && address<=sp+65536) {
        p_sys(1,2,(WInt)(intptr_t)p_overflow,(WInt)sizeof(p_overflow)-1,0);
        p_sys(231,70,0,0,0);
    }
    /* SA_RESETHAND and SA_NODEFER: anything else dies by the ordinary signal. */
    p_sys(62,p_sys(39,0,0,0,0),signal,0,0);
}
void wd_stack_guard_thread(void *base,unsigned long size) {
    PSignalStack alternate={base,0,size};
    p_sys(131,(WInt)(intptr_t)&alternate,0,0,0);
}
void wd_m_stack_guard(void) {
    wd_stack_guard_thread(p_fault_stack,sizeof(p_fault_stack));
    PSignalAction action={(void *)p_stack_fault,0x4|0x08000000|0x04000000|0x40000000|0x80000000u,(void *)p_stack_fault,0};
    p_sys(13,11,(WInt)(intptr_t)&action,0,8);
}
#endif
#else
void wd_m_stack_guard(void) {}
#endif
static WInt p_argc;
static const unsigned char *const *p_argv;
void wd_m_args_init(WInt argc,const void *argv) {
    p_argc=argc; p_argv=argv;
#ifndef _WIN32
    wd_m_stack_guard();
#endif
}
void *wd_m_args(void *ctx) {
    WSeq *out=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop); if(!out) return 0;
#ifdef _WIN32
    (void)p_argc; (void)p_argv;
    const unsigned short *command=GetCommandLineW(); WInt i=0;
    /* CRT's argv[0] has separate quoting rules and is omitted by Wood args(). */
    int quote=0; while(command[i] && (quote || (command[i]!=' ' && command[i]!='\t'))) { if(command[i]=='"') quote=!quote; ++i; }
    while(command[i] && !wd_rt_unwinding(ctx)) {
        while(command[i]==' ' || command[i]=='\t') ++i; if(!command[i]) break;
        JBuffer wide={ctx,0}; quote=0;
        while(command[i] && (quote || (command[i]!=' ' && command[i]!='\t'))) {
            WInt slashes=0; while(command[i]=='\\') { ++slashes; ++i; }
            if(command[i]=='"') {
                unsigned short slash='\\'; for(WInt k=0;k<slashes/2;++k) jb_add(&wide,&slash,2);
                if(slashes%2) { unsigned short q='"'; jb_add(&wide,&q,2); ++i; }
                else if(quote && command[i+1]=='"') { unsigned short q='"'; jb_add(&wide,&q,2); i+=2; }
                else { quote=!quote; ++i; }
            } else {
                unsigned short slash='\\'; for(WInt k=0;k<slashes;++k) jb_add(&wide,&slash,2);
                if(!command[i] || (!quote && (command[i]==' ' || command[i]=='\t'))) break;
                jb_add(&wide,command+i,2); ++i;
            }
        }
        WStr arg={0}; if(!wd_rt_unwinding(ctx)) p_utf8(ctx,&arg,wide.seq?(unsigned short*)data(wide.seq):0,wd_m_seq_len(wide.seq)/2);
        if(!wd_rt_unwinding(ctx)) wd_m_seq_append(ctx,&out,&arg); wd_m_str_drop(&arg); wd_m_release(wide.seq);
    }
#else
    for(WInt i=1;i<p_argc && !wd_rt_unwinding(ctx);++i) {
        WInt n=0; while(p_argv[i][n]) ++n; WStr arg={0}; p_copy(ctx,&arg,p_argv[i],n); if(!wd_rt_unwinding(ctx)) wd_m_seq_append(ctx,&out,&arg); wd_m_str_drop(&arg);
    }
#endif
    if(wd_rt_unwinding(ctx)) { wd_m_release(out); return 0; } return out;
}

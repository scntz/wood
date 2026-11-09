/* Private native driver process runner. No shell, libc or C++ runtime.
   Keep process trees reserved until cleanup, bound captured output, and restore
   cancellation handlers. Mirrors the policies in src/process.cpp. */
static volatile int32_t d_cancelled;
#ifdef _WIN32
typedef struct { uint32_t size; void *descriptor; int inherit; } DSecurity;
typedef struct {
    uint32_t size; void *reserved,*desktop,*title;
    uint32_t x,y,width,height,cols,rows,fill,flags;
    uint16_t show,reserved_size; void *reserved_bytes,*input,*output,*error;
} DStartup;
typedef struct { void *process,*thread; uint32_t pid,tid; } DProcess;
typedef struct { int64_t process_time,job_time; uint32_t flags; size_t min,max;
    uint32_t active; uintptr_t affinity; uint32_t priority,scheduling; } DLimits;
typedef struct { DLimits basic; uint64_t io[6]; size_t process_memory,job_memory,peak_process,peak_job; } DJob;
_Static_assert(sizeof(DSecurity)==24 && sizeof(DStartup)==104 && sizeof(DProcess)==24 && sizeof(DJob)==144,"Windows driver ABI");
WD_DLL void *CreateJobObjectW(void*,const unsigned short*);
WD_DLL int SetInformationJobObject(void*,int,void*,unsigned);
WD_DLL int CreatePipe(void**,void**,void*,unsigned);
WD_DLL int SetHandleInformation(void*,unsigned,unsigned);
WD_DLL void *GetStdHandle(unsigned);
WD_DLL int CreateProcessW(const unsigned short*,unsigned short*,void*,void*,int,unsigned,void*,void*,DStartup*,DProcess*);
WD_DLL int AssignProcessToJobObject(void*,void*);
WD_DLL unsigned ResumeThread(void*);
WD_DLL int TerminateJobObject(void*,unsigned);
WD_DLL int TerminateProcess(void*,unsigned);
WD_DLL unsigned WaitForSingleObject(void*,unsigned);
WD_DLL int GetExitCodeProcess(void*,unsigned*);
WD_DLL int PeekNamedPipe(void*,void*,unsigned,void*,unsigned*,void*);
WD_DLL int ReadFile(void*,void*,unsigned,unsigned*,void*);
WD_DLL uint64_t GetTickCount64(void);
WD_DLL void Sleep(unsigned);
WD_DLL int SetConsoleCtrlHandler(int(*)(unsigned),int);
static int d_interrupt(unsigned event) {
    if(event!=0 && event!=1) return 0;
    __atomic_store_n(&d_cancelled,1,__ATOMIC_RELAXED); return 1;
}
static WInt d_milliseconds(void) { return (WInt)GetTickCount64(); }
static void d_pause(void) { Sleep(5); }
#else
#if defined(__wasm__)
static WInt d_sys(WInt n,WInt a,WInt b,WInt c,WInt d,WInt e,WInt f) { return wood_syscall(n,a,b,c,d,e,f); }
static void d_sigreturn(void) {}
#else
static WInt d_sys(WInt n,WInt a,WInt b,WInt c,WInt d,WInt e,WInt f) {
    register WInt r10 __asm__("r10")=d,r8 __asm__("r8")=e,r9 __asm__("r9")=f; WInt out;
    __asm__ volatile("syscall":"=a"(out):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory"); return out;
}
__attribute__((naked)) static void d_sigreturn(void) { __asm__ volatile("mov $15, %rax; syscall"); }
#endif
typedef struct { void (*handler)(int); uint64_t flags; void (*restorer)(void); uint64_t mask; } DSignal;
static void d_interrupt(int signal) { (void)signal; __atomic_store_n(&d_cancelled,1,__ATOMIC_RELAXED); }
static WInt d_milliseconds(void) {
    WInt time[2]={0}; if(d_sys(228,1,(WInt)(intptr_t)time,0,0,0,0)<0) return -1;
    return time[0]>(INT64_MAX-999)/1000?INT64_MAX:time[0]*1000+time[1]/1000000;
}
static void d_pause(void) { WInt time[2]={0,5000000}; d_sys(35,(WInt)(intptr_t)time,0,0,0,0,0); }
#endif
static int d_deadline(void *ctx,WInt start,WInt timeout) {
    if(__atomic_load_n(&d_cancelled,__ATOMIC_RELAXED)) { fail(ctx,"D5005","process cancelled"); return 1; }
    WInt now=d_milliseconds();
    if(now<0) { fail(ctx,"D5001","cannot read process clock"); return 1; }
    if(timeout && now-start>=timeout) {
        JBuffer b={ctx,0}; WStr value={0},message={0}; wd_m_int_str(ctx,&value,timeout);
        jb_word(&b,"process exceeded timeout of "); jb_add(&b,value.data,value.size); jb_word(&b," ms"); jb_finish(&b,&message); wd_m_str_drop(&value);
        if(!wd_rt_unwinding(ctx)) wd_rt_error_value(ctx,"D5003",5,&message);
        wd_m_str_drop(&message); return 1;
    }
    return 0;
}
static int d_capture(void *ctx,WInt fd,const unsigned char *bytes,WInt n,WInt *count,WInt limit) {
    WInt allowed=n<limit-*count?n:limit-*count,at=0;
    while(at<allowed) {
        WInt written=wd_file_write(fd,bytes+at,allowed-at);
        if(written==-4) continue;
        if(written<=0 || written>allowed-at) { fail(ctx,"D5001","cannot write process log"); return 0; }
        at+=written;
    }
    *count+=allowed;
    if(allowed!=n) {
        JBuffer b={ctx,0}; WStr value={0},message={0}; wd_m_int_str(ctx,&value,limit);
        jb_word(&b,"captured process output exceeds "); jb_add(&b,value.data,value.size); jb_word(&b," bytes"); jb_finish(&b,&message); wd_m_str_drop(&value);
        if(!wd_rt_unwinding(ctx)) wd_rt_error_value(ctx,"D5004",5,&message);
        wd_m_str_drop(&message); return 0;
    }
    return 1;
}
#ifdef _WIN32
/* Windows ignores trailing dots and spaces in a file name, so they cannot hide the extension. */
static int d_batch(const WStr *program) {
    WInt n=program->size; while(n && (program->data[n-1]=='.' || program->data[n-1]==' ')) --n;
    if(n<4 || program->data[n-4]!='.') return 0;
    char kind[4]; for(int i=0;i<3;++i) { unsigned char c=program->data[n-3+i]; kind[i]=c>='A' && c<='Z'?c+32:c; } kind[3]=0;
    return (kind[0]=='b' && kind[1]=='a' && kind[2]=='t') || (kind[0]=='c' && kind[1]=='m' && kind[2]=='d');
}
#endif
static WInt d_process(void *ctx,const WSeq *arguments,const WStr *log,WInt timeout,WInt limit,int inherit_stdin) {
    if(wd_rt_unwinding(ctx)) return 2;
    WInt count=wd_m_seq_len(arguments),fd=-1,result=2,captured=0,start=d_milliseconds();
    if(count<1 || !((WStr*)wd_m_seq_read(arguments,0))->size) { fail(ctx,"D5001","empty process command"); return 2; }
    if(timeout<0 || timeout>86400000 || limit<1 || limit>16777216 || start<0) { fail(ctx,"D5001","invalid child process limits"); return 2; }
#ifdef _WIN32
    /* cmd.exe parses a batch file's arguments, so they could carry commands. */
    if(d_batch(wd_m_seq_read(arguments,0))) { fail(ctx,"D5001","batch files cannot be started directly, because cmd.exe would parse their arguments"); return 2; }
    /* Windows looks for a bare program name in the current directory first, so a project could
       supply its own git or llc. Children inherit this, and PATH is still searched. */
    WStr search=d_literal("NoDefaultCurrentDirectoryInExePath"),enabled=d_literal("1");
    d_environment_set(ctx,&search,&enabled);
    if(wd_rt_unwinding(ctx)) return 2;
#endif
    if(log->size) { fd=wd_file_open(log->data,log->size,1); if(fd<0) { fail(ctx,"D5001","cannot create process log"); return 2; } }
    __atomic_store_n(&d_cancelled,0,__ATOMIC_RELAXED);
#ifdef _WIN32
    void *job=0,*reader=0,*writer=0,*null_input=0; DProcess child={0}; int handler=0;
    JBuffer command={ctx,0}; WStr command8={0}; WOwner *wide_owner=0; unsigned short *wide=0;
    for(WInt i=0;i<count;++i) {
        const WStr *arg=wd_m_seq_read(arguments,i); if(i) jb_byte(&command,' ');
        /* Some system programs (notably wsl.exe) parse their own command line.
           Preserve simple arguments literally; quote only when required. */
        int quoted=arg->size==0;
        for(WInt j=0;j<arg->size;++j) if(arg->data[j]==' ' || arg->data[j]=='\t' || arg->data[j]=='"') quoted=1;
        if(!quoted) { jb_add(&command,arg->data,arg->size); continue; }
        jb_byte(&command,'"');
        WInt slashes=0;
        for(WInt j=0;j<arg->size;++j) {
            unsigned char c=arg->data[j]; if(c=='\\') { ++slashes; continue; }
            WInt n=c=='"'?slashes*2+1:slashes;
            while(n--) jb_byte(&command,'\\'); slashes=0; jb_byte(&command,c);
        }
        for(WInt j=0;j<slashes*2;++j) jb_byte(&command,'\\'); jb_byte(&command,'"');
    }
    jb_finish(&command,&command8);
    wide=p_wide(ctx,&command8,&wide_owner); if(!wide) goto done;
    if((wide_owner->bytes-sizeof(WOwner))/2-1>32766) { fail(ctx,"D5001","process command exceeds Windows limit"); goto done; }
    handler=SetConsoleCtrlHandler(d_interrupt,1); if(!handler) { fail(ctx,"D5001","cannot install child cancellation handler"); goto done; }
    job=CreateJobObjectW(0,0); DJob limits={0}; limits.basic.flags=0x2000;
    if(!job || !SetInformationJobObject(job,9,&limits,sizeof limits)) { fail(ctx,"D5001","cannot configure child process job"); goto done; }
    DStartup startup={0}; startup.size=sizeof startup;
    if(!inherit_stdin) {
        const unsigned short name[]={'N','U','L',0}; DSecurity security={sizeof security,0,1};
        null_input=CreateFileW(name,0x80000000,3,&security,3,0,0);
        if(null_input==(void*)(intptr_t)-1) { null_input=0; fail(ctx,"D5001","cannot open compiler null input"); goto done; }
    }
    if(fd>=0) {
        DSecurity security={sizeof security,0,1};
        if(!CreatePipe(&reader,&writer,&security,0) || !SetHandleInformation(reader,1,0)) { fail(ctx,"D5001","cannot create process output pipe"); goto done; }
        startup.flags=0x100; startup.input=GetStdHandle((unsigned)-10); startup.output=writer; startup.error=writer;
    }
    if(!inherit_stdin) {
        startup.flags=0x100; startup.input=null_input;
        if(fd<0) { startup.output=GetStdHandle((unsigned)-11); startup.error=GetStdHandle((unsigned)-12); }
    }
    if(!CreateProcessW(0,wide,0,0,1,4,0,0,&startup,&child)) { fail(ctx,"D5001","cannot start native compiler or program"); goto done; }
    if(!AssignProcessToJobObject(job,child.process)) { TerminateProcess(child.process,2); fail(ctx,"D5001","cannot manage child process tree"); goto done; }
    if(ResumeThread(child.thread)==UINT32_MAX) { fail(ctx,"D5001","cannot resume child process"); goto done; }
    if(writer) { CloseHandle(writer); writer=0; }
    int exited=0; unsigned code=2;
    for(;;) {
        unsigned waited=WaitForSingleObject(child.process,0);
        if(waited==UINT32_MAX) { fail(ctx,"D5001","cannot wait for child process"); break; }
        if(!waited && !exited) {
            if(!GetExitCodeProcess(child.process,&code)) { fail(ctx,"D5001","cannot read child status"); break; }
            exited=1; TerminateJobObject(job,2);
        }
        int drained=1;
        if(reader) for(unsigned batch=0;batch<16;++batch) {
            unsigned available=0,n=0; unsigned char buffer[8192];
            if(!PeekNamedPipe(reader,0,0,0,&available,0)) { if(GetLastError()!=109) fail(ctx,"D5001","cannot inspect process output"); break; }
            if(!available) break;
            if(available>sizeof buffer) available=sizeof buffer;
            if(!ReadFile(reader,buffer,available,&n,0) || n>available) { fail(ctx,"D5001","cannot read process output"); break; }
            if(!d_capture(ctx,fd,buffer,n,&captured,limit)) break;
            if(batch==15) drained=0;
        }
        if(wd_rt_unwinding(ctx)) break;
        if(exited && drained) { result=code<=255?code:2; break; }
        if(d_deadline(ctx,start,timeout)) break;
        d_pause();
    }
done:
    if(child.process) { if(job) TerminateJobObject(job,2); WaitForSingleObject(child.process,UINT32_MAX); CloseHandle(child.process); }
    if(child.thread) CloseHandle(child.thread);
    if(writer) CloseHandle(writer); if(reader) CloseHandle(reader); if(job) CloseHandle(job);
    if(null_input) CloseHandle(null_input);
    if(handler) SetConsoleCtrlHandler(d_interrupt,0);
    wd_m_release(wide_owner); wd_m_str_drop(&command8);
#else
    WOwner *argv_owner=0,*env_owner=0; const void *child_env=p_argv?(const void*)(p_argv+p_argc+1):0; WSeq *candidates=0; WStr path={0}; WInt null_input=-1;
    WInt reader=-1,writer=-1,pid=-1; DSignal old_int={0},old_term={0}; int have_int=0,have_term=0,reaped=0;
    WInt argv_bytes;
    if(!product(ctx,count+1,sizeof(char*),&argv_bytes)) goto done;
    argv_owner=allocate(ctx,sizeof(WOwner)+argv_bytes,no_children); if(!argv_owner) goto done;
    const char **argv=(const char**)(argv_owner+1);
    for(WInt i=0;i<count;++i) argv[i]=(const char*)((const WStr*)wd_m_seq_read(arguments,i))->data; argv[count]=0;
    if(wd_m_seq_len(d_env_extra)) {
        const unsigned char *const *inherited=p_argv?p_argv+p_argc+1:0; WInt extra=wd_m_seq_len(d_env_extra),kept=0,env_bytes;
        if(inherited) while(inherited[kept]) ++kept;
        if(!product(ctx,extra+kept+1,sizeof(char*),&env_bytes)) goto done;
        env_owner=allocate(ctx,sizeof(WOwner)+env_bytes,no_children); if(!env_owner) goto done;
        const unsigned char **block=(const unsigned char**)(env_owner+1);
        for(WInt i=0;i<extra;++i) block[i]=((const WStr*)wd_m_seq_read(d_env_extra,extra-1-i))->data;
        for(WInt i=0;i<kept;++i) block[extra+i]=inherited[i];
        block[extra+kept]=0; child_env=block;
    }
    candidates=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop); if(!candidates) goto done;
    const WStr *program=wd_m_seq_read(arguments,0); WStr slash=d_literal("/");
    if(wd_m_find(program,&slash)>=0) wd_m_seq_append(ctx,&candidates,program);
    else {
        WStr key=d_literal("PATH"); const unsigned char *raw=d_environment_raw(&key);
        if(raw) { WInt size=0; while(raw[size]) ++size; p_copy(ctx,&path,raw,size); }
        else path=d_literal("/bin:/usr/bin");
        WInt first=0;
        for(WInt i=0;i<=path.size;++i) if(i==path.size || path.data[i]==':') {
            WStr part={path.data+first,i-first,0},candidate={0}; if(!part.size) part=d_literal(".");
            wd_m_path_join(ctx,&candidate,&part,program); if(!wd_rt_unwinding(ctx)) wd_m_seq_append(ctx,&candidates,&candidate); wd_m_str_drop(&candidate); first=i+1;
        }
    }
    if(wd_rt_unwinding(ctx)) goto done;
    DSignal action={d_interrupt,0x04000000,d_sigreturn,0};
    if(d_sys(13,2,(WInt)(intptr_t)&action,(WInt)(intptr_t)&old_int,8,0,0)<0) { fail(ctx,"D5001","cannot install child cancellation handler"); goto done; } have_int=1;
    if(d_sys(13,15,(WInt)(intptr_t)&action,(WInt)(intptr_t)&old_term,8,0,0)<0) { fail(ctx,"D5001","cannot install child cancellation handler"); goto done; } have_term=1;
    if(fd>=0) {
        int pipes[2];
        if(d_sys(293,(WInt)(intptr_t)pipes,0x80000,0,0,0,0)<0) { fail(ctx,"D5001","cannot create process pipe"); goto done; }
        reader=pipes[0]; writer=pipes[1];
        if(d_sys(72,reader,4,0x800,0,0,0)<0) { fail(ctx,"D5001","cannot configure process pipe"); goto done; }
    }
    if(!inherit_stdin) {
        const char name[]="/dev/null";
        null_input=d_sys(2,(WInt)(intptr_t)name,0x80000,0,0,0,0);
        if(null_input<0) { fail(ctx,"D5001","cannot open compiler null input"); goto done; }
    }
    pid=d_sys(57,0,0,0,0,0,0);
    if(pid<0) { fail(ctx,"D5001","cannot fork"); goto done; }
    if(!pid) {
        if(d_sys(109,0,0,0,0,0,0)<0) goto child_failure;
        if(null_input>=0) {
            if(null_input==0) { if(d_sys(72,0,2,0,0,0,0)<0) goto child_failure; }
            else if(d_sys(33,null_input,0,0,0,0,0)<0) goto child_failure;
            if(null_input>2) d_sys(3,null_input,0,0,0,0,0);
        }
        if(writer>=0) {
            if(d_sys(33,writer,1,0,0,0,0)<0 || d_sys(33,writer,2,0,0,0,0)<0) goto child_failure;
            d_sys(3,reader,0,0,0,0,0); d_sys(3,writer,0,0,0,0,0);
        }
        /* Restore inherited handlers before exec. No allocation after fork. */
        d_sys(13,2,(WInt)(intptr_t)&old_int,0,8,0,0); d_sys(13,15,(WInt)(intptr_t)&old_term,0,8,0,0);
        for(WInt i=0;i<wd_m_seq_len(candidates);++i) {
            WStr *candidate=wd_m_seq_read(candidates,i);
            d_sys(59,(WInt)(intptr_t)candidate->data,(WInt)(intptr_t)argv,(WInt)(intptr_t)child_env,0,0,0);
        }
child_failure:
        { const char message[]="wood: could not execute native compiler or program\n"; d_sys(1,2,(WInt)(intptr_t)message,sizeof message-1,0,0,0); }
        d_sys(231,127,0,0,0,0,0); __builtin_unreachable();
    }
    { WInt grouped=d_sys(109,pid,pid,0,0,0,0); if(grouped<0 && grouped!=-13 && grouped!=-3) { fail(ctx,"D5001","cannot manage child process group"); goto done; } }
    if(writer>=0) { d_sys(3,writer,0,0,0,0,0); writer=-1; }
    int exited=0;
    for(;;) {
        union { uint64_t alignment; unsigned char bytes[128]; } state={0};
        WInt waited=d_sys(247,1,pid,(WInt)(intptr_t)&state,0x1000005,0,0);
        if(waited==-4) continue;
        if(waited<0) { fail(ctx,"D5001","cannot inspect child status"); break; }
        int32_t child_pid=0; copy_bytes(&child_pid,state.bytes+16,sizeof child_pid);
        if(child_pid==pid && !exited) { exited=1; d_sys(62,-pid,9,0,0,0,0); }
        int drained=1;
        if(reader>=0) for(unsigned batch=0;batch<16;++batch) {
            unsigned char buffer[8192]; WInt n=wd_file_read(reader,buffer,sizeof buffer);
            if(n==-4) continue; if(n==-11 || n==0) break;
            if(n<0 || n>(WInt)sizeof buffer) { fail(ctx,"D5001","cannot read process output"); break; }
            if(!d_capture(ctx,fd,buffer,n,&captured,limit)) break;
            if(batch==15) drained=0;
        }
        if(wd_rt_unwinding(ctx)) break;
        if(exited && drained) {
            int status=0; WInt n;
            do { n=d_sys(61,pid,(WInt)(intptr_t)&status,0,0,0,0); } while(n==-4);
            if(n<0) fail(ctx,"D5001","cannot wait for child process");
            else { reaped=1; result=(status&127)?128+(status&127):(status>>8)&255; }
            break;
        }
        if(d_deadline(ctx,start,timeout)) break;
        d_pause();
    }
done:
    if(pid>0 && !reaped) { d_sys(62,-pid,9,0,0,0,0); d_sys(62,pid,9,0,0,0,0); while(d_sys(61,pid,0,0,0,0,0)==-4) {} }
    if(reader>=0) d_sys(3,reader,0,0,0,0,0); if(writer>=0) d_sys(3,writer,0,0,0,0,0);
    if(null_input>=0) d_sys(3,null_input,0,0,0,0,0);
    if(have_term) d_sys(13,15,(WInt)(intptr_t)&old_term,0,8,0,0);
    if(have_int) d_sys(13,2,(WInt)(intptr_t)&old_int,0,8,0,0);
    wd_m_release(argv_owner); wd_m_release(env_owner); wd_m_release(candidates); wd_m_str_drop(&path);
#endif
    if(fd>=0 && wd_file_close(fd)<0 && !wd_rt_unwinding(ctx)) fail(ctx,"D5001","cannot close process log");
    return result;
}

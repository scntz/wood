/* Joinable workers exchange immutable JSON only. Task owns its worker, context
   and result; final release requests stop and joins before freeing any of them.
   Registry IDs are atomic; a lookup dereferences ONLY its own live thread slot.
   This implements per-thread callback/stop state without ELF/CRT TLS startup. */
typedef struct WTask WTask;
typedef struct WCallback WCallback;
struct WCallback { const void *key; void *ctx,*function; WCallback *previous; };
typedef struct { WCallback *callbacks; WTask *task; } WLocal;
static WLocal wt_primary;
static WInt wt_primary_id,wt_ids[256];
static WLocal *wt_locals[256];
static WLock wt_registry_lock;
static unsigned wt_active;
typedef unsigned (*WThreadEntry)(void *);
typedef void *(*WTaskWork)(void *,void *);
typedef void *(*WTaskIoWork)(void *,void *,void *);
struct WTask {
    WOwner owner;
    WTaskWork work;
    WTaskIoWork io_work;
    WStream *io;
    WLock start_gate;
    WStr site_file;
    WInt site_line,site_column;
    WJson *payload,*result;
    void *context,*stack;
    WInt handle;
    WLocal local;
    int32_t completed,cancelled,closed,tid;
    int slot;
    int hosted_thread;
#ifdef _WIN32
    void *event;
#endif
};
#ifdef _WIN32
WD_DLL unsigned GetCurrentThreadId(void);
WD_DLL void *CreateThread(void *,WSize,WThreadEntry,void *,unsigned,unsigned *);
WD_DLL void *CreateEventW(void *,int,int,const unsigned short *);
WD_DLL int SetEvent(void *);
static WInt wt_id(void) { return GetCurrentThreadId(); }
void wd_native_context_missing_forward(void *to,void *from) { (void)from; fail(to,"R4022","tasks require the current ABI-4 LLVM core"); }
#pragma comment(linker,"/alternatename:wd_rt_context_forward=wd_native_context_missing_forward")
void wd_native_context_missing_site(void *ctx,const WStr *file,WInt line,WInt column) {
    (void)file; (void)line; (void)column; fail(ctx,"R4022","socket tasks require the current ABI-4 LLVM core");
}
#pragma comment(linker,"/alternatename:wd_rt_context_site=wd_native_context_missing_site")
#else
static WInt wt_id(void) {
    WInt id=d_sys(186,0,0,0,0,0,0);
    if(id<=0) __builtin_trap(); /* Never alias unknown threads to one context. */
    return id;
}
#ifdef WOOD_NATIVE_HOSTED_THREADS
/* Only sanitizer/oracle ports supply these real pthread operations. */
extern WInt wd_test_thread_start(WThreadEntry,void *);
extern void wd_test_thread_join(WInt);
#else
/* Raw x86-64 clone changes rsp: it must never return through a compiler C frame
   in the child. Two values on the new aligned stack carry entry and argument.
   The parent-set/child-clear TID lives in Task until the joining futex completes. */
WInt wd_native_clone(WThreadEntry,void *,void *,int32_t *);
#if defined(__wasm__)
/* The browser has no threads here: spawning a task reports the failure. */
WInt wd_native_clone(WThreadEntry entry,void *argument,void *stack,int32_t *tid) { (void)entry; (void)argument; (void)stack; (void)tid; return -38; }
#else
/* Keep this a fixed external SysV ABI boundary, not an internal C function
   whose apparently unused parameters/return could be rewritten by optimizers. */
__asm__(".text\n.p2align 4\n.globl wd_native_clone\n.hidden wd_native_clone\n"
    ".type wd_native_clone,@function\nwd_native_clone:\n"
    "mov %rdi,-8(%rdx); mov %rsi,-16(%rdx); lea -16(%rdx),%rsi;"
    "mov %rcx,%r10; mov %rcx,%rdx; xor %r8,%r8; mov $0x350f00,%edi; mov $56,%eax; syscall;"
    "test %rax,%rax; jnz 1f; pop %rdi; pop %rax; call *%rax; xor %edi,%edi; mov $60,%eax; syscall; ud2; 1: ret\n"
    ".size wd_native_clone,.-wd_native_clone\n");
#endif
#endif
#endif
void wd_m_threads_init(void) {
    WInt empty=0;
    (void)__atomic_compare_exchange_n(&wt_primary_id,&empty,wt_id(),0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
}
static WLocal *wt_current(void) {
    WInt id=wt_id();
    if(id==__atomic_load_n(&wt_primary_id,__ATOMIC_ACQUIRE)) return &wt_primary;
    for(unsigned i=0;i<256;++i)
        if(id==__atomic_load_n(wt_ids+i,__ATOMIC_ACQUIRE)) return wt_locals[i];
    return 0; /* Unregistered foreign threads cannot borrow a Wood callback. */
}
WInt wd_m_worker_thread(void) { WLocal *local=wt_current(); return local && local->task; }
void wd_m_callback_push(void *memory,const void *key,void *ctx,void *function) {
    wd_m_threads_init(); WLocal *local=wt_current();
    if(!local) __builtin_trap();
    WCallback *scope=memory;
    *scope=(WCallback){key,ctx,function,local->callbacks}; local->callbacks=scope;
}
void *wd_m_callback_find(const void *key) {
    WLocal *local=wt_current();
    for(WCallback *p=local?local->callbacks:0;p;p=p->previous) if(p->key==key) return p;
    return 0;
}
void wd_m_callback_pop(void *memory) {
    WLocal *local=wt_current();
    if(!local || local->callbacks!=memory) __builtin_trap();
    local->callbacks=((WCallback*)memory)->previous;
}
WInt wd_m_task_cancelled(void) {
    WLocal *local=wt_current();
    return local && local->task && __atomic_load_n(&local->task->cancelled,__ATOMIC_ACQUIRE);
}
void wd_m_task_checkpoint(void *ctx) {
    if(!wd_rt_unwinding(ctx) && wd_m_task_cancelled())
        fail(ctx,"R4022","task cancelled");
}
WInt wd_m_task_closed(void *value) { WTask *t=value; return !t || __atomic_load_n(&t->closed,__ATOMIC_ACQUIRE)!=0; }
void wd_m_task_check(void *ctx,void *value) { if(wd_m_task_closed(value)) fail(ctx,"R4022","task is closed"); }
/* A raw clone stack is guard page, stack, then the signal stack that reports its overflow. */
#define WT_STACK_SIZE (8388608+4096)
#define WT_SIGNAL_STACK 65536
static unsigned wt_worker(void *value) {
    WTask *t=value;
    __atomic_store_n(wt_ids+t->slot,wt_id(),__ATOMIC_RELEASE);
#if !defined(_WIN32) && WD_STACK_GUARD
    if(t->stack) wd_stack_guard_thread((unsigned char*)t->stack+WT_STACK_SIZE,WT_SIGNAL_STACK);
#endif
    if(t->io_work) {
        w_lock(&t->start_gate); w_unlock(&t->start_gate);
        t->result=t->io_work(t->context,t->io,t->payload);
        if(!wd_rt_unwinding(t->context)) wd_rt_context_site(t->context,&t->site_file,t->site_line,t->site_column);
        wd_m_stream_close(t->context,t->io,1);
        wd_m_release(t->io); t->io=0;
    } else t->result=t->work(t->context,t->payload);
    wd_m_release(t->payload); t->payload=0;
    if(t->local.callbacks) __builtin_trap();
    /* Unpublish thread-local state before its slot can be reused. */
    __atomic_store_n(wt_ids+t->slot,0,__ATOMIC_RELEASE);
    w_lock(&wt_registry_lock); wt_locals[t->slot]=0; --wt_active; w_unlock(&wt_registry_lock);
    __atomic_store_n(&t->completed,1,__ATOMIC_RELEASE);
#ifdef _WIN32
    if(!SetEvent(t->event)) __builtin_trap();
#else
    (void)d_sys(202,(WInt)(intptr_t)&t->completed,129,INT32_MAX,0,0,0);
#endif
    return 0;
}
#if !defined(_WIN32) && !defined(WOOD_NATIVE_HOSTED_THREADS)
static void *wt_hosted_worker(void *value) { (void)wt_worker(value); return 0; }
#endif
static void wt_join(WTask *t) {
    if(!t->handle) return;
#ifdef _WIN32
    if(WaitForSingleObject((void*)(intptr_t)t->handle,UINT32_MAX)!=0) __builtin_trap();
    CloseHandle((void*)(intptr_t)t->handle);
#elif defined(WOOD_NATIVE_HOSTED_THREADS)
    wd_test_thread_join(t->handle);
#else
    if(t->hosted_thread) {
        if(!pthread_join || pthread_join((WSize)t->handle,0)) __builtin_trap();
        t->handle=0; return;
    }
    int32_t tid;
    while((tid=__atomic_load_n(&t->tid,__ATOMIC_ACQUIRE))!=0) {
        /* CHILD_CLEARTID wakes a shared (not PRIVATE) futex. */
        WInt status=d_sys(202,(WInt)(intptr_t)&t->tid,0,tid,0,0,0);
        if(status<0 && status!=-4 && status!=-11) __builtin_trap();
    }
    wd_platform_free(t->stack,WT_STACK_SIZE+WT_SIGNAL_STACK); t->stack=0;
#endif
    t->handle=0;
}
void wd_m_task_close(void *value) {
    WTask *t=value; if(!t) return;
    int32_t expected=0;
    if(__atomic_compare_exchange_n(&t->closed,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        __atomic_store_n(&t->cancelled,1,__ATOMIC_RELEASE);
        wt_join(t);
#ifdef _WIN32
        if(t->event) { CloseHandle(t->event); t->event=0; }
#endif
        __atomic_store_n(&t->closed,2,__ATOMIC_RELEASE);
#ifndef _WIN32
        (void)d_sys(202,(WInt)(intptr_t)&t->closed,129,INT32_MAX,0,0,0);
#endif
    } else {
        while(__atomic_load_n(&t->closed,__ATOMIC_ACQUIRE)!=2) {
#ifdef _WIN32
            Sleep(1);
#else
            (void)d_sys(202,(WInt)(intptr_t)&t->closed,128,1,0,0,0);
#endif
        }
    }
}
static void wt_destroy(void *value) {
    WTask *t=value; wd_m_task_close(t);
#ifdef _WIN32
    if(t->event) CloseHandle(t->event);
#endif
    wd_m_release(t->payload); wd_m_release(t->result);
    if(t->context) wd_rt_context_clear(t->context);
    wd_m_release(t->io); wd_m_str_drop(&t->site_file);
}
static void *wt_spawn(void *ctx,void *work,void *payload,void *connection,const WStr *file,WInt line,WInt column,int io) {
    if(wd_rt_unwinding(ctx)) return 0;
    j_validate(ctx,payload,0); if(wd_rt_unwinding(ctx)) return 0;
#ifndef _WIN32
    if(!wd_rt_context_size || !wd_rt_context_forward || !wd_rt_context_clear) { fail(ctx,"R4022","tasks require the current ABI-4 LLVM core"); return 0; }
#endif
    WInt context_size=wd_rt_context_size(),bytes;
    if(context_size<=0 || context_size>65536 || !work) { fail(ctx,"R4022","invalid native task entry or context"); return 0; }
#ifndef _WIN32
    if(io && !wd_rt_context_site) { fail(ctx,"R4022","socket tasks require the current ABI-4 LLVM core"); return 0; }
#endif
    if(!sum(ctx,sizeof(WTask),context_size,&bytes)) return 0;
    WTask *t=(WTask*)allocate(ctx,bytes,wt_destroy); if(!t) return 0;
    zero_bytes(t+1,(WSize)context_size); zero_bytes((unsigned char*)t+sizeof(WOwner),sizeof(WTask)-sizeof(WOwner));
    t->context=t+1; t->work=(WTaskWork)work; t->payload=payload; wd_m_retain(payload); t->local.task=t; t->slot=-1;
    if(io) {
        t->io=stream_transfer_prepare(ctx,connection);
        if(!t->io) { wd_m_release(t); return 0; }
        t->io_work=(WTaskIoWork)work;
        t->site_file=*file; wd_m_str_retain(&t->site_file);
        t->site_line=line; t->site_column=column;
    }
    wd_m_threads_init();
    w_lock(&wt_registry_lock);
    if(wt_active<256) for(int i=0;i<256;++i) if(!wt_locals[i]) { t->slot=i; wt_locals[i]=&t->local; ++wt_active; break; }
    w_unlock(&wt_registry_lock);
    if(t->slot<0) { fail(ctx,"R4022","at most 256 native tasks may run at once"); wd_m_release(t); return 0; }
    if(io) w_lock(&t->start_gate);
    /* From here on, owners may be shared between threads. */
    __atomic_store_n(&shared_owners,1,__ATOMIC_SEQ_CST);
#ifdef _WIN32
    t->event=CreateEventW(0,1,0,0);
    if(t->event) t->handle=(WInt)(intptr_t)CreateThread(0,8388608,wt_worker,t,0x10000,0);
#elif defined(WOOD_NATIVE_HOSTED_THREADS)
    t->handle=wd_test_thread_start(wt_worker,t);
#else
    /* Optional system libraries need real ELF TLS and libc thread startup.
       Raw clone workers must never call a TLS-using system C library. */
    if(pthread_create && pthread_join) {
        WSize handle=0;
        if(!pthread_create(&handle,0,wt_hosted_worker,t)) { t->handle=(WInt)handle; t->hosted_thread=1; }
    } else {
    t->stack=wd_platform_alloc(WT_STACK_SIZE+WT_SIGNAL_STACK);
    if(t->stack) {
        if(d_sys(10,(WInt)(intptr_t)t->stack,4096,0,0,0,0)==0) {
            WInt id=wd_native_clone(wt_worker,t,(unsigned char*)t->stack+WT_STACK_SIZE,&t->tid);
            if(id>0) t->handle=id;
        }
        if(!t->handle) { wd_platform_free(t->stack,WT_STACK_SIZE+WT_SIGNAL_STACK); t->stack=0; }
    }
    }
#endif
    if(!t->handle) {
        if(io) w_unlock(&t->start_gate);
        w_lock(&wt_registry_lock); wt_locals[t->slot]=0; --wt_active; w_unlock(&wt_registry_lock);
        fail(ctx,"R4022","cannot start native task"); wd_m_release(t); return 0;
    }
    if(io) {
        stream_transfer_commit(connection,t->io);
        w_unlock(&t->start_gate);
    }
    return t;
}
void *wd_m_task_spawn(void *ctx,void *work,void *payload) { return wt_spawn(ctx,work,payload,0,0,0,0,0); }
void *wd_m_task_spawn_io(void *ctx,void *work,void *connection,void *payload,const WStr *file,WInt line,WInt column) {
    return wt_spawn(ctx,work,payload,connection,file,line,column,1);
}
WInt wd_m_task_done(void *ctx,void *value) {
    wd_m_task_check(ctx,value); if(wd_rt_unwinding(ctx)) return 0;
    return __atomic_load_n(&((WTask*)value)->completed,__ATOMIC_ACQUIRE);
}
void wd_m_task_cancel(void *ctx,void *value) {
    wd_m_task_check(ctx,value); if(!wd_rt_unwinding(ctx)) __atomic_store_n(&((WTask*)value)->cancelled,1,__ATOMIC_RELEASE);
}
void *wd_m_task_wait(void *ctx,void *value,WInt timeout) {
    if(timeout<1 || timeout>86400000) { fail(ctx,"R4022","task timeout must be in 1..86400000 milliseconds"); return 0; }
    wd_m_task_check(ctx,value); if(wd_rt_unwinding(ctx)) return 0;
    WTask *t=value;
    if(!__atomic_load_n(&t->completed,__ATOMIC_ACQUIRE)) {
        WInt now=wd_m_time_ns(ctx),duration=timeout*1000000;
        WInt deadline=now>INT64_MAX-duration?INT64_MAX:now+duration;
        while(!wd_rt_unwinding(ctx) && !__atomic_load_n(&t->completed,__ATOMIC_ACQUIRE)) {
            now=wd_m_time_ns(ctx); if(wd_rt_unwinding(ctx)) break;
            if(now>=deadline) { fail(ctx,"R4022","task wait timed out; task is still running"); break; }
            WInt remaining=deadline-now;
#ifdef _WIN32
            unsigned status=WaitForSingleObject(t->event,(unsigned)((remaining+999999)/1000000));
            if(status!=0 && status!=258) { fail(ctx,"R4022","cannot wait for native task"); break; }
#else
            WInt wait[2]={remaining/1000000000,remaining%1000000000};
            WInt status=d_sys(202,(WInt)(intptr_t)&t->completed,128,0,(WInt)(intptr_t)wait,0,0);
            if(status<0 && status!=-4 && status!=-11 && status!=-110) { fail(ctx,"R4022","cannot wait for native task"); break; }
#endif
        }
    }
    if(wd_rt_unwinding(ctx)) return 0;
    wd_rt_context_forward(ctx,t->context);
    if(wd_rt_unwinding(ctx)) return 0;
    wd_m_retain(t->result); return t->result;
}

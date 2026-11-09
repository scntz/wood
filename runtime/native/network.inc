/* Qualified x86-64 socket ABI. Optional initialized OS libraries provide DNS;
   ordinary compiler/application outputs keep their freestanding entry. Every
   socket is owned immediately and closed on setup, race, timeout or OOM failure. */
#ifdef _WIN32
typedef WSize NSocket;
typedef int NResult;
typedef int NCount;
typedef int NLength;
typedef struct NInfo {
    int flags,family,type,protocol;
    WSize length;
    char *canonical;
    void *address;
    struct NInfo *next;
} NInfo;
enum { N_INET6=23,N_NUMERICSERV=8,N_SOCKET=65535,N_ERROR=0x1007,N_REUSE=-5,N_IPV6=41,N_V6ONLY=27,N_SEND=1 };
#else
typedef int NSocket;
typedef WInt NResult;
typedef WSize NCount;
typedef unsigned NLength;
typedef struct NInfo {
    int flags,family,type,protocol;
    unsigned length;
    void *address;
    char *canonical;
    struct NInfo *next;
} NInfo;
enum { N_INET6=10,N_NUMERICSERV=1024,N_SOCKET=1,N_ERROR=4,N_REUSE=2,N_IPV6=41,N_V6ONLY=26,N_SEND=1 };
#endif
_Static_assert(sizeof(NInfo)==48,"qualified addrinfo layout");
typedef union { WSize align[16]; unsigned char bytes[128]; } NAddress;
typedef struct { unsigned count; WSize sockets[64]; } NSet;
typedef struct { int seconds,microseconds; } NTime;
typedef struct { int fd; short events,returned; } NPoll;
_Static_assert(sizeof(NPoll)==8 && offsetof(NSet,sockets)==8,"qualified readiness ABI");
#ifndef WOOD_NATIVE_NETWORK_ABI_ONLY
typedef struct {
    void *library;
#define N_API(name,result,...) result (*name)(__VA_ARGS__)
    N_API(socket,NSocket,int,int,int);
    N_API(connect,int,NSocket,const void *,NLength);
    N_API(bind,int,NSocket,const void *,NLength);
    N_API(listen,int,NSocket,int);
    N_API(accept,NSocket,NSocket,void *,NLength *);
    N_API(getsockname,int,NSocket,void *,NLength *);
    N_API(getsockopt,int,NSocket,int,int,void *,NLength *);
    N_API(setsockopt,int,NSocket,int,int,const void *,NLength);
    N_API(shutdown,int,NSocket,int);
    N_API(recv,NResult,NSocket,void *,NCount,int);
    N_API(send,NResult,NSocket,const void *,NCount,int);
    N_API(getaddrinfo,int,const char *,const char *,const NInfo *,NInfo **);
    N_API(freeaddrinfo,void,NInfo *);
    N_API(inet_pton,int,int,const char *,void *);
#ifdef _WIN32
    N_API(close,int,NSocket);
    N_API(error,int,void);
    N_API(startup,int,unsigned short,void *);
    N_API(ioctl,int,NSocket,uint32_t,uint32_t *);
    N_API(select,int,int,NSet *,NSet *,NSet *,const NTime *);
#else
    N_API(close,int,NSocket);
    N_API(error,int *,void);
    N_API(fcntl,int,int,int,...);
    N_API(poll,int,NPoll *,WSize,int);
#endif
#undef N_API
} NApi;
static NApi n_api;
static WLock n_lock;
static int n_loaded;
static int n_load(void *ctx) {
    if(__atomic_load_n(&n_loaded,__ATOMIC_ACQUIRE)) return 1;
    w_lock(&n_lock);
    if(!n_loaded) {
        NApi a={0};
#ifdef _WIN32
        const unsigned short name[]={'w','s','2','_','3','2','.','d','l','l',0};
        a.library=LoadLibraryExW(name,0,0x800); /* System32 only; no user search path. */
#else
        if(dlopen && dlsym && dlclose && pthread_create && pthread_join) a.library=dlopen("libc.so.6",2);
#endif
        if(!a.library) fail(ctx,"R4017","cannot initialize native networking");
        else {
            int valid=1;
#define N_LOAD(field,symbol) do { void *p=optional_symbol(a.library,symbol); copy_bytes(&a.field,&p,sizeof p); if(!p) valid=0; } while(0)
            N_LOAD(socket,"socket"); N_LOAD(connect,"connect"); N_LOAD(bind,"bind"); N_LOAD(listen,"listen");
            N_LOAD(accept,"accept"); N_LOAD(getsockname,"getsockname"); N_LOAD(getsockopt,"getsockopt");
            N_LOAD(setsockopt,"setsockopt"); N_LOAD(shutdown,"shutdown"); N_LOAD(recv,"recv"); N_LOAD(send,"send");
            N_LOAD(getaddrinfo,"getaddrinfo"); N_LOAD(freeaddrinfo,"freeaddrinfo"); N_LOAD(inet_pton,"inet_pton");
#ifdef _WIN32
            N_LOAD(close,"closesocket"); N_LOAD(error,"WSAGetLastError"); N_LOAD(startup,"WSAStartup");
            N_LOAD(ioctl,"ioctlsocket"); N_LOAD(select,"select");
#else
            N_LOAD(close,"close"); N_LOAD(error,"__errno_location"); N_LOAD(fcntl,"fcntl"); N_LOAD(poll,"poll");
#endif
#undef N_LOAD
            if(!valid) fail(ctx,"R4017","native networking is missing a required OS ABI symbol");
#ifdef _WIN32
            else {
                union { WSize align; unsigned char bytes[512]; } state;
                if(a.startup(0x202,&state)) fail(ctx,"R4017","cannot initialize native networking");
            }
#endif
            if(!wd_rt_unwinding(ctx)) { n_api=a; __atomic_store_n(&n_loaded,1,__ATOMIC_RELEASE); }
            else optional_close(a.library);
        }
    }
    int result=n_loaded; w_unlock(&n_lock); return result;
}
static int n_errno(void) {
#ifdef _WIN32
    return n_api.error();
#else
    return *n_api.error();
#endif
}
static int n_interrupted(int code) {
#ifdef _WIN32
    return code==10004;
#else
    return code==4;
#endif
}
static int n_pending(int code) {
#ifdef _WIN32
    return code==10035 || code==10036 || code==10037;
#else
    return code==11 || code==115;
#endif
}
static int n_close(WInt fd) { return n_api.close((NSocket)fd)?n_errno():0; }
static void n_error(void *ctx,const char *action,int number) {
    if(wd_rt_unwinding(ctx)) return;
    WStr digits={0},message={0}; wd_m_int_str(ctx,&digits,number);
    JBuffer b={ctx,0}; jb_word(&b,action); jb_word(&b," failed (OS error "); jb_add(&b,digits.data,digits.size); jb_word(&b,")"); jb_finish(&b,&message);
    if(!wd_rt_unwinding(ctx)) wd_rt_error_value(ctx,"R4017",5,&message);
    wd_m_str_drop(&digits); wd_m_str_drop(&message);
}
static int n_timeout(void *ctx,WInt milliseconds) {
    if(milliseconds>=1 && milliseconds<=86400000) return 1;
    fail(ctx,"R4017","socket timeout must be in 1..86400000 milliseconds"); return 0;
}
static WInt n_until(void *ctx,WInt timeout) {
    WInt now=wd_m_time_ns(ctx),duration=timeout*1000000;
    return now>INT64_MAX-duration?INT64_MAX:now+duration;
}
static int n_check(void *ctx,WInt until) {
    if(wd_rt_unwinding(ctx)) return 0;
    if(wd_m_task_cancelled()) { fail(ctx,"R4022","socket operation cancelled"); return 0; }
    if(wd_m_time_ns(ctx)>=until && !wd_rt_unwinding(ctx)) fail(ctx,"R4018","socket operation timed out");
    return !wd_rt_unwinding(ctx);
}
static int n_configure(void *ctx,WInt fd) {
#ifdef _WIN32
    uint32_t on=1;
    if(n_api.ioctl((NSocket)fd,0x8004667e,&on)) { n_error(ctx,"nonblocking socket setup",n_errno()); return 0; }
    if(!SetHandleInformation((void*)(intptr_t)fd,1,0)) { n_error(ctx,"noninheritable socket setup",(int)GetLastError()); return 0; }
#else
    int flags=n_api.fcntl((int)fd,3),descriptor=n_api.fcntl((int)fd,1);
    if(flags<0 || descriptor<0 || n_api.fcntl((int)fd,4,flags|2048)<0 || n_api.fcntl((int)fd,2,descriptor|1)<0) {
        n_error(ctx,"socket setup",n_errno()); return 0;
    }
#endif
    return 1;
}
/* At most 64 addresses/sockets. Poll in bounded slices for cooperative task
   cancellation; a stagger boundary is not an operation timeout. */
static int n_ready(void *ctx,const WInt *fds,int count,int writing,WInt wake,unsigned char *ready,const char *action) {
    zero_bytes(ready,(WSize)count);
    WInt now=wd_m_time_ns(ctx); if(wd_rt_unwinding(ctx)) return -1;
    if(now>=wake) return 0;
    WInt ms=(wake-now+999999)/1000000; if(ms>50) ms=50;
#ifdef _WIN32
    NSet events={0},errors={0};
    for(int i=0;i<count;++i) if(fds[i]!=-1) { events.sockets[events.count++]=(WSize)fds[i]; errors.sockets[errors.count++]=(WSize)fds[i]; }
    NTime time={(int)(ms/1000),(int)(ms%1000)*1000};
    int result=n_api.select(0,writing?0:&events,writing?&events:0,&errors,&time);
    if(result>0) for(int i=0;i<count;++i) if(fds[i]!=-1) {
        for(unsigned j=0;j<events.count;++j) if(events.sockets[j]==(WSize)fds[i]) ready[i]=1;
        for(unsigned j=0;j<errors.count;++j) if(errors.sockets[j]==(WSize)fds[i]) ready[i]=1;
    }
#else
    NPoll entries[64];
    for(int i=0;i<count;++i) entries[i]=(NPoll){(int)fds[i],(short)(writing?4:1),0};
    int result=n_api.poll(entries,(WSize)count,(int)ms);
    if(result>0) for(int i=0;i<count;++i) ready[i]=entries[i].returned!=0;
#endif
    if(result<0 && !n_interrupted(n_errno())) { n_error(ctx,action,n_errno()); return -1; }
    return result>0;
}
static int n_wait(void *ctx,WInt fd,int writing,WInt until) {
    while(n_check(ctx,until)) {
        unsigned char ready=0;
        if(n_ready(ctx,&fd,1,writing,until,&ready,"socket wait")<0) return 0;
        if(ready) return 1;
    }
    return 0;
}
static WStream *n_stream(void *ctx,WInt fd,int kind,WInt timeout) {
    WStream *s=(WStream*)allocate(ctx,sizeof(WStream),stream_destroy);
    if(!s) { if(fd!=-1) (void)n_close(fd); return 0; }
    zero_bytes((unsigned char*)s+sizeof(WOwner),sizeof(WStream)-sizeof(WOwner));
    s->handle=fd; s->kind=kind; s->timeout=timeout; s->readable=s->writable=kind==1;
    return s;
}
static WInt n_read(void *ctx,WStream *s,void *bytes,WInt count,WInt until) {
    if(s->secure) return t_read(ctx,s,bytes,count,until);
    while(n_check(ctx,until)) {
        NResult received=n_api.recv((NSocket)s->handle,bytes,(NCount)count,0);
        if(received>=0) return (WInt)received;
        int code=n_errno(); if(n_interrupted(code)) continue;
        if(!n_pending(code)) { n_error(ctx,"socket read",code); break; }
        if(!n_wait(ctx,s->handle,0,until)) break;
    }
    return 0;
}
static WInt n_write(void *ctx,WStream *s,const void *bytes,WInt count,WInt until) {
    if(s->secure) return t_write(ctx,s,bytes,count,until);
    while(n_check(ctx,until)) {
#ifdef _WIN32
        int flags=0;
#else
        int flags=0x4000; /* Linux MSG_NOSIGNAL, without changing signal policy. */
#endif
        NResult sent=n_api.send((NSocket)s->handle,bytes,(NCount)count,flags);
        if(sent>0) return (WInt)sent;
        int code=n_errno(); if(sent<0 && n_interrupted(code)) continue;
        if(sent>=0 || !n_pending(code)) { n_error(ctx,"socket write",code); break; }
        if(!n_wait(ctx,s->handle,1,until)) break;
    }
    return 0;
}
void wd_m_stream_timeout(void *ctx,void *value,WInt timeout) {
    if(!n_timeout(ctx,timeout)) return;
    WStream *s=stream_state(ctx,value); if(!s) return;
    if(!s->kind) { fail(ctx,"R4016","timeouts require a socket stream"); return; }
    s->timeout=timeout;
}
void *wd_m_tcp_connect(void *ctx,const WStr *host,WInt port,WInt timeout) {
    if(!n_timeout(ctx,timeout)) return 0;
    if(!host->size || wd_m_find(host,&(WStr){(unsigned char*)"\0",1,0})>=0 || port<1 || port>65535) {
        fail(ctx,"R4017","connect requires a nonempty host and port in 1..65535"); return 0;
    }
    if(!n_load(ctx)) return 0;
    WStr terminated={0},service={0}; p_copy(ctx,&terminated,host->data,host->size); wd_m_int_str(ctx,&service,port);
    NInfo hints={0},*addresses=0; hints.family=0; hints.type=1; hints.protocol=6; hints.flags=N_NUMERICSERV;
    int lookup=0;
    if(!wd_rt_unwinding(ctx)) lookup=n_api.getaddrinfo((char*)terminated.data,(char*)service.data,&hints,&addresses);
    wd_m_str_drop(&terminated); wd_m_str_drop(&service);
    if(lookup) n_error(ctx,"host resolution",lookup);
    WInt handles[64],winner=-1; NInfo *preferred[64],*alternate[64],*ordered[64];
    for(int i=0;i<64;++i) handles[i]=-1;
    int pn=0,an=0,count=0,first=addresses?addresses->family:N_INET6;
    for(NInfo *p=addresses;p;p=p->next) {
        if(p->family==first) { if(pn<64) preferred[pn++]=p; }
        else if(an<64) alternate[an++]=p;
    }
    for(int i=0;count<64 && (i<pn || i<an);++i) {
        if(i<pn) ordered[count++]=preferred[i];
        if(i<an && count<64) ordered[count++]=alternate[i];
    }
    WInt until=n_until(ctx,timeout),next_start=0;
    int next=0,active=0,last=0;
    while(!wd_rt_unwinding(ctx) && (next<count || active)) {
        if(!n_check(ctx,until)) break;
        WInt now=wd_m_time_ns(ctx);
        if(next<count && (!active || now>=next_start)) {
            int index=next++; NInfo *address=ordered[index];
            WInt fd=(WInt)n_api.socket(address->family,1,6);
            if(fd==-1) { last=n_errno(); continue; }
            if(!n_configure(ctx,fd)) { (void)n_close(fd); break; }
            if(!n_api.connect((NSocket)fd,address->address,(NLength)address->length)) { winner=fd; break; }
            last=n_errno();
            if(!n_pending(last) && !n_interrupted(last)) { (void)n_close(fd); continue; }
            handles[index]=fd; ++active;
            next_start=now>INT64_MAX-250000000?INT64_MAX:now+250000000;
        }
        if(!active) continue;
        unsigned char ready[64]; WInt wake=next<count && next_start<until?next_start:until;
        if(n_ready(ctx,handles,count,1,wake,ready,"connection wait")<0) break;
        for(int i=0;i<count;++i) if(ready[i] && handles[i]!=-1) {
            last=0; NLength length=sizeof(last);
            if(n_api.getsockopt((NSocket)handles[i],N_SOCKET,N_ERROR,&last,&length)) last=n_errno();
            if(!last) { winner=handles[i]; handles[i]=-1; break; }
            (void)n_close(handles[i]); handles[i]=-1; --active; next_start=wd_m_time_ns(ctx);
        }
        if(winner!=-1) break;
    }
    for(int i=0;i<count;++i) if(handles[i]!=-1) (void)n_close(handles[i]);
    if(addresses) n_api.freeaddrinfo(addresses);
    if(wd_rt_unwinding(ctx)) { if(winner!=-1) (void)n_close(winner); return 0; }
    if(winner==-1) { n_error(ctx,"connect",last); return 0; }
    return n_stream(ctx,winner,1,timeout);
}
void *wd_m_tcp_listen(void *ctx,const WStr *host,WInt port,WInt timeout) {
    if(!n_timeout(ctx,timeout)) return 0;
    if(!host->size || wd_m_find(host,&(WStr){(unsigned char*)"\0",1,0})>=0 || port<0 || port>65535) {
        fail(ctx,"R4017","listen requires an explicit numeric address and port in 0..65535"); return 0;
    }
    if(!n_load(ctx)) return 0;
    WStr terminated={0}; p_copy(ctx,&terminated,host->data,host->size); if(wd_rt_unwinding(ctx)) return 0;
    NAddress address={0}; int family=2; NLength length=16;
    if(n_api.inet_pton(2,(char*)terminated.data,address.bytes+4)!=1) {
        family=N_INET6; length=28;
        if(n_api.inet_pton(family,(char*)terminated.data,address.bytes+8)!=1) fail(ctx,"R4017","listen address must be numeric IPv4 or IPv6");
    }
    wd_m_str_drop(&terminated); if(wd_rt_unwinding(ctx)) return 0;
    uint16_t f=(uint16_t)family; copy_bytes(address.bytes,&f,2);
    address.bytes[2]=(unsigned char)(port>>8); address.bytes[3]=(unsigned char)port;
    WInt fd=(WInt)n_api.socket(family,1,6); if(fd==-1) { n_error(ctx,"socket creation",n_errno()); return 0; }
    if(n_configure(ctx,fd)) {
        int on=1;
        if(n_api.setsockopt((NSocket)fd,N_SOCKET,N_REUSE,&on,sizeof(on))) n_error(ctx,"listen address setup",n_errno());
        else if(family==N_INET6 && n_api.setsockopt((NSocket)fd,N_IPV6,N_V6ONLY,&on,sizeof(on))) n_error(ctx,"IPv6-only listen setup",n_errno());
        else if(n_api.bind((NSocket)fd,&address,length) || n_api.listen((NSocket)fd,INT32_MAX)) n_error(ctx,"listen",n_errno());
    }
    if(wd_rt_unwinding(ctx)) { (void)n_close(fd); return 0; }
    return n_stream(ctx,fd,2,timeout);
}
void *wd_m_tcp_accept(void *ctx,void *value) {
    WStream *source=stream_state(ctx,value); if(!source) return 0;
    if(source->kind!=2) { fail(ctx,"R4017","accept requires a listening socket"); return 0; }
    WInt until=n_until(ctx,source->timeout);
    while(n_check(ctx,until)) {
        WInt fd=(WInt)n_api.accept((NSocket)source->handle,0,0);
        if(fd!=-1) {
            if(!n_configure(ctx,fd)) { (void)n_close(fd); return 0; }
            return n_stream(ctx,fd,1,source->timeout);
        }
        int code=n_errno(); if(n_interrupted(code)) continue;
        if(!n_pending(code)) { n_error(ctx,"accept",code); break; }
        if(!n_wait(ctx,source->handle,0,until)) break;
    }
    return 0;
}
WInt wd_m_tcp_port(void *ctx,void *value) {
    WStream *s=stream_state(ctx,value); if(!s) return 0;
    if(!s->kind) { fail(ctx,"R4017","port requires a socket stream"); return 0; }
    NAddress address={0}; NLength length=sizeof(address);
    if(n_api.getsockname((NSocket)s->handle,&address,&length)) { n_error(ctx,"local address",n_errno()); return 0; }
    return ((WInt)address.bytes[2]<<8)|address.bytes[3];
}
void wd_m_tcp_shutdown_write(void *ctx,void *value) {
    WStream *s=stream_state(ctx,value); if(!s) return;
    if(s->kind!=1) { fail(ctx,"R4017","shutdown requires a connected socket"); return; }
    if(!s->writable) return;
    if(s->secure) { t_shutdown(ctx,s,n_until(ctx,s->timeout)); if(wd_rt_unwinding(ctx)) return; }
    if(n_api.shutdown((NSocket)s->handle,N_SEND)) { n_error(ctx,"write shutdown",n_errno()); return; }
    s->writable=0;
}
#endif

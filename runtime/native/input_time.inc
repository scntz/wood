/* Hosted OS services without libc/CRT. Public clocks share a process-local
   epoch; private driver deadlines keep their independent clock domain. */
static unsigned char input_bytes[8192];
static WLock input_lock,time_lock;
static WInt input_at,input_size;
static int input_end,input_failed,input_look=-2;
#ifdef _WIN32
static int input_raw_look=-2;
WD_DLL int QueryPerformanceCounter(WInt*);
WD_DLL int QueryPerformanceFrequency(WInt*);
#endif
static int input_raw(void *ctx) {
#ifdef _WIN32
    if(input_raw_look!=-2) { int c=input_raw_look; input_raw_look=-2; return c; }
#endif
    if(input_at==input_size) {
        if(input_end || wd_rt_unwinding(ctx)) return -1;
        WInt count;
#ifdef _WIN32
        void *handle=GetStdHandle((unsigned)-10); unsigned n=0;
        if(!ReadFile(handle,input_bytes,sizeof input_bytes,&n,0)) {
            unsigned error=GetLastError();
            if(error==109 || error==38) count=0;
            else { input_failed=1; fail(ctx,"R4008","cannot read standard input"); return -1; }
        } else count=n;
#else
        do { count=wd_file_read(0,input_bytes,sizeof input_bytes); } while(count==-4);
        if(count<0) { input_failed=1; fail(ctx,"R4008","cannot read standard input"); return -1; }
#endif
        if(count>(WInt)sizeof input_bytes) { input_failed=1; fail(ctx,"R4008","cannot read standard input"); return -1; }
        input_at=0; input_size=count;
        if(!count) { input_end=1; return -1; }
    }
    return input_bytes[input_at++];
}
static int input_next(void *ctx) {
    if(wd_rt_unwinding(ctx)) return -1;
    int c=input_raw(ctx);
#ifdef _WIN32
    /* Match the qualified MSVC text-mode stdin, including CRLF and Ctrl-Z.
       read_line subsequently strips one trailing CR, as the reference does. */
    if(c==26) { input_end=1; input_at=input_size; return -1; }
    if(c==13) {
        int next=input_raw(ctx);
        if(next==10) return 10;
        input_raw_look=next;
    }
#endif
    return c;
}
static WInt input_eof(void *ctx) {
    if(input_failed) { fail(ctx,"R4008","cannot read standard input"); return 1; }
    if(input_look==-2) input_look=input_next(ctx);
    return input_look<0;
}
static void input_line(void *ctx,WStr *out) {
    JBuffer buffer={ctx,0};
    if(input_failed) fail(ctx,"R4008","cannot read standard input");
    while(!wd_rt_unwinding(ctx)) {
        int c=input_look==-2?input_next(ctx):input_look; input_look=-2;
        if(c<0) { input_look=-1; break; }
        if(c==10) break;
        jb_byte(&buffer,(unsigned char)c);
    }
    if(buffer.seq && buffer.seq->length && data(buffer.seq)[buffer.seq->length-1]==13) --buffer.seq->length;
    jb_finish(&buffer,out);
}
WInt wd_m_eof(void *ctx) {
    w_lock(&input_lock); WInt result=input_eof(ctx); w_unlock(&input_lock); return result;
}
void wd_m_read_line(void *ctx,WStr *out) {
    w_lock(&input_lock); input_line(ctx,out); w_unlock(&input_lock);
}

static int time_started;
static WInt time_origin[2];
#ifdef _WIN32
static WInt time_frequency;
#endif
static int time_read(void *ctx,WInt value[2]) {
#ifdef _WIN32
    value[1]=0;
    if(QueryPerformanceCounter(value) && value[0]>=0) return 1;
#else
    if(p_sys(228,1,(WInt)(intptr_t)value,0,0)==0 && value[0]>=0 && value[1]>=0 && value[1]<1000000000) return 1;
#endif
    fail(ctx,"R4015","cannot read monotonic clock"); return 0;
}
/* Exact floor(remainder*1e9/frequency), including frequencies near INT64_MAX.
   The common direct product fits; modular doubling covers the full range
   without compiler-rt 128-bit division helpers. */
WInt wd_m_time_fraction(WInt remainder,WInt frequency) {
    uint64_t r=(uint64_t)remainder,f=(uint64_t)frequency;
    if(r<=UINT64_MAX/1000000000) return (WInt)(r*1000000000/f);
    uint64_t part=0,result=0;
    for(int bit=29;bit>=0;--bit) {
        part*=2; result*=2;
        if(part>=f) { part-=f; ++result; }
        if((1000000000u>>bit)&1u) {
            part+=r;
            if(part>=f) { part-=f; ++result; }
        }
    }
    return (WInt)result;
}
static int time_elapsed_locked(void *ctx,WInt *seconds,WInt *fraction) {
    if(wd_rt_unwinding(ctx)) return 0;
    WInt now[2]; if(!time_read(ctx,now)) return 0;
    if(!time_started) {
#ifdef _WIN32
        if(!QueryPerformanceFrequency(&time_frequency) || time_frequency<=0) {
            fail(ctx,"R4015","cannot read monotonic clock frequency"); return 0;
        }
#endif
        time_origin[0]=now[0]; time_origin[1]=now[1]; time_started=1;
    }
#ifdef _WIN32
    if(now[0]<time_origin[0]) { fail(ctx,"R4015","monotonic clock moved backwards"); return 0; }
    WInt ticks=now[0]-time_origin[0];
    *seconds=ticks/time_frequency; *fraction=wd_m_time_fraction(ticks%time_frequency,time_frequency);
#else
    *seconds=now[0]-time_origin[0]; *fraction=now[1]-time_origin[1];
    if(*fraction<0) { --*seconds; *fraction+=1000000000; }
    if(*seconds<0) { fail(ctx,"R4015","monotonic clock moved backwards"); return 0; }
#endif
    return 1;
}
static int time_elapsed(void *ctx,WInt *seconds,WInt *fraction) {
    w_lock(&time_lock); int ok=time_elapsed_locked(ctx,seconds,fraction); w_unlock(&time_lock); return ok;
}
WInt wd_m_time_ns(void *ctx) {
    WInt seconds,fraction;
    if(!time_elapsed(ctx,&seconds,&fraction)) return 0;
    if(seconds>INT64_MAX/1000000000 || (seconds==INT64_MAX/1000000000 && fraction>INT64_MAX%1000000000)) {
        fail(ctx,"R4015","monotonic counter exceeds the int64 nanosecond range"); return 0;
    }
    return seconds*1000000000+fraction;
}
double wd_m_time_seconds(void *ctx) {
    WInt seconds,fraction;
    if(!time_elapsed(ctx,&seconds,&fraction)) return 0;
    return (double)seconds+(double)fraction/1000000000.0;
}
void wd_m_time_sleep(void *ctx,double seconds) {
    if(wd_rt_unwinding(ctx)) return;
    if(!(seconds>=0.0 && seconds<=86400.0)) { fail(ctx,"R4015","sleep requires finite seconds in 0..86400"); return; }
    double scaled=seconds*1000000000.0;
    WInt duration=(WInt)scaled; if((double)duration<scaled) ++duration;
    if(!duration) return;
#ifdef _WIN32
    WInt start[2],now[2],frequency;
    if(!time_read(ctx,start)) return;
    if(!QueryPerformanceFrequency(&frequency) || frequency<=0) { fail(ctx,"R4015","cannot read monotonic clock frequency"); return; }
    WInt remaining=duration;
    do {
        Sleep((unsigned)((remaining+999999)/1000000));
        if(!time_read(ctx,now)) return;
        if(now[0]<start[0]) { fail(ctx,"R4015","monotonic clock moved backwards"); return; }
        WInt ticks=now[0]-start[0],whole=ticks/frequency;
        if(whole>duration/1000000000) return;
        WInt elapsed=whole*1000000000+wd_m_time_fraction(ticks%frequency,frequency);
        if(elapsed>=duration) return;
        remaining=duration-elapsed;
    } while(remaining>0);
#else
    WInt request[2]={duration/1000000000,duration%1000000000},left[2];
    for(;;) {
        WInt status=p_sys(35,(WInt)(intptr_t)request,(WInt)(intptr_t)left,0,0);
        if(!status) return;
        if(status!=-4) { fail(ctx,"R4015","cannot sleep"); return; }
        request[0]=left[0]; request[1]=left[1];
    }
#endif
}

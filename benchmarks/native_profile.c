/* Included only in a private, instrumented managed.c made by prepare_native_profile.cmake.
   Never part of a normal runtime. Counters preserve allocation size/alignment and
   count requested bytes, not OS page/heap overhead or process RSS. No libc. */
void *wd_profile_platform_alloc(WSize);
void wd_profile_platform_free(void *, WSize);
static WSize profile_enabled=1,profile_report_exit=1;
static WSize profile_allocs,profile_bytes,profile_live,profile_base,profile_peak;
static WSize profile_retains,profile_releases,profile_unique,profile_shared;
#ifdef WOOD_PROFILE_SITES
#include "native_allocation_sites.c"
#endif
static WSize profile_load(WSize *p) { return __atomic_load_n(p,__ATOMIC_RELAXED); }
static void profile_add(WSize *p,WSize n) { __atomic_fetch_add(p,n,__ATOMIC_RELAXED); }
void *wd_platform_alloc(WSize bytes) {
    void *value=wd_profile_platform_alloc(bytes);
    if(!value) return 0;
    WSize live=__atomic_add_fetch(&profile_live,bytes,__ATOMIC_RELAXED);
#ifdef WOOD_PROFILE_SITES
    site_allocated(value,bytes);
#endif
    if(profile_load(&profile_enabled)) {
        profile_add(&profile_allocs,1); profile_add(&profile_bytes,bytes);
        WSize peak=profile_load(&profile_peak);
        while(live>peak && !__atomic_compare_exchange_n(&profile_peak,&peak,live,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
    }
    return value;
}
void wd_platform_free(void *value,WSize bytes) {
#ifdef WOOD_PROFILE_SITES
    site_freed(value,bytes);
#endif
    if(value) {
        WSize before=__atomic_fetch_sub(&profile_live,bytes,__ATOMIC_RELAXED);
        if(before<bytes) __builtin_trap();
    }
    wd_profile_platform_free(value,bytes);
}
void wd_m_retain(void *value) {
    if(value && profile_load(&profile_enabled)) profile_add(&profile_retains,1);
    wd_profile_raw_retain(value);
}
void wd_m_release(void *value) {
    if(value && profile_load(&profile_enabled)) profile_add(&profile_releases,1);
    wd_profile_raw_release(value);
}
void wd_profile_seq_copy(WInt length,WInt managed,WInt unique) {
    if(managed && profile_load(&profile_enabled))
        profile_add(unique?&profile_unique:&profile_shared,(WSize)length);
}
/* Reset only with no concurrent measured workers. Existing input storage remains
   live; peak is the additional live-byte high-water mark above that baseline. */
void bench_reset(void) {
    profile_enabled=0; profile_report_exit=0;
    profile_allocs=profile_bytes=profile_retains=profile_releases=0;
    profile_unique=profile_shared=0;
    profile_base=profile_live; profile_peak=profile_live;
    profile_enabled=1;
}
WInt bench_stop(void) { profile_enabled=0; return (WInt)profile_allocs; }
WInt bench_bytes(void) { return (WInt)profile_bytes; }
WInt bench_peak(void) { return (WInt)(profile_peak-profile_base); }
WInt bench_retains(void) { return (WInt)profile_retains; }
WInt bench_releases(void) { return (WInt)profile_releases; }
WInt bench_unique(void) { return (WInt)profile_unique; }
WInt bench_shared(void) { return (WInt)profile_shared; }
static WSize profile_text(char *out,WSize at,const char *text) {
    for(WSize i=0;text[i];++i) out[at++]=text[i];
    return at;
}
static WSize profile_number(char *out,WSize at,WSize value) {
    char digits[20]; WSize size=0;
    do { digits[size++]=(char)('0'+value%10); value/=10; } while(value);
    while(size) out[at++]=digits[--size];
    return at;
}
void wd_profile_report(void) {
    if(!profile_report_exit) return;
    profile_enabled=0;
    char out[512]; WSize at=0;
    const char *names[]={"allocations","requested_bytes","peak_additional_bytes","retains","releases","unique_managed_slots","shared_managed_slots"};
    WSize values[]={profile_allocs,profile_bytes,profile_peak-profile_base,profile_retains,profile_releases,profile_unique,profile_shared};
    out[at++]='{';
    for(WSize i=0;i<7;++i) {
        if(i) out[at++]=',';
        out[at++]='"'; at=profile_text(out,at,names[i]); out[at++]='"'; out[at++]=':';
        at=profile_number(out,at,values[i]);
    }
    out[at++]='}'; out[at++]='\n';
    WInt channel=2;
#ifdef _WIN32
    channel=(WInt)(intptr_t)GetStdHandle((unsigned)-12);
#endif
    WSize sent=0;
    while(sent<at) { WInt n=wd_file_write(channel,out+sent,(WInt)(at-sent)); if(n<=0) break; sent+=(WSize)n; }
#ifdef WOOD_PROFILE_SITES
    site_report();
#endif
}

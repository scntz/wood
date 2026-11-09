/* Browser platform layer. Pointers stay 64-bit, so the qualified layouts and offsets of
   this runtime are unchanged; tools/playground lowers the linked module to 32-bit memory.
   The host offers one import, the Linux x86-64 system call numbers this runtime already
   uses. Anything it does not implement answers ENOSYS, and that is the whole sandbox. */
#if defined(__wasm__)
__attribute__((import_module("wood"),import_name("syscall")))
extern WInt wood_syscall(WInt,WInt,WInt,WInt,WInt,WInt,WInt);

enum { WASM_PAGE=65536 };
typedef struct WasmSpan { struct WasmSpan *next; WInt pages; } WasmSpan;
static WasmSpan *wasm_spans;

/* Page spans are reused first fit; a free span holds its own list node. */
void *wd_platform_alloc(WSize bytes) {
    if(bytes==0 || bytes>(WSize)INT64_MAX-WASM_PAGE) return 0;
    WInt pages=(WInt)(bytes+WASM_PAGE-1)/WASM_PAGE;
    for(WasmSpan **link=&wasm_spans;*link;link=&(*link)->next) {
        WasmSpan *span=*link;
        if(span->pages<pages) continue;
        if(span->pages>pages) {
            WasmSpan *rest=(WasmSpan *)((unsigned char *)span+pages*WASM_PAGE);
            rest->next=span->next; rest->pages=span->pages-pages; *link=rest;
        } else *link=span->next;
        return span;
    }
    size_t old=__builtin_wasm_memory_grow(0,(size_t)pages);
    if(old==(size_t)-1) return 0;
    return (void *)(old*WASM_PAGE);
}
void wd_platform_free(void *memory,WSize bytes) {
    if(!memory || bytes==0) return;
    WasmSpan *span=memory;
    span->pages=(WInt)((bytes+WASM_PAGE-1)/WASM_PAGE); span->next=wasm_spans; wasm_spans=span;
}

WInt wd_wasm_sys_write(WInt fd,const void *data,WInt size) __asm__("wd.sys_write");
WInt wd_wasm_sys_write(WInt fd,const void *data,WInt size) { return wood_syscall(1,fd,(WInt)(intptr_t)data,size,0,0,0); }
WInt wd_wasm_output_write(WInt fd,const void *data,WInt size) __asm__("wd.output_write");
WInt wd_wasm_output_write(WInt fd,const void *data,WInt size) { return wood_syscall(1,fd,(WInt)(intptr_t)data,size,0,0,0); }
void wd_wasm_exit(int32_t code) __asm__("wd.exit");
void wd_wasm_exit(int32_t code) { wood_syscall(231,code,0,0,0,0,0); __builtin_unreachable(); }

/* The only compiler-rt helpers the 64-bit overflow checks need, since nothing is linked but this runtime. */
typedef union { __int128 whole; struct { uint64_t low; uint64_t high; } part; } WasmWide;
__int128 __ashlti3(__int128 a,int count) {
    WasmWide x,out; x.whole=a;
    if(count>=64) { out.part.low=0; out.part.high=x.part.low<<(count-64); }
    else if(count==0) out=x;
    else { out.part.low=x.part.low<<count; out.part.high=(x.part.high<<count)|(x.part.low>>(64-count)); }
    return out.whole;
}
__int128 __multi3(__int128 a,__int128 b) {
    WasmWide x,y,out; x.whole=a; y.whole=b;
    uint64_t a0=x.part.low&0xffffffffu,a1=x.part.low>>32,b0=y.part.low&0xffffffffu,b1=y.part.low>>32;
    uint64_t low=a0*b0,middle=a1*b0+(low>>32),other=a0*b1+(middle&0xffffffffu);
    out.part.low=(other<<32)|(low&0xffffffffu);
    out.part.high=a1*b1+(middle>>32)+(other>>32)+x.part.high*y.part.low+x.part.low*y.part.high;
    return out.whole;
}

/* Same flags as linux-x86_64.ll. */
WInt wd_file_open(const void *path,WInt size,WInt mode) {
    WInt flags=mode==0?0:mode==1?577:mode==2?1089:193;
    return wood_syscall(257,-100,(WInt)(intptr_t)path,flags|524288,0666,size,0);
}
WInt wd_file_read(WInt fd,void *data,WInt size) { return wood_syscall(0,fd,(WInt)(intptr_t)data,size,0,0,0); }
WInt wd_file_write(WInt fd,const void *data,WInt size) { return wood_syscall(1,fd,(WInt)(intptr_t)data,size,0,0,0); }
WInt wd_file_close(WInt fd) { return wood_syscall(3,fd,0,0,0,0,0); }
WInt wd_file_seek(WInt fd,WInt offset,WInt origin) { return wood_syscall(8,fd,offset,origin,0,0,0); }
#endif

/* Independent registry/lifetime oracle for the optional compiler profiler. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/native/managed.h"
static void copy_bytes(void *to,const void *from,WSize count) { memcpy(to,from,(size_t)count); }
void wd_profile_location(void *ctx,WStr *file,WInt *line,WInt *column) {
    (void)ctx; *file=(WStr){(unsigned char*)"profile-fixture.wd",18,0}; *line=7; *column=11;
}
WInt wd_file_write(WInt channel,const void *text,WInt size) {
    assert(channel==2 && size>=0); return (WInt)fwrite(text,1,(size_t)size,stderr);
}
#define WD_PROFILE_ENTRIES 64u
#include "native_allocation_sites.c"
int main(int argc,char **argv) {
    if(argc>1) {
        if(!strcmp(argv[1],"table-full")) {
            for(unsigned i=0;i<WD_PROFILE_ENTRIES;++i)site_allocated((void*)(uintptr_t)(4096+i*16),8);
        }
        else if(!strcmp(argv[1],"wrong-size")) { site_allocated((void*)4096,32); site_freed((void*)4096,31); }
        else site_freed((void*)4096,32);
        return 2;
    }
    WSize state=12345,total=0,bytes=0;
    struct { void *pointer; WSize bytes; } live[24]={0};
    for(unsigned round=0;round<12000;++round) {
        state=state*UINT64_C(6364136223846793005)+1;
        unsigned index=(unsigned)(state>>32)%24;
        if(live[index].pointer) {
            site_freed(live[index].pointer,live[index].bytes); live[index].pointer=0;
        } else {
            void *pointer=(void*)(uintptr_t)(4096+index*16); WSize size=25+index;
            wd_profile_request((void*)1,"fixture","owner.c",3,24);
            site_allocated(pointer,size); wd_profile_request_end();
            live[index].pointer=pointer;live[index].bytes=size; ++total; bytes+=size;
        }
        WSize count=0,amount=0;
        for(unsigned i=0;i<24;++i) if(live[i].pointer){++count;amount+=live[i].bytes;}
        assert(site_live==count);
        WSize observed=0,found=0;
        for(unsigned i=0;i<WD_PROFILE_ENTRIES;++i) if(site_entries[i].pointer){++found;observed+=site_entries[i].bytes;}
        assert(found==count && observed==amount);
    }
    for(unsigned i=0;i<24;++i) if(live[i].pointer)site_freed(live[i].pointer,live[i].bytes);
    assert(!site_live && site_count==1);
    for(unsigned i=0;i<WD_PROFILE_SITES;++i) if(site_stats[i].function) {
        ProfileSite *s=&site_stats[i]; WSize ages=0;
        for(unsigned j=0;j<5;++j)ages+=s->ages[j];
        assert(s->count==total && s->bytes==bytes && s->frees==total && ages==total);
        assert(s->live==0 && s->live_count==0 && s->min_size==25 && s->max_size==48);
    }
    const unsigned ages[]={0,2,17,257,4097};
    for(unsigned a=0;a<5;++a) {
        wd_profile_request((void*)1,"age","owner.c",4,0);
        site_allocated((void*)4096,50); wd_profile_request_end();
        for(unsigned i=0;i<ages[a];++i) { site_allocated((void*)8192,8);site_freed((void*)8192,8); }
        site_freed((void*)4096,50);
    }
    assert(!site_live && site_count==3);
    for(unsigned i=0;i<WD_PROFILE_SITES;++i) if(site_stats[i].function && !strcmp(site_stats[i].function,"age"))
        for(unsigned j=0;j<5;++j)assert(site_stats[i].ages[j]==1);
    /* Exercise deletion just below saturation, with a permuted free order. */
    for(unsigned i=0;i<63;++i)site_allocated((void*)(uintptr_t)(4096+i*16),8);
    assert(site_live==63);
    for(unsigned i=0;i<63;++i)site_freed((void*)(uintptr_t)(4096+((i*17)%63)*16),8);
    assert(!site_live);
    site_report(); puts("allocation-site registry/lifetime oracle passed");
    return 0;
}

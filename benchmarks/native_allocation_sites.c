/* Private single-threaded compiler profiling only. No allocation/header changes,
   clock reads, symbolizer, host runtime, or production-runtime dependency. */
#ifndef WD_PROFILE_ENTRIES
#define WD_PROFILE_ENTRIES (1u<<21)
#endif
_Static_assert(WD_PROFILE_ENTRIES>=2 && !(WD_PROFILE_ENTRIES&(WD_PROFILE_ENTRIES-1)),"profile entry capacity must be a power of two");
#define WD_PROFILE_SITES 16384u
typedef struct { void *pointer; WSize bytes,birth; unsigned site; } ProfileEntry;
typedef struct {
    const char *function,*cfile;
    WInt cline,line,column,stride;
    unsigned file;
    WSize count,bytes,live,peak,frees,live_count,peak_count,min_size,max_size;
    WSize ages[5];
} ProfileSite;
static ProfileEntry site_entries[WD_PROFILE_ENTRIES];
static ProfileSite site_stats[WD_PROFILE_SITES];
static struct { const unsigned char *original; WInt size; char data[512]; } site_files[256];
static unsigned site_file_count=1,site_count;
static WSize site_clock,site_live,site_peak_count;
static void *site_context;
static const char *site_function="platform.raw",*site_cfile="platform";
static WInt site_cline,site_stride;
static char site_output[8192];
static WSize site_output_size;
void wd_profile_location(void *,WStr *,WInt *,WInt *);
static unsigned site_hash(WSize value,unsigned mask) {
    value^=value>>33; value*=UINT64_C(0xff51afd7ed558ccd);
    value^=value>>33; return (unsigned)value&mask;
}
static void site_require(int valid) { if(!valid) __builtin_trap(); }
void wd_profile_request(void *ctx,const char *function,const char *file,WInt line,WInt stride) {
    site_require(site_context==0);
    site_context=ctx; site_function=function; site_cfile=file; site_cline=line; site_stride=stride;
}
void wd_profile_request_end(void) {
    site_context=0; site_function="platform.raw"; site_cfile="platform"; site_cline=site_stride=0;
}
static unsigned site_file(WStr file) {
    if(!file.size) return 0;
    for(unsigned i=1;i<site_file_count;++i)
        if(site_files[i].original==file.data && site_files[i].size==file.size) return i;
    site_require(site_file_count<256 && file.size>0 && file.size<512);
    unsigned i=site_file_count++; site_files[i].original=file.data; site_files[i].size=file.size;
    copy_bytes(site_files[i].data,file.data,(WSize)file.size);
    return i;
}
static unsigned site_index(void) {
    WStr source={0}; WInt line=0,column=0;
    if(site_context) wd_profile_location(site_context,&source,&line,&column);
    unsigned file=site_file(source);
    WSize key=(WSize)(uintptr_t)site_function+(WSize)site_cline*31+(WSize)line*65537+(WSize)column*257+(WSize)file*17+(WSize)site_stride;
    unsigned i=site_hash(key,WD_PROFILE_SITES-1);
    for(unsigned n=0;n<WD_PROFILE_SITES;++n,i=(i+1)&(WD_PROFILE_SITES-1)) {
        ProfileSite *s=&site_stats[i];
        if(!s->function) {
            *s=(ProfileSite){.function=site_function,.cfile=site_cfile,.cline=site_cline,
                .line=line,.column=column,.stride=site_stride,.file=file};
            ++site_count; return i;
        }
        if(s->function==site_function && s->cfile==site_cfile && s->cline==site_cline &&
           s->line==line && s->column==column && s->file==file && s->stride==site_stride) return i;
    }
    __builtin_trap();
}
static void site_allocated(void *pointer,WSize bytes) {
    /* Backshift deletion needs an empty terminator in every probe chain. */
    site_require(site_live<WD_PROFILE_ENTRIES-1);
    unsigned index=site_index(); ProfileSite *s=&site_stats[index];
    unsigned i=site_hash((WSize)(uintptr_t)pointer>>4,WD_PROFILE_ENTRIES-1);
    for(unsigned n=0;n<WD_PROFILE_ENTRIES;++n,i=(i+1)&(WD_PROFILE_ENTRIES-1)) {
        site_require(site_entries[i].pointer!=pointer);
        if(!site_entries[i].pointer) {
            site_entries[i]=(ProfileEntry){pointer,bytes,++site_clock,index};
            ++s->count; s->bytes+=bytes; s->live+=bytes; ++s->live_count;
            if(s->live>s->peak) s->peak=s->live;
            if(s->live_count>s->peak_count) s->peak_count=s->live_count;
            if(s->count==1 || bytes<s->min_size) s->min_size=bytes;
            if(bytes>s->max_size) s->max_size=bytes;
            if(++site_live>site_peak_count) site_peak_count=site_live;
            return;
        }
    }
    __builtin_trap();
}
static void site_freed(void *pointer,WSize bytes) {
    if(!pointer) return;
    unsigned mask=WD_PROFILE_ENTRIES-1,i=site_hash((WSize)(uintptr_t)pointer>>4,mask);
    for(unsigned n=0;n<WD_PROFILE_ENTRIES;++n,i=(i+1)&mask) {
        ProfileEntry e=site_entries[i]; site_require(e.pointer!=0);
        if(e.pointer!=pointer) continue;
        site_require(e.bytes==bytes);
        ProfileSite *s=&site_stats[e.site]; WSize age=site_clock-e.birth;
        ++s->ages[age<=1?0:age<=16?1:age<=256?2:age<=4096?3:4];
        ++s->frees; s->live-=bytes; --s->live_count; --site_live;
        /* Backshift deletion preserves probe chains without accumulating tombstones. */
        unsigned hole=i;
        for(unsigned j=(i+1)&mask;site_entries[j].pointer;j=(j+1)&mask) {
            unsigned home=site_hash((WSize)(uintptr_t)site_entries[j].pointer>>4,mask);
            if(((j-home)&mask)>=((j-hole)&mask)) { site_entries[hole]=site_entries[j]; hole=j; }
        }
        site_entries[hole]=(ProfileEntry){0}; return;
    }
    __builtin_trap();
}
static void site_flush(void) {
    WInt channel=2;
#ifdef _WIN32
    channel=(WInt)(intptr_t)GetStdHandle((unsigned)-12);
#endif
    for(WSize i=0;i<site_output_size;) { WInt n=wd_file_write(channel,site_output+i,(WInt)(site_output_size-i)); site_require(n>0); i+=(WSize)n; }
    site_output_size=0;
}
static void site_write(const char *data,WSize size) {
    for(WSize i=0;i<size;) {
        WSize count=sizeof(site_output)-site_output_size;
        if(count>size-i) count=size-i;
        copy_bytes(site_output+site_output_size,data+i,count); i+=count; site_output_size+=count;
        if(site_output_size==sizeof(site_output)) site_flush();
    }
}
static void site_text(const char *value) { WSize n=0; while(value[n])++n; site_write(value,n); }
static void site_number(WSize value) {
    char data[20]; WSize n=0; do { data[n++]=(char)('0'+value%10); value/=10; } while(value);
    while(n) site_write(&data[--n],1);
}
static void site_string(const char *text) {
    site_text("\"");
    for(WSize i=0;text[i];++i) {
        unsigned char c=(unsigned char)text[i];
        if(c=='"' || c=='\\') site_text("\\");
        site_require(c>=32); site_write(text+i,1);
    }
    site_text("\"");
}
static void site_field(const char *key,WSize value) { site_text(",\"");site_text(key);site_text("\":");site_number(value); }
static void site_report(void) {
    site_text("{\"allocation_profile\":1"); site_field("sites",site_count);
    site_field("live_allocations",site_live); site_field("peak_live_allocations",site_peak_count);
    site_field("metadata_bytes",sizeof(site_entries)+sizeof(site_stats)+sizeof(site_files)+sizeof(site_output)); site_text("}\n");
    for(unsigned i=0;i<WD_PROFILE_SITES;++i) {
        ProfileSite *s=&site_stats[i]; if(!s->function) continue;
        site_text("{\"function\":");site_string(s->function);site_text(",\"c_file\":");site_string(s->cfile);
        site_text(",\"source\":");site_string(site_files[s->file].data);
        site_field("c_line",(WSize)s->cline);site_field("line",(WSize)s->line);site_field("column",(WSize)s->column);
        site_field("stride",(WSize)s->stride);site_field("allocations",s->count);site_field("requested_bytes",s->bytes);
        site_field("peak_live_bytes",s->peak);site_field("live_bytes_at_exit",s->live);
        site_field("frees",s->frees);site_field("peak_live_count",s->peak_count);
        site_field("min_size",s->min_size);site_field("max_size",s->max_size);
        site_text(",\"allocation_age_bins\":[");
        for(unsigned j=0;j<5;++j){if(j)site_text(",");site_number(s->ages[j]);}site_text("]}\n");
    }
    site_flush();
}

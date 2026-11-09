/* Private installer adapter. One sequential root, bounded handles/buffers.
   All installed file policy and manifest/receipt decisions remain in Wood.
   No arbitrary recursive deletion, shell execution, C++ or CRT on Windows. */
#include <stdint.h>
#include <stddef.h>
#include "sha256.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
typedef HANDLE FileHandle;
#define BAD INVALID_HANDLE_VALUE
#define IS_BAD(f) ((f)==BAD)
static int error(void) { return -(int)(GetLastError()?GetLastError():ERROR_GEN_FAILURE); }
static void close_file(FileHandle f) { if(f!=BAD) CloseHandle(f); }
static int read_file(FileHandle f,unsigned char *out,unsigned n) { DWORD got=0;return ReadFile(f,out,n,&got,0)?(int)got:error(); }
static int write_file(FileHandle f,const unsigned char *data,unsigned n) { DWORD got=0;return WriteFile(f,data,n,&got,0)&&got==n?0:error(); }
static int sync_file(FileHandle f) { return FlushFileBuffers(f)?0:error(); }
static int seek_file(FileHandle f,int64_t offset) { LARGE_INTEGER n;n.QuadPart=offset;return SetFilePointerEx(f,n,0,FILE_BEGIN)?0:error(); }
static WCHAR root_name[32768];
static HANDLE ancestors[256];
static unsigned ancestor_count;
static HANDLE lock_handle=BAD;
static int live;
static unsigned serial;
static int to_wide(const unsigned char *text,int64_t size,WCHAR out[32768]) {
    if(size<=0 || size>32000) return -87;
    for(int64_t i=0;i<size;++i) if(!text[i]) return -87;
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)text,(int)size,out,32767);
    if(!n) return error();
    out[n]=0;
    return n;
}
/* Holding each directory without FILE_SHARE_DELETE prevents ancestor renames
   and junction replacement while an operation is using an absolute Win32 path. */
static int hold_directory(const WCHAR *name,HANDLE *out) {
    HANDLE f=CreateFileW(name,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,0);
    if(f==BAD) return error();
    BY_HANDLE_FILE_INFORMATION info;
    if(!GetFileInformationByHandle(f,&info)) { int e=error();CloseHandle(f);return e; }
    if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {CloseHandle(f);return -87;}
    *out=f;return 0;
}
static int full_name(const unsigned char *relative,int64_t size,WCHAR out[32768]) {
    WCHAR tail[32768];int n=to_wide(relative,size,tail);if(n<0)return n;
    unsigned used=0;while(root_name[used]) {out[used]=root_name[used];++used;}
    if(used+(unsigned)n+2>=32768)return -87;
    out[used++]=L'\\';
    for(int i=0;i<n;++i) out[used++]=tail[i]==L'/'?L'\\':tail[i];
    out[used]=0;return 0;
}
typedef struct { WCHAR name[32768];HANDLE held[256];unsigned count; } Leaf;
static void leaf_close(Leaf *p) {while(p->count)CloseHandle(p->held[--p->count]);}
static int leaf(const unsigned char *rel,int64_t size,Leaf *p) {
    p->count=0;int e=full_name(rel,size,p->name);if(e<0)return e;
    unsigned start=0;while(root_name[start])++start;
    for(unsigned i=start+1;p->name[i];++i) if(p->name[i]==L'\\') {
        if(p->count>=256) {leaf_close(p);return -87;}
        p->name[i]=0;e=hold_directory(p->name,&p->held[p->count]);p->name[i]=L'\\';
        if(e<0) {leaf_close(p);return e;}++p->count;
    }
    return 0;
}
static FileHandle external(const unsigned char *file,int64_t size) {
    WCHAR input[32768],name[32768];if(to_wide(file,size,input)<0)return BAD;
    DWORD n=GetFullPathNameW(input,32760,name+4,0);if(!n||n>=32760)return BAD;
    if(name[5]!=L':'||name[6]!=L'\\'){SetLastError(ERROR_INVALID_NAME);return BAD;}
    name[0]=L'\\';name[1]=L'\\';name[2]=L'?';name[3]=L'\\';
    HANDLE f=CreateFileW(name,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,0);
    if(f==BAD)return BAD;
    BY_HANDLE_FILE_INFORMATION info;
    if(!GetFileInformationByHandle(f,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {CloseHandle(f);SetLastError(ERROR_INVALID_DATA);return BAD;}
    return f;
}
#else
/* Linux x86-64 syscall ABI; deliberately no libc/C++ runtime dependency. */
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h>
typedef int FileHandle;
#define BAD (-1)
#define IS_BAD(f) ((f)<0)
static long sc(long n,long a,long b,long c,long d,long e,long f) {
    register long r10 __asm__("r10")=d;register long r8 __asm__("r8")=e;register long r9 __asm__("r9")=f;
    long out;__asm__ volatile("syscall":"=a"(out):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");return out;
}
static int last_error;
static int error(void) {return last_error?last_error:-5;}
static int result(long n) {if(n<0)last_error=(int)n;return (int)n;}
static void close_file(FileHandle f) {if(f>=0)(void)sc(3,f,0,0,0,0,0);}
static int read_file(FileHandle f,unsigned char *out,unsigned n) {long r;do{r=sc(0,f,(long)out,n,0,0,0);}while(r==-4);return result(r);}
static int write_file(FileHandle f,const unsigned char *data,unsigned n) {unsigned done=0;while(done<n){long r=sc(1,f,(long)(data+done),n-done,0,0,0);if(r==-4)continue;if(r<=0)return result(r?r:-5);done+=(unsigned)r;}return 0;}
static int sync_file(FileHandle f) {return result(sc(74,f,0,0,0,0,0));}
static int seek_file(FileHandle f,int64_t offset) {long r=sc(8,f,offset,0,0,0,0);return r<0?result(r):0;}
static int root_fd=BAD,lock_handle=BAD,live;
static unsigned serial;
typedef struct {int parent;char name[1025];} Leaf;
static int cstring(const unsigned char *text,int64_t size,char *out,int64_t limit) {
    if(size<=0||size>=limit)return -22;
    for(int64_t i=0;i<size;++i) {if(!text[i])return -22;out[i]=(char)text[i];}out[size]=0;return 0;
}
static int open_dir(int parent,const char *name) {return result(sc(257,parent,(long)name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC,0,0,0));}
static void leaf_close(Leaf *p) {close_file(p->parent);p->parent=BAD;}
static int leaf(const unsigned char *relative,int64_t size,Leaf *p) {
    p->parent=BAD;if(cstring(relative,size,p->name,1025)<0)return -22;
    int current=open_dir(root_fd,".");if(current<0)return current;
    unsigned start=0;
    for(unsigned i=0;p->name[i];++i)if(p->name[i]=='/') {
        p->name[i]=0;int next=open_dir(current,p->name+start);p->name[i]='/';close_file(current);
        if(next<0)return next;
        current=next;start=i+1;
    }
    unsigned n=0;while(p->name[start+n]) {p->name[n]=p->name[start+n];++n;}p->name[n]=0;p->parent=current;return 0;
}
static FileHandle external(const unsigned char *file,int64_t size) {
    char name[32768];if(cstring(file,size,name,32768)<0)return BAD;
    int fd=result(sc(257,AT_FDCWD,(long)name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC,0,0,0));
    if(fd<0)return BAD;
    struct stat info;if(sc(5,fd,(long)&info,0,0,0,0)<0||!S_ISREG(info.st_mode)) {close_file(fd);last_error=-22;return BAD;}return fd;
}
#endif

/* Optional installer-frontend pump. No Wood callback is retained by C. */
static int (*frontend_poll)(void);
void wood_setup_poll_function(int (*callback)(void)){frontend_poll=callback;}

static int valid_relative(const unsigned char *p,int64_t n) {
    if(!live||n<=0||n>1024)return 0;
    int start=1;unsigned part=0;
    for(int64_t i=0;i<n;++i) {
        unsigned c=p[i];if(!c||c<32||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return 0;
        if(c=='/') {if(start||!part||p[i-1]=='.'||p[i-1]==' ')return 0;start=1;part=0;}
        else {if(start&&c=='.'&&(i+1==n||p[i+1]=='/'||(p[i+1]=='.'&&(i+2==n||p[i+2]=='/'))))return 0;start=0;if(++part>200)return 0;}
    }
    return !start&&p[n-1]!='.'&&p[n-1]!=' ';
}
int64_t wood_setup_close(void) {
#ifdef _WIN32
    close_file(lock_handle);lock_handle=BAD;
    while(ancestor_count)CloseHandle(ancestors[--ancestor_count]);
    root_name[0]=0;
#else
    close_file(lock_handle);lock_handle=BAD;close_file(root_fd);root_fd=BAD;
#endif
    live=0;return 0;
}
int64_t wood_setup_open(const unsigned char *root,int64_t size,int64_t create) {
    if(live)return -16;
#ifdef _WIN32
    WCHAR input[32768];int length=to_wide(root,size,input);if(length<0)return length;
    if(length<4||input[1]!=L':'||(input[2]!=L'\\'&&input[2]!=L'/'))return -87;
    root_name[0]=L'\\';root_name[1]=L'\\';root_name[2]=L'?';root_name[3]=L'\\';
    for(int i=0;i<=length;++i)root_name[4+i]=input[i]==L'/'?L'\\':input[i];
    unsigned end=(unsigned)length+4;if(root_name[end-1]==L'\\')root_name[--end]=0;
    int status=0;
    for(unsigned i=7;i<=end;++i)if(i==end||root_name[i]==L'\\') {
        WCHAR c=root_name[i];root_name[i]=0;
        if(i==end&&create&&!CreateDirectoryW(root_name,0)&&GetLastError()!=ERROR_ALREADY_EXISTS)status=error();
        if(status==0&&ancestor_count<256)status=hold_directory(root_name,&ancestors[ancestor_count]);else if(!status)status=-87;
        root_name[i]=c;if(status<0){wood_setup_close();return status;}++ancestor_count;
    }
    WCHAR lock[32768];unsigned i=0;while(root_name[i]){lock[i]=root_name[i];++i;}
    static const WCHAR suffix[]=L"\\.setup-lock";for(unsigned j=0;j<sizeof(suffix)/sizeof(WCHAR);++j)lock[i+j]=suffix[j];
    lock_handle=CreateFileW(lock,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT,0);
    BY_HANDLE_FILE_INFORMATION info;
    if(lock_handle==BAD||!GetFileInformationByHandle(lock_handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {int e=error();wood_setup_close();return e;}
#else
    char name[32768];if(cstring(root,size,name,32768)<0||name[0]!='/')return -22;
    root_fd=open_dir(AT_FDCWD,"/");if(root_fd<0)return root_fd;
    unsigned start=1;int status=0;
    for(unsigned i=1;;++i) if(!name[i]||name[i]=='/') {
        char c=name[i];name[i]=0;
        if(i==start||(!c&&start==1)){status=-22;break;}
        if(!c&&create) {long r=sc(258,root_fd,(long)(name+start),0700,0,0,0);if(r<0&&r!=-17){status=result(r);break;}}
        int next=open_dir(root_fd,name+start);name[i]=c;
        if(next<0){status=next;break;}close_file(root_fd);root_fd=next;start=i+1;if(!c)break;
    }
    if(status<0){wood_setup_close();return status;}
    lock_handle=result(sc(257,root_fd,(long)".setup-lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600,0,0));
    if(lock_handle<0){int e=error();wood_setup_close();return e;}
    struct stat lock_info;
    if(sc(5,lock_handle,(long)&lock_info,0,0,0,0)<0||!S_ISREG(lock_info.st_mode)){wood_setup_close();return -22;}
    status=result(sc(73,lock_handle,6,0,0,0,0));if(status<0){wood_setup_close();return status;}
#endif
    live=1;return 0;
}
int64_t wood_setup_kind(const unsigned char *rel,int64_t n) {
    if(!valid_relative(rel,n))return -22;
    Leaf p;int e=leaf(rel,n,&p);
#ifdef _WIN32
    if(e==-ERROR_PATH_NOT_FOUND||e==-ERROR_FILE_NOT_FOUND)return 0;
#else
    if(e==-2)return 0;
#endif
    if(e<0)return e;
#ifdef _WIN32
    DWORD attrs=GetFileAttributesW(p.name);int r;
    if(attrs==INVALID_FILE_ATTRIBUTES)r=GetLastError()==ERROR_FILE_NOT_FOUND?0:error();
    else r=(attrs&FILE_ATTRIBUTE_REPARSE_POINT)?3:((attrs&FILE_ATTRIBUTE_DIRECTORY)?2:1);
#else
    struct stat info;long status=sc(262,p.parent,(long)p.name,(long)&info,AT_SYMLINK_NOFOLLOW,0,0);
    int r=status==-2?0:(status<0?result(status):(S_ISREG(info.st_mode)?1:(S_ISDIR(info.st_mode)?2:3)));
#endif
    leaf_close(&p);return r;
}
int64_t wood_setup_platform(void) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
}
int64_t wood_setup_executable(int64_t *out,int64_t count) {
    if(!out||count<131072)return -22;
#ifdef _WIN32
    WCHAR path[32768];unsigned char bytes[131072];DWORD n=GetModuleFileNameW(0,path,32768);
    if(!n||n>=32768)return error();
    int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,(int)n,(char*)bytes,131072,0,0);if(size<=0)return error();
#else
    unsigned char bytes[131072];long got=sc(89,(long)"/proc/self/exe",(long)bytes,sizeof(bytes),0,0,0);
    if(got<0)return result(got);
    if(got>32767)return -36;
    int size=(int)got;
#endif
    for(int i=0;i<size;++i)out[i]=bytes[i];
    return size;
}
int64_t wood_setup_make_executable(const unsigned char *file,int64_t size) {
#ifdef _WIN32
    (void)file;(void)size;return 0;
#else
    FileHandle f=external(file,size);if(f<0)return error();int r=result(sc(91,f,0700,0,0,0,0));close_file(f);return r;
#endif
}
int64_t wood_setup_nonce(int64_t *out,int64_t count) {
    if(!out||count!=64)return -22;
    uint64_t seed[4];
#ifdef _WIN32
    LARGE_INTEGER tick;FILETIME wall;QueryPerformanceCounter(&tick);GetSystemTimeAsFileTime(&wall);
    seed[0]=(uint64_t)tick.QuadPart;seed[1]=((uint64_t)wall.dwHighDateTime<<32)|wall.dwLowDateTime;seed[2]=GetCurrentProcessId();
#else
    struct {long sec,nsec;} tick;long r=sc(228,1,(long)&tick,0,0,0,0);if(r<0)return result(r);
    seed[0]=(uint64_t)tick.sec;seed[1]=(uint64_t)tick.nsec;seed[2]=(uint64_t)sc(39,0,0,0,0,0,0);
#endif
    seed[3]=++serial;SetupHash hash;hash_init(&hash);hash_add(&hash,(const unsigned char*)seed,sizeof(seed));
    unsigned char digest[64];hash_end(&hash,digest);for(unsigned i=0;i<64;++i)out[i]=digest[i];return 0;
}
int64_t wood_setup_mkdir(const unsigned char *rel,int64_t n) {
    if(!valid_relative(rel,n))return -22;
    Leaf p;int e=leaf(rel,n,&p);if(e<0)return e;
#ifdef _WIN32
    int r=CreateDirectoryW(p.name,0)?0:error();
#else
    int r=result(sc(258,p.parent,(long)p.name,0700,0,0,0));
#endif
    leaf_close(&p);return r;
}
int64_t wood_setup_remove(const unsigned char *rel,int64_t n,int64_t directory) {
    if(!valid_relative(rel,n))return -22;
    Leaf p;int e=leaf(rel,n,&p);if(e<0)return e;
#ifdef _WIN32
    int r=(directory?RemoveDirectoryW(p.name):DeleteFileW(p.name))?0:error();
#else
    int r=result(sc(263,p.parent,(long)p.name,directory?AT_REMOVEDIR:0,0,0,0));
#endif
    leaf_close(&p);return r;
}
static int hash_file(FileHandle source,int64_t offset,int64_t size,unsigned char digest[64],FileHandle target) {
    if(offset<0||size< -1||size>2147483648LL)return -22;
    int status=seek_file(source,offset);if(status<0)return status;
    SetupHash hash;hash_init(&hash);unsigned char buffer[4096];int64_t total=0;
    while(size==-1||total<size) {
        if(frontend_poll&&frontend_poll()<0){frontend_poll=0;return -125;}
        unsigned count=sizeof(buffer);if(size>=0&&size-total<(int64_t)count)count=(unsigned)(size-total);
        int got=read_file(source,buffer,count);if(got<0)return got;
        if(!got){if(size>=0&&total!=size)return -5;break;}
        if(total>2147483648LL-got)return -27;
        hash_add(&hash,buffer,(unsigned)got);total+=got;
        if(target!=BAD){status=write_file(target,buffer,(unsigned)got);if(status<0)return status;}
    }
    hash_end(&hash,digest);return 0;
}
int64_t wood_setup_hash(const unsigned char *file,int64_t n,int64_t offset,int64_t size,int64_t *out,int64_t count) {
    if(count!=64||!out)return -22;
    FileHandle source=external(file,n);if(source==BAD)return error();
    unsigned char digest[64];int status=hash_file(source,offset,size,digest,BAD);close_file(source);
    if(status==0)for(unsigned i=0;i<64;++i)out[i]=digest[i];
    return status;
}
int64_t wood_setup_hash_bytes(const unsigned char *data,int64_t size,int64_t *out,int64_t count) {
    if(size<0||size>4194304||(!data&&size)||!out||count!=64)return -22;
    SetupHash hash;unsigned char digest[64];hash_init(&hash);hash_add(&hash,data,(unsigned)size);hash_end(&hash,digest);
    for(unsigned i=0;i<64;++i)out[i]=digest[i];
    return 0;
}
int64_t wood_setup_extract(const unsigned char *file,int64_t n,int64_t offset,int64_t size,const unsigned char *rel,int64_t rn,const unsigned char *expected,int64_t en) {
    if(!valid_relative(rel,rn)||en!=64||size<0)return -22;
    FileHandle source=external(file,n);if(source==BAD)return error();
    Leaf p;int status=leaf(rel,rn,&p);if(status<0){close_file(source);return status;}
#ifdef _WIN32
    FileHandle target=CreateFileW(p.name,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
#else
    FileHandle target=result(sc(257,p.parent,(long)p.name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0700,0,0));
#endif
    if(IS_BAD(target)){status=error();leaf_close(&p);close_file(source);return status;}
    unsigned char digest[64];status=hash_file(source,offset,size,digest,target);
    if(status==0)for(unsigned i=0;i<64;++i)if(digest[i]!=expected[i]){status=-74;break;}
    if(status==0)status=sync_file(target);
    close_file(target);close_file(source);
    if(status<0){
#ifdef _WIN32
        DeleteFileW(p.name);
#else
        (void)sc(263,p.parent,(long)p.name,0,0,0,0);
#endif
    }
    leaf_close(&p);return status;
}
int64_t wood_setup_write(const unsigned char *rel,int64_t n,const unsigned char *data,int64_t size,int64_t replace) {
    if(!valid_relative(rel,n)||size<0||size>4194304||(!data&&size))return -22;
    Leaf p;int status=leaf(rel,n,&p);if(status<0)return status;
    /* A sibling reserved temporary is created exclusively, never followed. */
    ++serial;
#ifdef _WIN32
    WCHAR temp[32768];unsigned i=0;while(p.name[i]){temp[i]=p.name[i];++i;}
    static const WCHAR suffix[]=L".setup-tmp-";for(unsigned j=0;j<11;++j)temp[i++]=suffix[j];
    for(unsigned j=0;j<8;++j)temp[i++]=L"0123456789abcdef"[(serial>>(j*4))&15];
    temp[i]=0;
    FileHandle target=CreateFileW(temp,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
#else
    char temp[1100];unsigned i=0;while(p.name[i]){temp[i]=p.name[i];++i;}
    const char *suffix=".setup-tmp-";for(unsigned j=0;j<11;++j)temp[i++]=suffix[j];
    for(unsigned j=0;j<8;++j)temp[i++]="0123456789abcdef"[(serial>>(j*4))&15];
    temp[i]=0;
    FileHandle target=result(sc(257,p.parent,(long)temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600,0,0));
#endif
    if(IS_BAD(target)){
        status=error();
#ifdef _WIN32
        status-=1000000;
#endif
        leaf_close(&p);return status;
    }
    status=write_file(target,data,(unsigned)size);
#ifdef _WIN32
    if(status<0)status-=2000000;
#endif
    if(status==0){
        status=sync_file(target);
#ifdef _WIN32
        if(status<0)status-=3000000;
#endif
    }
    close_file(target);
    if(status==0){
#ifdef _WIN32
        /* Indexers/scanners and ordinary readers can briefly deny rename or
           leave a just-deleted journal delete-pending. Retry only these Win32
           statuses, with identical no-overwrite flags and a fixed 310 ms budget.
           Persistent permissions/locks remain errors; never copy over a target. */
        for(unsigned attempt=0;attempt<6;++attempt) {
            if(MoveFileExW(temp,p.name,MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))break;
            DWORD code=GetLastError();
            if(attempt==5||(code!=ERROR_ACCESS_DENIED&&code!=ERROR_SHARING_VIOLATION&&code!=ERROR_LOCK_VIOLATION)){status=-4000000-(int)code;break;}
            Sleep(10u<<attempt);
        }
#else
        status=result(sc(316,p.parent,(long)temp,p.parent,(long)p.name,replace?0:1,0));if(status==0)status=sync_file(p.parent);
#endif
    }
#ifdef _WIN32
    if(status<0)DeleteFileW(temp);
#else
    if(status<0)(void)sc(263,p.parent,(long)temp,0,0,0,0);
#endif
    leaf_close(&p);return status;
}

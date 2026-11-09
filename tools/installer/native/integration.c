/* Private current-user registry/process adapter. Policy, journals and PATH
   component ownership are in Wood. Tests select a fixed isolated namespace. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>

static WCHAR environment_key[256],application_key[512];
static int configured,isolated;
static HANDLE settings_mutex;
static HKEY isolated_profile;
static WCHAR *from_utf8(const unsigned char *text,int64_t size) {
    if(size<0||size>131068||(!text&&size)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    for(int64_t i=0;i<size;++i)if(!text[i]){SetLastError(ERROR_INVALID_DATA);return 0;}
    int n=size?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)text,(int)size,0,0):0;
    if(size&&!n)return 0;
    WCHAR *result=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,((SIZE_T)n+1)*sizeof(WCHAR));
    if(!result){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}
    if(n&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)text,(int)size,result,n)){HeapFree(GetProcessHeap(),0,result);return 0;}
    return result;
}
static void drop(void *p){if(p)HeapFree(GetProcessHeap(),0,p);}
static int64_t failure(void){DWORD n=GetLastError();return -(int64_t)(n?n:ERROR_GEN_FAILURE);}
static unsigned length(const WCHAR *s){unsigned n=0;while(s[n])++n;return n;}
static void append_w(WCHAR *target,const WCHAR *tail){unsigned i=length(target),j=0;do{target[i+j]=tail[j];}while(tail[j++]);}
static int equal(const WCHAR *a,const WCHAR *b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static int permitted(int64_t section,const WCHAR *key){
    if(section==0)return equal(key,L"Path");
    if(section!=1)return 0;
    const WCHAR *fields[]={L"DisplayName",L"DisplayVersion",L"Publisher",L"InstallLocation",L"UninstallString",L"WoodInstallerOwner",L"DisplayIcon"};
    for(unsigned i=0;i<7;++i)if(equal(key,fields[i]))return 1;
    return 0;
}
int64_t wood_setup_registry_scope(const unsigned char *space,int64_t sn,const unsigned char *id,int64_t in) {
    if(configured||in<1||in>80||(sn!=0&&sn!=32))return -ERROR_INVALID_PARAMETER;
    for(int64_t i=0;i<sn;++i)if(!((space[i]>='0'&&space[i]<='9')||(space[i]>='a'&&space[i]<='f')))return -ERROR_INVALID_PARAMETER;
    for(int64_t i=0;i<in;++i)if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='A'&&id[i]<='Z')||(id[i]>='0'&&id[i]<='9')||id[i]=='_'||id[i]=='-'||id[i]=='.'))return -ERROR_INVALID_PARAMETER;
    WCHAR *app=from_utf8(id,in),*name=sn?from_utf8(space,sn):0;
    if(!app||(sn&&!name)){drop(app);drop(name);return failure();}
    if(sn){
        WCHAR profile[256]={0};append_w(profile,L"Software\\WoodInstallerTests\\");append_w(profile,name);append_w(profile,L"\\Profile");
        LSTATUS status=RegCreateKeyExW(HKEY_CURRENT_USER,profile,0,0,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,0,&isolated_profile,0);
        if(!status)status=RegOverridePredefKey(HKEY_CURRENT_USER,isolated_profile);
        if(status){if(isolated_profile)RegCloseKey(isolated_profile);isolated_profile=0;drop(app);drop(name);return -(int64_t)status;}
        /* This process-wide override also confines in-process shell-dialog
           preferences. A child worker explicitly selects the same namespace. */
    }
    append_w(environment_key,L"Environment");append_w(application_key,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WoodInstaller.");
    append_w(application_key,app);
    WCHAR mutex_name[128]={0};append_w(mutex_name,L"Local\\WoodInstallerSettings-v1");if(name)append_w(mutex_name,name);
    settings_mutex=CreateMutexW(0,FALSE,mutex_name);drop(app);drop(name);
    if(!settings_mutex)return failure();configured=1;isolated=sn!=0;return 0;
}
int64_t wood_setup_settings_lock(int64_t enter){
    if(!configured)return -ERROR_INVALID_STATE;
    if(enter){DWORD n=WaitForSingleObject(settings_mutex,10000);return n==WAIT_OBJECT_0||n==WAIT_ABANDONED?0:(n==WAIT_TIMEOUT?-ERROR_TIMEOUT:failure());}
    return ReleaseMutex(settings_mutex)?0:failure();
}
static LSTATUS read_value(int64_t section,const WCHAR *name,DWORD *type,WCHAR **value){
    HKEY key;*type=0;*value=0;
    LSTATUS status=RegOpenKeyExW(HKEY_CURRENT_USER,section==0?environment_key:application_key,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key);
    if(status==ERROR_FILE_NOT_FOUND)return ERROR_SUCCESS;if(status)return status;
    DWORD bytes=0;status=RegQueryValueExW(key,name,0,type,0,&bytes);
    if(status==ERROR_FILE_NOT_FOUND){*type=0;RegCloseKey(key);return ERROR_SUCCESS;}
    if(status){RegCloseKey(key);return status;}
    if((*type!=REG_SZ&&*type!=REG_EXPAND_SZ)||bytes>65536||bytes%2){RegCloseKey(key);return ERROR_INVALID_DATA;}
    WCHAR *data=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)bytes+2);
    if(!data){RegCloseKey(key);return ERROR_NOT_ENOUGH_MEMORY;}
    DWORD actual_type=0,actual=bytes;status=RegQueryValueExW(key,name,0,&actual_type,(BYTE*)data,&actual);RegCloseKey(key);
    if(status||actual_type!=*type||actual!=bytes){drop(data);return status?status:ERROR_RETRY;}
    unsigned count=bytes/2;if(count&&data[count-1]==0)--count;
    for(unsigned i=0;i<count;++i)if(!data[i]){drop(data);return ERROR_INVALID_DATA;}
    data[count]=0;*value=data;return ERROR_SUCCESS;
}
int64_t wood_setup_setting_read(int64_t section,const unsigned char *name,int64_t nn,int64_t *info,int64_t ic,int64_t *out,int64_t cap){
    if(!configured||!info||ic!=1||!out||cap<131072)return -ERROR_INVALID_PARAMETER;
    WCHAR *key=from_utf8(name,nn);if(!key)return failure();if(!permitted(section,key)){drop(key);return -ERROR_ACCESS_DENIED;}
    DWORD type;WCHAR *data;LSTATUS status=read_value(section,key,&type,&data);drop(key);if(status)return -(int64_t)status;
    info[0]=type;if(!type)return 0;
    unsigned char *bytes=HeapAlloc(GetProcessHeap(),0,131072);if(!bytes){drop(data);return -ERROR_NOT_ENOUGH_MEMORY;}
    unsigned count=length(data);int n=count?WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data,(int)count,(char*)bytes,131072,0,0):0;
    if(count&&!n){int64_t error=failure();drop(data);drop(bytes);return error;}
    for(int i=0;i<n;++i)out[i]=bytes[i];drop(data);drop(bytes);return n;
}
int64_t wood_setup_setting_write(int64_t section,const unsigned char *name,int64_t nn,int64_t expected_type,const unsigned char *expected,int64_t en,int64_t next_type,const unsigned char *next,int64_t nx){
    if(!configured||(expected_type!=0&&expected_type!=1&&expected_type!=2)||(next_type!=0&&next_type!=1&&next_type!=2))return -ERROR_INVALID_PARAMETER;
    WCHAR *key=from_utf8(name,nn),*before=from_utf8(expected,en),*after=from_utf8(next,nx);
    if(!key||!before||!after){drop(key);drop(before);drop(after);return failure();}
    if(!permitted(section,key)){drop(key);drop(before);drop(after);return -ERROR_ACCESS_DENIED;}
    /* All published strings must be readable/reversible through this adapter.
       Reserve the UTF-16 terminator, including for application registration. */
    if(next_type&&length(after)>32766){drop(key);drop(before);drop(after);return -ERROR_BUFFER_OVERFLOW;}
    DWORD type;WCHAR *current;LSTATUS status=read_value(section,key,&type,&current);
    if(!status&&(type!=(DWORD)expected_type||(type&&!equal(current,before))))status=ERROR_RETRY;
    drop(current);
    if(!status){
        HKEY opened;
        if(next_type)status=RegCreateKeyExW(HKEY_CURRENT_USER,section==0?environment_key:application_key,0,0,0,KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_WOW64_64KEY,0,&opened,0);
        else status=RegOpenKeyExW(HKEY_CURRENT_USER,section==0?environment_key:application_key,0,KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_WOW64_64KEY,&opened);
        if(!status){
            status=next_type?RegSetValueExW(opened,key,0,(DWORD)next_type,(const BYTE*)after,(length(after)+1)*2):RegDeleteValueW(opened,key);
            if(status==ERROR_FILE_NOT_FOUND&&!next_type)status=0;
            if(!status)status=RegFlushKey(opened);RegCloseKey(opened);
        }else if(status==ERROR_FILE_NOT_FOUND&&!next_type)status=0;
    }
    drop(key);drop(before);drop(after);
    if(!status&&section==0&&!isolated){DWORD_PTR result;SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"Environment",SMTO_ABORTIFHUNG,200,&result);}
    return status?-(int64_t)status:0;
}
int64_t wood_setup_registration_available(const unsigned char *owner,int64_t size){
    if(!configured)return -ERROR_INVALID_STATE;
    WCHAR *want=from_utf8(owner,size);if(!want)return failure();DWORD type;WCHAR *actual;LSTATUS status=read_value(1,L"WoodInstallerOwner",&type,&actual);
    if(status){drop(want);return -(int64_t)status;}
    if(type){int match=equal(actual,want);drop(actual);drop(want);return match;}
    drop(want);HKEY key;status=RegOpenKeyExW(HKEY_CURRENT_USER,application_key,0,KEY_QUERY_VALUE|KEY_ENUMERATE_SUB_KEYS|KEY_WOW64_64KEY,&key);
    if(status==ERROR_FILE_NOT_FOUND)return 1;if(status)return -(int64_t)status;
    DWORD children=0,values=0;status=RegQueryInfoKeyW(key,0,0,0,&children,0,0,&values,0,0,0,0);RegCloseKey(key);return status?-(int64_t)status:(children==0&&values==0);
}
int64_t wood_setup_known_path(int64_t kind,int64_t *out,int64_t count){
    if(!out||count<131072)return -ERROR_INVALID_PARAMETER;
    WCHAR path[32768];DWORD n;
    if(kind==0)n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
    else if(kind==1){n=GetSystemDirectoryW(path,32768);if(n&&n<32700){append_w(path,L"\\WindowsPowerShell\\v1.0\\powershell.exe");n=length(path);}}
    else return -ERROR_INVALID_PARAMETER;
    if(!n||n>=32768)return failure();
    unsigned char bytes[131072];int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,(int)n,(char*)bytes,sizeof(bytes),0,0);if(!size)return failure();
    for(int i=0;i<size;++i)out[i]=bytes[i];return size;
}
int64_t wood_setup_start_cleanup(const unsigned char *encoded,int64_t size){
    if(size<1||size>28000)return -ERROR_INVALID_PARAMETER;
    for(int64_t i=0;i<size;++i)if(!((encoded[i]>='A'&&encoded[i]<='Z')||(encoded[i]>='a'&&encoded[i]<='z')||(encoded[i]>='0'&&encoded[i]<='9')||encoded[i]=='+'||encoded[i]=='/'||encoded[i]=='='))return -ERROR_INVALID_PARAMETER;
    WCHAR exe[32768],command[32768];UINT n=GetSystemDirectoryW(exe,32768);if(!n||n>32000)return failure();append_w(exe,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    if(GetFileAttributesW(exe)==INVALID_FILE_ATTRIBUTES)return -ERROR_FILE_NOT_FOUND;
    command[0]=L'"';command[1]=0;append_w(command,exe);append_w(command,L"\" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand ");
    unsigned at=length(command);if(at+(unsigned)size>=32768)return -ERROR_INVALID_PARAMETER;
    for(int64_t i=0;i<size;++i)command[at++]=encoded[i];command[at]=0;
    STARTUPINFOW info={0};PROCESS_INFORMATION child={0};info.cb=sizeof(info);info.dwFlags=STARTF_USESHOWWINDOW;info.wShowWindow=SW_HIDE;
    if(!CreateProcessW(exe,command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&info,&child))return failure();
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return 0;
}
int64_t wood_setup_process_identity(int64_t *out,int64_t count){
    if(!out||count!=2)return -ERROR_INVALID_PARAMETER;
    FILETIME created,exit,kernel,user;if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user))return failure();
    out[0]=GetCurrentProcessId();out[1]=(int64_t)(((uint64_t)created.dwHighDateTime<<32)|created.dwLowDateTime);return 0;
}

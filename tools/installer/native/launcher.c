/* Relocatable Windows console shim. No registry/global environment mutation,
   command shell, C/C++ runtime or fallback compiler. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
static unsigned size_w(const WCHAR *s){unsigned n=0;while(s[n])++n;return n;}
static int add(WCHAR *out,const WCHAR *s){unsigned n=size_w(out),m=size_w(s);if(n+m>=32768)return 0;for(unsigned i=0;i<=m;++i)out[n+i]=s[i];return 1;}
static int64_t failed(void){DWORD e=GetLastError();return -(int64_t)(e?e:ERROR_GEN_FAILURE);}
int64_t wood_setup_launch_compiler(void){
    WCHAR own[32768],root[32768],compiler[32768],tools[32768],library[32768],command[32768];
    DWORD n=GetModuleFileNameW(0,own,32768);if(!n||n>=32768)return failed();
    while(n&&own[n-1]!=L'\\')--n;if(!n)return -ERROR_INVALID_NAME;own[n-1]=0;
    for(unsigned i=0;i<n;++i)root[i]=own[i];
    compiler[0]=0;if(!add(compiler,own)||!add(compiler,L"\\wood-compiler.exe"))return -ERROR_FILENAME_EXCED_RANGE;
    unsigned parent=size_w(root);while(parent&&root[parent-1]!=L'\\')--parent;if(!parent)return -ERROR_INVALID_NAME;root[parent-1]=0;
    tools[0]=0;library[0]=0;if(!add(tools,root)||!add(tools,L"\\tools\\llvm")||!add(library,root)||!add(library,L"\\lib"))return -ERROR_FILENAME_EXCED_RANGE;
    WCHAR previous[32768];DWORD count=GetEnvironmentVariableW(L"PATH",previous,32768);
    if(count>=32768)return -ERROR_ENVVAR_NOT_FOUND;
    if(count&&(!add(tools,L";")||!add(tools,previous)))return -ERROR_FILENAME_EXCED_RANGE;
    if(!SetEnvironmentVariableW(L"PATH",tools))return failed();
    count=GetEnvironmentVariableW(L"LIB",previous,32768);if(count>=32768)return -ERROR_ENVVAR_NOT_FOUND;
    if(count&&(!add(library,L";")||!add(library,previous)))return -ERROR_FILENAME_EXCED_RANGE;
    if(!SetEnvironmentVariableW(L"LIB",library))return failed();
    /* Preserve the original argument tail byte-for-byte in UTF-16. Windows
       executable names cannot contain a quote; no shell parses this command. */
    const WCHAR *tail=GetCommandLineW();if(*tail==L'"'){++tail;while(*tail&&*tail!=L'"')++tail;if(*tail)++tail;}else while(*tail&&*tail!=L' '&&*tail!=L'\t')++tail;
    command[0]=0;if(!add(command,L"\"")||!add(command,compiler)||!add(command,L"\"")||!add(command,tail))return -ERROR_FILENAME_EXCED_RANGE;
    HANDLE job=CreateJobObjectW(0,0);if(!job)return failed();
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){int64_t e=failed();CloseHandle(job);return e;}
    STARTUPINFOW start={0};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;start.hStdInput=GetStdHandle(STD_INPUT_HANDLE);start.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);start.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child={0};
    if(!CreateProcessW(compiler,command,0,0,TRUE,CREATE_SUSPENDED,0,0,&start,&child)){int64_t e=failed();CloseHandle(job);return e;}
    if(!AssignProcessToJobObject(job,child.hProcess)||ResumeThread(child.hThread)==(DWORD)-1){int64_t e=failed();TerminateProcess(child.hProcess,2);WaitForSingleObject(child.hProcess,INFINITE);CloseHandle(child.hThread);CloseHandle(child.hProcess);CloseHandle(job);return e;}
    CloseHandle(child.hThread);DWORD result=2;
    if(WaitForSingleObject(child.hProcess,INFINITE)==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&result);
    CloseHandle(child.hProcess);CloseHandle(job);return result;
}

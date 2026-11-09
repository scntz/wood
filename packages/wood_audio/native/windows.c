/* Optional Windows audio adapter over winmm's waveOut/waveIn. winmm.dll is loaded
   at use, so no SDK import library is needed and absence is a clean error. Devices
   are always reset and closed before returning. No Wood object crosses this ABI. */
#include <stdint.h>
typedef void *HANDLE;
typedef void *HMODULE;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef uintptr_t DWORD_PTR;
typedef struct { WORD wFormatTag,nChannels; DWORD nSamplesPerSec,nAvgBytesPerSec; WORD nBlockAlign,wBitsPerSample,cbSize; } WaveFormat;
typedef struct Header { char *lpData; DWORD dwBufferLength,dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags,dwLoops; struct Header *lpNext; DWORD_PTR reserved; } Header;
#define IMPORT __declspec(dllimport)
IMPORT HMODULE __stdcall LoadLibraryExW(const unsigned short*,HANDLE,DWORD);
IMPORT int __stdcall FreeLibrary(HMODULE);
IMPORT void *__stdcall GetProcAddress(HMODULE,const char*);
IMPORT HANDLE __stdcall GetProcessHeap(void);
IMPORT void *__stdcall HeapAlloc(HANDLE,DWORD,uint64_t);
IMPORT int __stdcall HeapFree(HANDLE,DWORD,void*);
IMPORT HANDLE __stdcall CreateEventW(void*,int,int,const unsigned short*);
IMPORT int __stdcall CloseHandle(HANDLE);
IMPORT DWORD __stdcall WaitForSingleObject(HANDLE,DWORD);
enum { E_DEVICE=-1,E_FORMAT=-2,E_ARGUMENT=-3,E_LIBRARY=-4,E_FAILURE=-5 };
enum { WAVE_MAPPER_ID=0xFFFFFFFFu,CALLBACK_EVENT_FLAG=0x00050000u,WHDR_DONE_FLAG=1u,BAD_FORMAT=32,HEAP_ZERO=8u,SEARCH_SYSTEM32=0x800u };
typedef UINT (__stdcall *Count)(void);
typedef UINT (__stdcall *Open)(HANDLE*,UINT,const WaveFormat*,DWORD_PTR,DWORD_PTR,DWORD);
typedef UINT (__stdcall *HeaderCall)(HANDLE,Header*,UINT);
typedef UINT (__stdcall *Volume)(HANDLE,DWORD);
typedef UINT (__stdcall *Handle)(HANDLE);
static HMODULE library(void) {
    static const unsigned short name[]={'w','i','n','m','m','.','d','l','l',0};
    return LoadLibraryExW(name,0,SEARCH_SYSTEM32);
}
static int valid(int64_t size,int64_t channels,int64_t rate) {
    return size>0 && size<=1073741823 && (channels==1 || channels==2) && rate>=8000 && rate<=192000;
}
static WaveFormat format(int64_t channels,int64_t rate) {
    WaveFormat f={1,(WORD)channels,(DWORD)rate,(DWORD)rate*(DWORD)(channels*2),(WORD)(channels*2),16,0};
    return f;
}
static int64_t count_devices(const char *name) {
    HMODULE module=library(); if(!module) return E_LIBRARY;
    Count count=(Count)GetProcAddress(module,name); int64_t n=count?(int64_t)count():E_LIBRARY;
    FreeLibrary(module); return n;
}
int64_t wood_audio_output_count(void) { return count_devices("waveOutGetNumDevs"); }
int64_t wood_audio_input_count(void) { return count_devices("waveInGetNumDevs"); }
int64_t wood_audio_play(const int64_t *samples,int64_t size,int64_t channels,int64_t rate,int64_t volume) {
    if(!samples || !valid(size,channels,rate) || size%channels || volume<0 || volume>65535) return E_ARGUMENT;
    HMODULE module=library(); if(!module) return E_LIBRARY;
    Open open=(Open)GetProcAddress(module,"waveOutOpen"); HeaderCall prepare=(HeaderCall)GetProcAddress(module,"waveOutPrepareHeader");
    HeaderCall write=(HeaderCall)GetProcAddress(module,"waveOutWrite"); HeaderCall unprepare=(HeaderCall)GetProcAddress(module,"waveOutUnprepareHeader");
    Volume level=(Volume)GetProcAddress(module,"waveOutSetVolume"); Handle reset=(Handle)GetProcAddress(module,"waveOutReset"); Handle shut=(Handle)GetProcAddress(module,"waveOutClose");
    int64_t result=E_LIBRARY; HANDLE done=0,device=0; int16_t *pcm=0; Header header={0};
    if(!open || !prepare || !write || !unprepare || !level || !reset || !shut) goto finish;
    result=E_FAILURE;
    pcm=(int16_t*)HeapAlloc(GetProcessHeap(),0,(uint64_t)size*2); done=CreateEventW(0,0,0,0);
    if(!pcm || !done) goto finish;
    for(int64_t i=0;i<size;++i) { if(samples[i]<-32768 || samples[i]>32767) { result=E_ARGUMENT; goto finish; } pcm[i]=(int16_t)samples[i]; }
    WaveFormat f=format(channels,rate);
    UINT opened=open(&device,WAVE_MAPPER_ID,&f,(DWORD_PTR)done,0,CALLBACK_EVENT_FLAG);
    if(opened) { device=0; result=opened==BAD_FORMAT?E_FORMAT:E_DEVICE; goto finish; }
    level(device,(DWORD)volume|((DWORD)volume<<16));
    header.lpData=(char*)pcm; header.dwBufferLength=(DWORD)size*2;
    if(prepare(device,&header,sizeof header)) goto finish;
    if(!write(device,&header,sizeof header)) {
        while(!(*(volatile DWORD*)&header.dwFlags&WHDR_DONE_FLAG)) WaitForSingleObject(done,100);
        result=0;
    }
    reset(device); unprepare(device,&header,sizeof header);
finish:
    if(device) shut(device);
    if(done) CloseHandle(done);
    if(pcm) HeapFree(GetProcessHeap(),0,pcm);
    FreeLibrary(module); return result;
}
int64_t wood_audio_record(int64_t *samples,int64_t size,int64_t channels,int64_t rate) {
    if(!samples || !valid(size,channels,rate) || size%channels) return E_ARGUMENT;
    HMODULE module=library(); if(!module) return E_LIBRARY;
    Open open=(Open)GetProcAddress(module,"waveInOpen"); HeaderCall prepare=(HeaderCall)GetProcAddress(module,"waveInPrepareHeader");
    HeaderCall add=(HeaderCall)GetProcAddress(module,"waveInAddBuffer"); HeaderCall unprepare=(HeaderCall)GetProcAddress(module,"waveInUnprepareHeader");
    Handle start=(Handle)GetProcAddress(module,"waveInStart"); Handle reset=(Handle)GetProcAddress(module,"waveInReset"); Handle shut=(Handle)GetProcAddress(module,"waveInClose");
    int64_t result=E_LIBRARY; HANDLE done=0,device=0; int16_t *pcm=0; Header header={0};
    if(!open || !prepare || !add || !unprepare || !start || !reset || !shut) goto finish;
    result=E_FAILURE;
    pcm=(int16_t*)HeapAlloc(GetProcessHeap(),HEAP_ZERO,(uint64_t)size*2); done=CreateEventW(0,0,0,0);
    if(!pcm || !done) goto finish;
    WaveFormat f=format(channels,rate);
    UINT opened=open(&device,WAVE_MAPPER_ID,&f,(DWORD_PTR)done,0,CALLBACK_EVENT_FLAG);
    if(opened) { device=0; result=opened==BAD_FORMAT?E_FORMAT:E_DEVICE; goto finish; }
    header.lpData=(char*)pcm; header.dwBufferLength=(DWORD)size*2;
    if(prepare(device,&header,sizeof header)) goto finish;
    if(!add(device,&header,sizeof header) && !start(device)) {
        while(!(*(volatile DWORD*)&header.dwFlags&WHDR_DONE_FLAG)) WaitForSingleObject(done,100);
        for(int64_t i=0;i<size;++i) samples[i]=pcm[i];
        result=0;
    }
    reset(device); unprepare(device,&header,sizeof header);
finish:
    if(device) shut(device);
    if(done) CloseHandle(done);
    if(pcm) HeapFree(GetProcessHeap(),0,pcm);
    FreeLibrary(module); return result;
}

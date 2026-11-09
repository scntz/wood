/* Freestanding deterministic callback concurrency oracle. Eight foreign calls
   must all install their scopes before ANY invokes its distinct callback. */
#include <stdint.h>
static unsigned arrivals,generation;
#ifdef _WIN32
__declspec(dllimport) void *GetCurrentProcess(void);
__declspec(dllimport) int GetProcessHandleCount(void *,unsigned *);
int64_t wood_task_handles(void) {
    unsigned count=0;
    return GetProcessHandleCount(GetCurrentProcess(),&count)?(int64_t)count:-1;
}
#else
int64_t wood_task_handles(void) { return 0; }
#endif
int64_t wood_task_gate(int64_t participants,int64_t value,int64_t (*callback)(int64_t)) {
    unsigned round=__atomic_load_n(&generation,__ATOMIC_ACQUIRE);
    if(__atomic_add_fetch(&arrivals,1,__ATOMIC_ACQ_REL)==(unsigned)participants) {
        __atomic_store_n(&arrivals,0,__ATOMIC_RELAXED);
        __atomic_add_fetch(&generation,1,__ATOMIC_RELEASE);
    } else while(__atomic_load_n(&generation,__ATOMIC_ACQUIRE)==round) __builtin_ia32_pause();
    return callback(value);
}

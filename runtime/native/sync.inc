/* Private ABI-4 synchronization. Owner layout is unchanged. All shared owner
   counts use atomic accesses; mutable values still belong to one Wood thread. */
typedef struct { WSize bits; } WLock;
#ifdef _WIN32
__declspec(dllimport) void AcquireSRWLockExclusive(void *);
__declspec(dllimport) void ReleaseSRWLockExclusive(void *);
static void w_lock(WLock *lock) { AcquireSRWLockExclusive(lock); }
static void w_unlock(WLock *lock) { ReleaseSRWLockExclusive(lock); }
#else
static WInt d_sys(WInt,WInt,WInt,WInt,WInt,WInt,WInt);
static void w_lock(WLock *lock) {
    WSize expected=0;
    if(__atomic_compare_exchange_n(&lock->bits,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    while(__atomic_exchange_n(&lock->bits,2,__ATOMIC_ACQUIRE)) {
        WInt status=d_sys(202,(WInt)(intptr_t)&lock->bits,128,2,0,0,0);
        if(status<0 && status!=-4 && status!=-11) __builtin_trap();
    }
}
static void w_unlock(WLock *lock) {
    if(__atomic_exchange_n(&lock->bits,0,__ATOMIC_RELEASE)==2)
        (void)d_sys(202,(WInt)(intptr_t)&lock->bits,129,1,0,0,0);
}
#endif
static WSize owner_refs(const WOwner *owner) {
    return __atomic_load_n(&owner->refs,__ATOMIC_ACQUIRE);
}

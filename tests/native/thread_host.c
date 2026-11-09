/* Real pthread port for sanitizer/oracle tests only. Normal native objects use
   CreateThread or guarded-stack clone/futex, never this hosted translation unit. */
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct { pthread_t thread; unsigned (*work)(void *); void *argument; } Thread;
static int fail_starts,live;
void wd_test_thread_fail(int fail) { __atomic_store_n(&fail_starts,fail,__ATOMIC_RELAXED); }
int wd_test_threads_live(void) { return __atomic_load_n(&live,__ATOMIC_RELAXED); }
static void *enter(void *value) { Thread *t=value; (void)t->work(t->argument); return 0; }
int64_t wd_test_thread_start(unsigned (*work)(void *),void *argument) {
    if(__atomic_load_n(&fail_starts,__ATOMIC_RELAXED)) return 0;
    Thread *t=malloc(sizeof *t); if(!t) return 0;
    t->work=work; t->argument=argument;
    if(pthread_create(&t->thread,0,enter,t)) { free(t); return 0; }
    __atomic_fetch_add(&live,1,__ATOMIC_RELAXED);
    return (int64_t)(intptr_t)t;
}
void wd_test_thread_join(int64_t handle) {
    Thread *t=(void*)(intptr_t)handle;
    if(pthread_join(t->thread,0)) abort();
    free(t); __atomic_fetch_sub(&live,1,__ATOMIC_RELAXED);
}

/* Restrict only this child process, after startup. Real pthread_create failure,
   without a runtime mock or any system-wide/user configuration change. */
#include <stdint.h>
#include <sys/resource.h>
#include <unistd.h>
static struct rlimit original;
int64_t wood_thread_limit(int64_t enable) {
    if (getuid() == 0) return -1; /* Root bypasses RLIMIT_NPROC. */
    if (!enable) return setrlimit(RLIMIT_NPROC, &original);
    if (getrlimit(RLIMIT_NPROC, &original)) return -1;
    struct rlimit bounded = original;
    bounded.rlim_cur = 1;
    return setrlimit(RLIMIT_NPROC, &bounded);
}

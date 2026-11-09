/* Real hosted Linux allocator/I/O port used only for native ASan/UBSan tests.
   Normal freestanding executables use their existing syscall/Kernel32 port. */
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#ifdef WOOD_NATIVE_CLOSE_FAULT
#define WOOD_TEST_FILE_FAULTS
#endif
#include "file_host.inc"

extern int wd_main(void);
void *wd_platform_alloc(uint64_t bytes) { return malloc((size_t)bytes); }
void wd_platform_free(void *pointer, uint64_t bytes) { (void)bytes; free(pointer); }
int64_t host_write(int64_t fd, void *data, int64_t count) __asm__("wd.sys_write");
int64_t host_write(int64_t fd, void *data, int64_t count) {
    ssize_t result = write((int)fd, data, (size_t)count);
    return result < 0 ? -(int64_t)errno : (int64_t)result;
}
void host_exit(int status) __asm__("wd.exit");
void host_exit(int status) { _exit(status); }
extern void wd_m_args_init(int64_t, const void *);
int main(int argc, char **argv) {
    wd_m_args_init(argc, argv);
#ifdef WOOD_NATIVE_CLOSE_FAULT
    file_close_error = 5;
#endif
    return wd_main();
}

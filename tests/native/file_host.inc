/* Real POSIX file port shared by the hosted sanitizer harnesses. These are
   test binaries only; normal native ELF/PE programs use OS-only LLVM ports. */
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#ifdef WOOD_TEST_FILE_FAULTS
static unsigned file_handles;
static int file_close_error;
#endif
int64_t wd_file_open(const void *path, int64_t size, int64_t mode) {
    (void)size;
    int flags = mode == 0 ? O_RDONLY : mode == 1 ? O_WRONLY|O_CREAT|O_TRUNC :
                mode == 2 ? O_WRONLY|O_CREAT|O_APPEND : O_WRONLY|O_CREAT|O_EXCL;
    /* F_DUPFD_CLOEXEC avoids relying on feature-test macros in test callers. */
    int fd = open(path,flags,0666);
    if (fd < 0) return -(int64_t)errno;
    if (fcntl(fd,F_SETFD,FD_CLOEXEC) < 0) {
        int error = errno; close(fd); return -(int64_t)error;
    }
    #ifdef WOOD_TEST_FILE_FAULTS
    ++file_handles;
    #endif
    return fd;
}
int64_t wd_file_close(int64_t fd) {
    int result = close((int)fd);
    #ifdef WOOD_TEST_FILE_FAULTS
    if (result == 0) {
        --file_handles;
        if (file_close_error) return -file_close_error;
    }
    #endif
    return result < 0 ? -(int64_t)errno : 0;
}
int64_t wd_file_read(int64_t fd, void *data, int64_t count) {
    ssize_t result = read((int)fd,data,(size_t)count);
    return result < 0 ? -(int64_t)errno : (int64_t)result;
}
int64_t wd_file_write(int64_t fd, const void *data, int64_t count) {
    ssize_t result = write((int)fd,data,(size_t)count);
    return result < 0 ? -(int64_t)errno : (int64_t)result;
}
int64_t wd_file_seek(int64_t fd, int64_t offset, int64_t origin) {
    off_t result = lseek((int)fd,(off_t)offset,(int)origin);
    return result < 0 ? -(int64_t)errno : (int64_t)result;
}

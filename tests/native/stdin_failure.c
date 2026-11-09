/* Test-only launcher: real closed stdin, no simulated Wood I/O implementation. */
#ifdef _WIN32
#include <io.h>
#include <process.h>
int wmain(int argc,wchar_t **argv) {
    if(argc<2) return 2;
    _close(0);
    return (int)_wspawnv(_P_WAIT,argv[1],(const wchar_t *const *)(argv+1));
}
#else
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc<2) return 2;
    close(0);
    execv(argv[1],argv+1);
    return 2;
}
#endif

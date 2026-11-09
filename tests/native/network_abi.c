/* Compare the runtime's actual declarations to the host SDK, not duplicated
   test declarations. Compile-only: no Wood/C runtime independence claim. */
#define _POSIX_C_SOURCE 200809L
#include "../../runtime/native/managed.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define HOST_INFO ADDRINFOA
#else
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#define HOST_INFO struct addrinfo
#endif
#define WOOD_NATIVE_NETWORK_ABI_ONLY
#include "../../runtime/native/network.inc"
#define FIELD(a,b) _Static_assert(offsetof(NInfo,a)==offsetof(HOST_INFO,b),"addrinfo " #a)
FIELD(flags,ai_flags); FIELD(family,ai_family); FIELD(type,ai_socktype);
FIELD(protocol,ai_protocol); FIELD(length,ai_addrlen); FIELD(address,ai_addr);
FIELD(canonical,ai_canonname); FIELD(next,ai_next);
_Static_assert(sizeof(NInfo)==sizeof(HOST_INFO),"addrinfo size");
_Static_assert(sizeof(NAddress)==sizeof(struct sockaddr_storage),"address storage");
_Static_assert(N_INET6==AF_INET6 && N_NUMERICSERV==AI_NUMERICSERV,"resolver constants");
_Static_assert(N_SOCKET==SOL_SOCKET && N_ERROR==SO_ERROR,"socket constants");
_Static_assert(N_IPV6==IPPROTO_IPV6 && N_V6ONLY==IPV6_V6ONLY,"IPv6 policy");
#ifdef _WIN32
_Static_assert(sizeof(NSocket)==sizeof(SOCKET) && sizeof(NSet)==sizeof(fd_set),"Windows socket/select ABI");
_Static_assert(sizeof(NTime)==sizeof(struct timeval),"Windows timeval");
_Static_assert(N_REUSE==SO_EXCLUSIVEADDRUSE && N_SEND==SD_SEND,"Windows socket policy");
_Static_assert(0x8004667e==FIONBIO,"Windows nonblocking ioctl");
_Static_assert(sizeof(WSADATA)<=512 && _Alignof(WSADATA)<=8,"WSAStartup output bound");
#else
_Static_assert(sizeof(NSocket)==sizeof(int) && sizeof(NPoll)==sizeof(struct pollfd),"POSIX socket/poll ABI");
_Static_assert(N_REUSE==SO_REUSEADDR && N_SEND==SHUT_WR,"POSIX socket policy");
_Static_assert(O_NONBLOCK==2048 && FD_CLOEXEC==1 && F_GETFL==3 && F_SETFL==4 && F_GETFD==1 && F_SETFD==2,"fcntl constants");
_Static_assert(MSG_NOSIGNAL==0x4000 && POLLOUT==4 && POLLIN==1,"POSIX send/readiness constants");
#endif

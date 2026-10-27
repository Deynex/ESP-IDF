#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
typedef int socklen_t;
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#ifndef DNS_HOST_TEST_PORT
#define DNS_HOST_TEST_PORT 1053
#endif
/* Only the host adapter translates DNS endpoints; firmware still uses UDP/53. */
static const struct sockaddr *host_endpoint(const struct sockaddr *addr, socklen_t len,
                                            struct sockaddr_in *mapped) {
    if (addr && len == sizeof(*mapped) && addr->sa_family == AF_INET) {
        *mapped = *(const struct sockaddr_in *)addr;
        if (mapped->sin_port == htons(53)) {
            mapped->sin_port = htons(DNS_HOST_TEST_PORT);
            return (const struct sockaddr *)mapped;
        }
    }
    return addr;
}
static void host_set_errno(void) {
    const int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EWOULDBLOCK : e == WSAEINTR ? EINTR : e;
}
static int host_socket(int af, int type, int proto) {
    SOCKET value = socket(af,type,proto);
    if (value == INVALID_SOCKET) { host_set_errno(); return -1; }
    if (value > INT_MAX) abort();
    return (int)value;
}
static int host_recv(int fd, void *buf, size_t len, int flags) {
    int result = recv((SOCKET)fd,(char *)buf,(int)len,flags);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
static int host_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *alen) {
    int result = recvfrom((SOCKET)fd,(char *)buf,(int)len,flags,addr,alen);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
static int host_send(int fd, const void *buf, size_t len, int flags) {
    int result = send((SOCKET)fd,(const char *)buf,(int)len,flags);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
static int host_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t alen) {
    int result = sendto((SOCKET)fd,(const char *)buf,(int)len,flags,addr,alen);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
static int host_connect(int fd,const struct sockaddr *addr,socklen_t alen) {
    struct sockaddr_in mapped;
    int result = connect((SOCKET)fd,host_endpoint(addr,alen,&mapped),alen);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
static int host_bind(int fd,const struct sockaddr *addr,socklen_t alen) {
    struct sockaddr_in mapped;
    int result = bind((SOCKET)fd,host_endpoint(addr,alen,&mapped),alen);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
static int host_select(int n,fd_set *r,fd_set *w,fd_set *x,struct timeval *tv) {
    int result = select(n,r,w,x,tv);
    if (result == SOCKET_ERROR) host_set_errno();
    return result;
}
#define socket host_socket
#define recv host_recv
#define recvfrom host_recvfrom
#define send host_send
#define sendto host_sendto
#define connect host_connect
#define bind host_bind
#define select host_select
#define close(fd) closesocket((SOCKET)(fd))

/*
 * platform.cpp
 */
#include "adapter/platform.h"
#include <cstring>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
static WSADATA wsa_data;
#endif

namespace framework {

ThreadId get_current_thread_id() {
#ifdef PLATFORM_WINDOWS
    return GetCurrentThreadId();
#else
    return (uint64_t)pthread_self();
#endif
}

int close_socket(SocketHandle sock) {
#ifdef PLATFORM_WINDOWS
    return closesocket(sock);
#else
    return close(sock);
#endif
}

int set_nonblocking(SocketHandle sock) {
#ifdef PLATFORM_WINDOWS
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

int set_reuseaddr(SocketHandle sock) {
    int opt = 1;
#ifdef PLATFORM_WINDOWS
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
}

int set_nodelay(SocketHandle sock) {
    int opt = 1;
#ifdef PLATFORM_WINDOWS
    return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
#else
    return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif
}

int bind_and_listen(SocketHandle sock, int port) {
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        return -1;
    if (listen(sock, SOMAXCONN) < 0)
        return -1;
    return 0;
}

SocketHandle accept_connection(SocketHandle server_sock) {
#ifdef PLATFORM_WINDOWS
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    return accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
#else
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    return accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
#endif
}

uint64_t get_tick_ms() {
#ifdef PLATFORM_WINDOWS
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int platform_init() {
#ifdef PLATFORM_WINDOWS
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        return -1;
#endif
    return 0;
}

int platform_shutdown() {
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
    return 0;
}

} // namespace framework
/*
 * platform.cpp
 */
#include "adapter/platform.h"
#include "logger/logger.h"
#include <cstring>

#ifdef PLATFORM_WINDOWS
static WSAData g_wsa_data;
static bool g_wsa_inited = false;
#endif

namespace framework {

// ============ 线程 ID ============
ThreadId get_current_thread_id() {
#ifdef PLATFORM_WINDOWS
    return (ThreadId)GetCurrentThreadId();
#else
    return (ThreadId)pthread_self();
#endif
}

// ============ Socket ============
int close_socket(SocketHandle sock) {
    if (sock == INVALID_SOCKET) return 0;
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
    memset(&addr, 0, sizeof(addr));
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
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
#ifdef PLATFORM_WINDOWS
    return accept(server_sock, (struct sockaddr*)&client_addr, (int*)&addr_len);
#else
    return accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
#endif
}

// ============ 文件操作 ============
int file_open(const char* path, int flags) {
#ifdef PLATFORM_WINDOWS
    DWORD access = 0;
    if (flags & 0x01) access |= GENERIC_READ;
    if (flags & 0x02) access |= GENERIC_WRITE;
    DWORD create_flags = (flags & 0x40) ? CREATE_ALWAYS : OPEN_EXISTING;
    HANDLE h = CreateFileA(path, access, 0, nullptr, create_flags, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    return (int)(intptr_t)h;
#else
    int oflag = 0;
    if (flags & 0x01) oflag |= O_RDONLY;
    if (flags & 0x02) oflag |= O_WRONLY;
    if (flags & 0x04) oflag |= O_RDWR;
    if (flags & 0x40) oflag |= O_CREAT | O_TRUNC;
    return open(path, oflag, 0644);
#endif
}

int file_close(int fd) {
#ifdef PLATFORM_WINDOWS
    return CloseHandle((HANDLE)(intptr_t)fd) ? 0 : -1;
#else
    return close(fd);
#endif
}

int file_read(int fd, void* buf, size_t len) {
#ifdef PLATFORM_WINDOWS
    DWORD readn = 0;
    BOOL ok = ReadFile((HANDLE)(intptr_t)fd, buf, (DWORD)len, &readn, nullptr);
    return ok ? (int)readn : -1;
#else
    return (int)read(fd, buf, len);
#endif
}

int file_write(int fd, const void* buf, size_t len) {
#ifdef PLATFORM_WINDOWS
    DWORD written = 0;
    BOOL ok = WriteFile((HANDLE)(intptr_t)fd, buf, (DWORD)len, &written, nullptr);
    return ok ? (int)written : -1;
#else
    return (int)write(fd, buf, len);
#endif
}

int64_t file_seek(int fd, int64_t off, int whence) {
#ifdef PLATFORM_WINDOWS
    LARGE_INTEGER li;
    li.QuadPart = off;
    li.LowPart = SetFilePointer((HANDLE)(intptr_t)fd, li.LowPart, &li.HighPart, (DWORD)whence);
    return li.QuadPart;
#else
    return lseek(fd, off, whence);
#endif
}

int64_t file_tell(int fd) {
    return file_seek(fd, 0, SEEK_CUR);
}

// ============ 时间 ============
uint64_t get_tick_ms() {
#ifdef PLATFORM_WINDOWS
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

uint64_t get_tick_us() {
#ifdef PLATFORM_WINDOWS
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (uint64_t)(cnt.QuadPart * 1000000 / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

int64_t get_real_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void sleep_us(int us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

// ============ 进程/线程工具 ============
int get_process_id() {
#ifdef PLATFORM_WINDOWS
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

int get_thread_id() {
    return (int)get_current_thread_id();
}

bool set_thread_name(const std::string& name) {
#ifdef PLATFORM_WINDOWS
    // Windows 不直接支持设置线程名
    (void)name;
    return false;
#else
    return pthread_setname_np(pthread_self(), name.c_str()) == 0;
#endif
}

// ============ 平台初始化 ============
int platform_init() {
#ifdef PLATFORM_WINDOWS
    if (g_wsa_inited) return 0;
    if (WSAStartup(MAKEWORD(2, 2), &g_wsa_data) != 0)
        return -1;
    g_wsa_inited = true;
#endif
    return 0;
}

int platform_shutdown() {
#ifdef PLATFORM_WINDOWS
    if (!g_wsa_inited) return 0;
    WSACleanup();
    g_wsa_inited = false;
#endif
    return 0;
}

// ============ FD 工具 ============
int close_fd(int fd) {
    if (fd < 0) return 0;
#ifdef PLATFORM_WINDOWS
    return CloseHandle((HANDLE)(intptr_t)fd) ? 0 : -1;
#else
    return close(fd);
#endif
}

int create_eventfd() {
#ifdef PLATFORM_WINDOWS
    return -1; // 不支持
#else
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#endif
}

int create_timerfd() {
#ifdef PLATFORM_WINDOWS
    return -1; // 不支持
#else
    return timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
#endif
}

} // namespace framework
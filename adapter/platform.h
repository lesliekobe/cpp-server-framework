/*
 * platform.h - 跨平台系统适配层
 *
 * 封装 Linux / Windows 差异：
 *   - 线程
 *   - Socket API
 *   - 文件操作
 *   - 时间
 *   - 原子操作
 *   - 进程/线程 ID
 */

#pragma once

#ifdef _WIN32
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_LINUX 0
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET SocketHandle;
    #define INVALID_SOCKET (SOCKET)(~0)
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 1
    #include <sys/socket.h>
    #include <sys/epoll.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <pthread.h>
    #include <sys/timerfd.h>
    #include <sys/eventfd.h>
    typedef int SocketHandle;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR_CODE errno
    #define SOCKET_ERROR (-1)
#endif

#include <cstdint>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>

namespace framework {

// ============ 线程 ID ============
ThreadId get_current_thread_id();

// ============ Socket 兼容 ============
int close_socket(SocketHandle sock);
int set_nonblocking(SocketHandle sock);
int set_reuseaddr(SocketHandle sock);
int set_nodelay(SocketHandle sock);
int bind_and_listen(SocketHandle sock, int port);
SocketHandle accept_connection(SocketHandle server_sock);

inline int get_last_socket_error() {
#ifdef PLATFORM_WINDOWS
    return WSAGetLastError();
#else
    return errno;
#endif
}

// ============ 文件操作 ============
int  file_open(const char* path, int flags);
int  file_close(int fd);
int  file_read(int fd, void* buf, size_t len);
int  file_write(int fd, const void* buf, size_t len);
int64_t file_seek(int fd, int64_t off, int whence);
int64_t file_tell(int fd);

// ============ 时间工具 ============
uint64_t get_tick_ms();    // 启动以来毫秒数（单调时钟）
uint64_t get_tick_us();    // 微秒
int64_t  get_real_time_ms(); // wall clock ms
void     sleep_ms(int ms);
void     sleep_us(int us);

// ============ Atomic 完整封装 ============
template<typename T>
T atomic_load(std::atomic<T>* v) { return v->load(std::memory_order_acquire); }
template<typename T>
void atomic_store(std::atomic<T>* v, T val) { v->store(val, std::memory_order_release); }
template<typename T>
T atomic_exchange(std::atomic<T>* v, T val) { return v->exchange(val); }
template<typename T>
bool atomic_compare_exchange(std::atomic<T>* v, T* expected, T desired) {
    return v->compare_exchange_weak(*expected, desired);
}

// ============ 进程工具 ============
int  get_process_id();
int  get_thread_id();
bool set_thread_name(const std::string& name);

// ============ 平台初始化（Windows WSA） ============
int platform_init();
int platform_shutdown();

// ============ 文件描述符工具 ============
int create_eventfd();     // Linux eventfd（用于信号/唤醒）
int create_timerfd();      // Linux timerfd
int close_fd(int fd);

} // namespace framework
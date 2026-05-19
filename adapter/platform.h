/*
 * platform.h - 跨平台系统适配层
 *
 * 封装 Linux / Windows 差异：
 *   - 线程
 *   - Socket API
 *   - 时间
 *   - 原子操作
 */

#pragma once

#ifdef _WIN32
    #define PLATFORM_WINDOWS 1
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET SocketHandle;
#else
    #define PLATFORM_LINUX 1
    #include <sys/socket.h>
    #include <sys/epoll.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    typedef int SocketHandle;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
#endif

#include <cstdint>
#include <thread>
#include <chrono>
#include <atomic>

namespace framework {

// ============ 线程 ============
#ifdef PLATFORM_WINDOWS
using ThreadHandle = std::thread*;
using ThreadId = uint64_t;
#else
using ThreadHandle = std::thread*;
using ThreadId = uint64_t;
#endif

ThreadId get_current_thread_id();

// ============ Socket 兼容 ============
int close_socket(SocketHandle sock);
int set_nonblocking(SocketHandle sock);
int set_reuseaddr(SocketHandle sock);
int set_nodelay(SocketHandle sock);
int bind_and_listen(SocketHandle sock, int port);
SocketHandle accept_connection(SocketHandle server_sock);

// ============ 时间工具 ============
uint64_t get_tick_ms();   // 启动以来毫秒数
void     sleep_ms(int ms);

// ============ 原子操作 ============
template<typename T>
T atomic_load(std::atomic<T>* v) { return v->load(std::memory_order_acquire); }

template<typename T>
void atomic_store(std::atomic<T>* v, T val) { v->store(val, std::memory_order_release); }

// ============ 平台初始化（Windows WSA） ============
int platform_init();
int platform_shutdown();

} // namespace framework
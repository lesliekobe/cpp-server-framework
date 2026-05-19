/*
 * tcp_server.h - TCP 服务端
 *
 * 特性：
 * - Linux: Epoll 事件驱动
 * - Windows: Select（兼容封装）
 * - 每连接独立读写线程（线程安全）
 * - 心跳检测：空闲超时断开
 * - 自动投递给业务回调
 */

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "tcp_connection.h"

namespace framework {

class TcpServer {
public:
    // port: 监听端口， threads: 工作线程数， idle_timeout_ms: 心跳超时（毫秒）
    TcpServer(int port, int threads = 4, uint64_t idle_timeout_ms = 30000);
    ~TcpServer();

    // 禁止拷贝
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // 启动/停止
    bool start();
    void stop();

    // 业务回调设置
    void set_message_handler(OnMessage fn)      { on_message_ = std::move(fn); }
    void set_close_handler(OnClose fn)          { on_close_ = std::move(fn); }
    void set_connected_handler(OnConnected fn)  { on_connected_ = std::move(fn); }

    // 会话操作
    bool         broadcast(uint16_t cmd, const std::string& data);
    bool         send_to(int64_t session_id, uint16_t cmd, const std::string& data);
    void         close_session(int64_t session_id);
    size_t       session_count() const;
    bool         is_running() const { return running_.load(std::memory_order_acquire); }

    // 心跳检测间隔（毫秒）
    void         set_heartbeat_interval(uint64_t ms) { heartbeat_interval_ms_ = ms; }
    void         set_idle_timeout(uint64_t ms)       { idle_timeout_ms_ = ms; }

private:
    void accept_loop();           // Accept 主循环
    void heartbeat_loop();        // 心跳检测
    int  epoll_wait_intr(int epfd, struct epoll_event* events, int maxevents, int timeout);

    int                    port_;
    int                    threads_;
    uint64_t               heartbeat_interval_ms_{ 10000 };
    uint64_t               idle_timeout_ms_{ 30000 };
    SocketHandle           server_sock_{ INVALID_SOCKET };

    std::atomic<bool>      running_{ false };
    std::atomic<bool>      stopped_{ false };

    std::unordered_map<int64_t, TcpConnectionPtr> connections_;
    mutable std::mutex     conn_mutex_;
    int64_t                next_session_id_{ 1 };

    // 跨平台 epoll 实现
    struct EpollData;
    std::unique_ptr<EpollData> epoll_data_;

    // 回调
    OnMessage       on_message_;
    OnClose         on_close_;
    OnConnected     on_connected_;
};

} // namespace framework
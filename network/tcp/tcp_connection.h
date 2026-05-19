/*
 * tcp_connection.h - 单个 TCP 连接会话
 *
 * 负责：读写缓冲区、拆包、粘包处理
 * 线程安全：每个连接一个读写线程，保证顺序
 */

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include "adapter/platform.h"
#include "protocol/packet.h"

namespace framework {

class TcpConnection;
class TcpServer;

// 连接状态
enum class ConnStatus {
    CONNECTED,
    RECONNECTING,
    CLOSED
};

// 连接事件回调
using OnMessage   = std::function<void(int64_t session_id, const protocol::Packet&)>;
using OnClose     = std::function<void(int64_t session_id)>;
using OnConnected = std::function<void(int64_t session_id)>;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(SocketHandle sock, int64_t session_id, TcpServer* server);
    ~TcpConnection();

    // 禁止拷贝
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    int64_t      session_id() const { return session_id_; }
    ConnStatus   status() const { return status_; }
    SocketHandle socket() const { return sock_; }

    // 发送数据包
    bool send_packet(uint16_t cmd, const std::string& data);
    bool send_raw(const uint8_t* data, size_t len);

    // 启动读写循环
    void start();
    void close();

    // 心跳检测：上次收到数据时间
    uint64_t last_recv_time() const { return last_recv_time_; }
    bool is_idle(uint64_t timeout_ms) const;

    void set_on_message(OnMessage fn)    { on_message_ = std::move(fn); }
    void set_on_close(OnClose fn)        { on_close_ = std::move(fn); }
    void set_on_connected(OnConnected fn) { on_connected_ = std::move(fn); }

private:
    void read_loop();      // 读循环：收包、拆包
    void write_loop();     // 写循环：发送队列
    bool process_read();   // 处理单个 read，调协议解析

    SocketHandle          sock_;
    int64_t               session_id_;
    TcpServer*            server_;
    ConnStatus            status_{ ConnStatus::CONNECTED };

    // 读缓冲（防粘包）
    std::vector<uint8_t> read_buf_;

    // 写队列 + 写互斥
    std::queue<std::vector<uint8_t>> write_queue_;
    std::mutex                       write_mutex_;
    std::condition_variable          write_cv_;

    uint64_t        last_recv_time_{ 0 };
    bool            closed_{ false };

    OnMessage       on_message_;
    OnClose         on_close_;
    OnConnected     on_connected_;
};

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

} // namespace framework
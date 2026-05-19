/*
 * tcp_client.h - TCP 客户端（自动重连）
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include "adapter/platform.h"

namespace framework {

class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    using OnMessageCb = std::function<void(const std::string& data)>;
    using OnCloseCb   = std::function<void()>;
    using OnConnCb    = std::function<void()>;

    TcpClient(const std::string& host, int port);
    ~TcpClient();

    // 连接服务器
    bool connect();
    void disconnect();

    // 发送数据
    bool send(uint16_t cmd, const std::string& data);
    bool send_raw(const std::string& data);

    // 回调设置
    void set_on_message(OnMessageCb fn)  { on_message_ = std::move(fn); }
    void set_on_close(OnCloseCb fn)       { on_close_ = std::move(fn); }
    void set_on_connected(OnConnCb fn)   { on_connected_ = std::move(fn); }

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

private:
    void read_loop();
    void write_loop();
    bool try_connect();
    void reconnect_loop();

    std::string       host_;
    int               port_;
    SocketHandle      sock_{ INVALID_SOCKET };
    std::atomic<bool> connected_{ false };
    std::atomic<bool> running_{ false };

    std::vector<uint8_t> read_buf_;
    std::queue<std::vector<uint8_t>> write_queue_;
    std::mutex              write_mutex_;
    std::condition_variable write_cv_;
    std::thread             read_thread_;
    std::thread             write_thread_;
    std::thread             reconnect_thread_;

    OnMessageCb    on_message_;
    OnCloseCb       on_close_;
    OnConnCb        on_connected_;
};

} // namespace framework
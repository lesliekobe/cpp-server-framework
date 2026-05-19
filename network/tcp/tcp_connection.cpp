/*
 * tcp_connection.cpp
 */
#include "tcp/tcp_connection.h"
#include "tcp/tcp_server.h"
#include "logger/logger.h"
#include <algorithm>

namespace framework {

TcpConnection::TcpConnection(SocketHandle sock, int64_t session_id, TcpServer* server)
    : sock_(sock), session_id_(session_id), server_(server)
{
    last_recv_time_ = get_tick_ms();
}

TcpConnection::~TcpConnection() {
    close();
}

bool TcpConnection::send_raw(const uint8_t* data, size_t len) {
    if (closed_ || status_ != ConnStatus::CONNECTED) return false;
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(sock_, (const char*)data + sent, (int)(len - sent), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += n;
    }
    return true;
}

bool TcpConnection::send_packet(uint16_t cmd, const std::string& data) {
    auto buf = protocol::encode(protocol::Packet(cmd, data));
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(std::move(buf));
    write_cv_.notify_one();
    return true;
}

void TcpConnection::start() {
    if (on_connected_) on_connected_(session_id_);

    // 启动读线程
    std::thread([self = shared_from_this()] {
        self->read_loop();
    }).detach();

    // 启动写线程
    std::thread([self = shared_from_this()] {
        self->write_loop();
    }).detach();
}

void TcpConnection::read_loop() {
    uint8_t tmp[8192];

    while (!closed_) {
        int n = ::recv(sock_, (char*)tmp, sizeof(tmp), 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }

        last_recv_time_ = get_tick_ms();

        // 追加到读缓冲
        read_buf_.insert(read_buf_.end(), tmp, tmp + n);

        // 循环解析（可能一次读到多个包）
        while (process_read()) {
            // 继续解析下一个包
        }
    }

    close();
    if (on_close_) on_close_(session_id_);
}

bool TcpConnection::process_read() {
    protocol::Packet pkt;
    size_t consumed = 0;
    if (!protocol::decode_from(read_buf_.data(), read_buf_.size(), pkt, consumed)) {
        return false;
    }
    read_buf_.erase(read_buf_.begin(), read_buf_.begin() + consumed);

    if (on_message_) {
        on_message_(session_id_, pkt);
    }
    return true;
}

void TcpConnection::write_loop() {
    while (!closed_) {
        std::vector<uint8_t> msg;
        {
            std::unique_lock<std::mutex> lock(write_mutex_);
            write_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return closed_ || !write_queue_.empty();
            });
            if (closed_ && write_queue_.empty()) break;
            if (!write_queue_.empty()) {
                msg = std::move(write_queue_.front());
                write_queue_.pop();
            }
        }
        if (!msg.empty()) {
            if (!send_raw(msg.data(), msg.size())) break;
        }
    }
}

void TcpConnection::close() {
    if (closed_) return;
    closed_ = true;
    status_ = ConnStatus::CLOSED;
    write_cv_.notify_one();
    close_socket(sock_);
    sock_ = INVALID_SOCKET;
}

bool TcpConnection::is_idle(uint64_t timeout_ms) const {
    return (get_tick_ms() - last_recv_time_) > timeout_ms;
}

} // namespace framework
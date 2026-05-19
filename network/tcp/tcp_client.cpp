/*
 * tcp_client.cpp
 */
#include "tcp/tcp_client.h"
#include "logger/logger.h"
#include "protocol/packet.h"
#include <algorithm>

namespace framework {

TcpClient::TcpClient(const std::string& host, int port)
    : host_(host), port_(port) {}

TcpClient::~TcpClient() {
    disconnect();
}

bool TcpClient::try_connect() {
    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == INVALID_SOCKET) return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (::connect(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    set_nodelay(sock_);
    connected_.store(true, std::memory_order_release);
    return true;
}

void TcpClient::reconnect_loop() {
    while (running_.load(std::memory_order_acquire)) {
        sleep_ms(3000);
        if (!connected_.load(std::memory_order_acquire) && running_.load(std::memory_order_acquire)) {
            if (try_connect()) {
                LOG_INFO("client reconnected to %s:%d", host_.c_str(), port_);
                if (on_connected_) on_connected_();
            }
        }
    }
}

bool TcpClient::connect() {
    if (!running_.load(std::memory_order_acquire)) {
        running_.store(true, std::memory_order_release);
        reconnect_thread_ = std::thread([this] { reconnect_loop(); });
    }

    if (!try_connect()) {
        LOG_WARN("client connect to %s:%d failed", host_.c_str(), port_);
        return false;
    }

    LOG_INFO("client connected to %s:%d", host_.c_str(), port_);

    read_thread_  = std::thread([this] { read_loop(); });
    write_thread_ = std::thread([this] { write_loop(); });

    if (on_connected_) on_connected_();
    return true;
}

void TcpClient::disconnect() {
    if (!running_.load(std::memory_order_acquire)) return;
    running_.store(false, std::memory_order_release);

    connected_.store(false, std::memory_order_release);

    if (sock_ != INVALID_SOCKET) {
        close_socket(sock_);
        sock_ = INVALID_SOCKET;
    }

    write_cv_.notify_all();
    if (write_thread_.joinable()) write_thread_.join();
    if (read_thread_.joinable()) read_thread_.join();
    if (reconnect_thread_.joinable()) reconnect_thread_.join();

    if (on_close_) on_close_();
}

bool TcpClient::send(uint16_t cmd, const std::string& data) {
    if (!connected_.load(std::memory_order_acquire)) return false;
    auto buf = protocol::encode(protocol::Packet(cmd, data));
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(std::move(buf));
    write_cv_.notify_one();
    return true;
}

bool TcpClient::send_raw(const std::string& data) {
    if (!connected_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lock(write_mutex_);
    auto& q = write_queue_;
    std::vector<uint8_t> v(data.begin(), data.end());
    q.push(std::move(v));
    write_cv_.notify_one();
    return true;
}

void TcpClient::read_loop() {
    uint8_t tmp[8192];
    while (connected_.load(std::memory_order_acquire)) {
        int n = ::recv(sock_, (char*)tmp, sizeof(tmp), 0);
        if (n <= 0) {
            connected_.store(false, std::memory_order_release);
            break;
        }
        read_buf_.insert(read_buf_.end(), tmp, tmp + n);

        // 解析所有完整包
        while (read_buf_.size() >= 4) {
            uint32_t total_len_be;
            std::memcpy(&total_len_be, read_buf_.data(), 4);
            uint32_t total_len = ntohl(total_len_be);
            if (read_buf_.size() < total_len) break;

            size_t data_len = total_len - 6;
            std::string msg((char*)read_buf_.data() + 6, data_len);
            read_buf_.erase(read_buf_.begin(), read_buf_.begin() + total_len);

            if (on_message_) on_message_(msg);
        }
    }
    connected_.store(false, std::memory_order_release);
    if (on_close_) on_close_();
}

void TcpClient::write_loop() {
    while (connected_.load(std::memory_order_acquire)) {
        std::vector<uint8_t> msg;
        {
            std::unique_lock<std::mutex> lock(write_mutex_);
            write_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !connected_.load(std::memory_order_acquire) || !write_queue_.empty();
            });
            if (!connected_.load(std::memory_order_acquire) && write_queue_.empty()) break;
            if (!write_queue_.empty()) {
                msg = std::move(write_queue_.front());
                write_queue_.pop();
            }
        }
        if (!msg.empty()) {
            size_t sent = 0;
            while (sent < msg.size()) {
                int n = ::send(sock_, (const char*)msg.data() + sent, (int)(msg.size() - sent), 0);
                if (n <= 0) {
                    connected_.store(false, std::memory_order_release);
                    break;
                }
                sent += n;
            }
        }
    }
}

} // namespace framework
/*
 * tcp_server.cpp
 */
#include "tcp/tcp_server.h"
#include "logger/logger.h"
#include <algorithm>
#include <cstring>

#ifdef PLATFORM_WINDOWS
// Windows: 使用 select
#include <ws2tcpip.h>
#else
// Linux: 使用 epoll
#include <sys/epoll.h>
#endif

namespace framework {

struct TcpServer::EpollData {
#ifdef PLATFORM_LINUX
    int epfd = -1;
#endif
};

// ==================== 跨平台 epoll ====================
#ifdef PLATFORM_LINUX

void TcpServer::heartbeat_loop() {
    while (running_.load(std::memory_order_acquire)) {
        sleep_ms((int)heartbeat_interval_ms_);

        std::lock_guard<std::mutex> lock(conn_mutex_);
        for (auto& kv : connections_) {
            auto& conn = kv.second;
            if (conn->is_idle(idle_timeout_ms_)) {
                LOG_WARN("session %ld idle timeout, closing", conn->session_id());
                conn->close();
            }
        }
    }
}

int TcpServer::epoll_wait_intr(int epfd, struct epoll_event* events, int maxevents, int timeout) {
    return epoll_wait(epfd, events, maxevents, timeout);
}

void TcpServer::accept_loop() {
    epoll_data_ = std::make_unique<EpollData>();
    epoll_data_->epfd = epoll_create1(0);
    if (epoll_data_->epfd < 0) {
        LOG_ERROR("epoll_create1 failed");
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_sock_;
    epoll_ctl(epoll_data_->epfd, EPOLL_CTL_ADD, server_sock_, &ev);

    struct epoll_event events[64];

    while (running_.load(std::memory_order_acquire)) {
        int nfds = epoll_wait_intr(epoll_data_->epfd, events, 64, 500);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_sock_) {
                // Accept
                SocketHandle client_sock = accept_connection(server_sock_);
                if (client_sock != INVALID_SOCKET) {
                    set_nonblocking(client_sock);
                    set_nodelay(client_sock);
                    set_reuseaddr(client_sock);

                    int64_t sid;
                    TcpConnectionPtr conn;
                    {
                        std::lock_guard<std::mutex> lock(conn_mutex_);
                        sid = next_session_id_++;
                        conn = std::make_shared<TcpConnection>(client_sock, sid, this);
                        conn->set_on_message(on_message_);
                        conn->set_on_close([this](int64_t id) {
                            {
                                std::lock_guard<std::mutex> l(conn_mutex_);
                                connections_.erase(id);
                            }
                            if (on_close_) on_close_(id);
                        });
                        connections_[sid] = conn;
                    }
                    conn->start();

                    ev.events = EPOLLIN | EPOLLOUT;
                    ev.data.fd = client_sock;
                    epoll_ctl(epoll_data_->epfd, EPOLL_CTL_ADD, client_sock, &ev);
                }
            }
        }
    }
}

#else // Windows: 使用 select

void TcpServer::heartbeat_loop() {
    while (running_.load(std::memory_order_acquire)) {
        sleep_ms((int)heartbeat_interval_ms_);
        std::lock_guard<std::mutex> lock(conn_mutex_);
        for (auto& kv : connections_) {
            if (kv.second->is_idle(idle_timeout_ms_)) {
                kv.second->close();
            }
        }
    }
}

void TcpServer::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        sleep_ms(100);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_sock_, &read_fds);
        struct timeval tv = { 0, 100000 };

        int ret = select(0, &read_fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(server_sock_, &read_fds)) {
            SocketHandle client_sock = accept_connection(server_sock_);
            if (client_sock != INVALID_SOCKET) {
                set_nonblocking(client_sock);
                set_nodelay(client_sock);

                int64_t sid;
                TcpConnectionPtr conn;
                {
                    std::lock_guard<std::mutex> lock(conn_mutex_);
                    sid = next_session_id_++;
                    conn = std::make_shared<TcpConnection>(client_sock, sid, this);
                    conn->set_on_message(on_message_);
                    conn->set_on_close([this](int64_t id) {
                        std::lock_guard<std::mutex> l(conn_mutex_);
                        connections_.erase(id);
                        if (on_close_) on_close_(id);
                    });
                    connections_[sid] = conn;
                }
                conn->start();
            }
        }
    }
}

#endif

// ==================== TcpServer 主体 ====================
TcpServer::TcpServer(int port, int threads, uint64_t idle_timeout_ms)
    : port_(port), threads_(threads), idle_timeout_ms_(idle_timeout_ms)
{}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (running_.load(std::memory_order_acquire)) return true;

    platform_init();

    server_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_ == INVALID_SOCKET) {
        LOG_ERROR("socket create failed");
        return false;
    }

    set_reuseaddr(server_sock_);
    if (bind_and_listen(server_sock_, port_) < 0) {
        LOG_ERROR("bind/listen failed on port %d", port_);
        return false;
    }

    set_nonblocking(server_sock_);

    running_.store(true, std::memory_order_release);

    // 启动 accept 线程
    std::thread([this] { accept_loop(); }).detach();

    // 启动心跳线程
    std::thread([this] { heartbeat_loop(); }).detach();

    LOG_INFO("TcpServer started on port %d", port_);
    return true;
}

void TcpServer::stop() {
    if (!running_.load(std::memory_order_acquire)) return;
    running_.store(false, std::memory_order_release);

    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        for (auto& kv : connections_) {
            kv.second->close();
        }
        connections_.clear();
    }

    close_socket(server_sock_);
    server_sock_ = INVALID_SOCKET;

#ifdef PLATFORM_LINUX
    if (epoll_data_ && epoll_data_->epfd >= 0) {
        close(epoll_data_->epfd);
        epoll_data_->epfd = -1;
    }
#endif

    platform_shutdown();
    LOG_INFO("TcpServer stopped");
}

bool TcpServer::broadcast(uint16_t cmd, const std::string& data) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto& kv : connections_) {
        kv.second->send_packet(cmd, data);
    }
    return true;
}

bool TcpServer::send_to(int64_t session_id, uint16_t cmd, const std::string& data) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = connections_.find(session_id);
    if (it != connections_.end()) {
        return it->second->send_packet(cmd, data);
    }
    return false;
}

void TcpServer::close_session(int64_t session_id) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = connections_.find(session_id);
    if (it != connections_.end()) {
        it->second->close();
        connections_.erase(it);
    }
}

size_t TcpServer::session_count() const {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    return connections_.size();
}

} // namespace framework
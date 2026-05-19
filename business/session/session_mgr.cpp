/*
 * session_mgr.cpp
 */
#include "session_mgr.h"
#include "adapter/platform.h"
#include "logger/logger.h"

namespace framework {

// ============ 全局 ============
static SessionMgr g_session_mgr;
SessionMgr* global_session_mgr() { return &g_session_mgr; }

SessionMgr::SessionMgr() = default;
SessionMgr::~SessionMgr() { shutdown(); }

bool SessionMgr::init() {
    LOG_INFO("SessionMgr: initialized");
    return true;
}

void SessionMgr::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    LOG_INFO("SessionMgr: shutdown");
}

int64_t SessionMgr::add(int socket_fd, const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t sid = next_sid_++;
    ConnectionContext ctx;
    ctx.session_id = sid;
    ctx.socket_fd = socket_fd;
    ctx.remote_ip = ip;
    ctx.remote_port = port;
    ctx.connect_time = get_tick_ms();
    ctx.last_msg_time = ctx.connect_time;
    ctx.heartbeat_count = 0;
    ctx.is_alive = true;

    sessions_[sid] = ctx;
    total_count_.fetch_add(1, std::memory_order_relaxed);

    size_t alive = sessions_.size();
    size_t peak = peak_count_.load(std::memory_order_relaxed);
    while (alive > peak && !peak_count_.compare_exchange_weak(peak, alive)) {}

    LOG_INFO("SessionMgr: add session sid=%lld ip=%s:%d (alive=%zu)",
             (long long)sid, ip.c_str(), port, sessions_.size());
    return sid;
}

void SessionMgr::remove(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    if (it != sessions_.end()) {
        LOG_INFO("SessionMgr: remove session sid=%lld", (long long)sid);
        sessions_.erase(it);
    }
}

bool SessionMgr::exists(int64_t sid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(sid) != sessions_.end();
}

size_t SessionMgr::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

ConnectionContext* SessionMgr::get(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    return it != sessions_.end() ? &it->second : nullptr;
}

const ConnectionContext* SessionMgr::get(int64_t sid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    return it != sessions_.end() ? &it->second : nullptr;
}

std::vector<int64_t> SessionMgr::all_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int64_t> ids;
    ids.reserve(sessions_.size());
    for (auto& kv : sessions_) ids.push_back(kv.first);
    return ids;
}

std::vector<ConnectionContext> SessionMgr::alive_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConnectionContext> result;
    result.reserve(sessions_.size());
    for (auto& kv : sessions_) result.push_back(kv.second);
    return result;
}

void SessionMgr::heartbeat(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    if (it != sessions_.end()) {
        it->second.last_msg_time = get_tick_ms();
        it->second.heartbeat_count++;
    }
}

bool SessionMgr::kick(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return false;

    LOG_INFO("SessionMgr: kick session sid=%lld", (long long)sid);
    if (it->second.socket_fd >= 0) {
        close_socket(it->second.socket_fd);
    }
    it->second.is_alive = false;
    sessions_.erase(it);
    return true;
}

void SessionMgr::mark_dead(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    if (it != sessions_.end()) {
        it->second.is_alive = false;
    }
}

void SessionMgr::broadcast(const std::vector<uint8_t>& data, Broadcaster broadcaster) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : sessions_) {
        if (kv.second.is_alive && broadcaster) {
            broadcaster(kv.first, data.data(), data.size());
        }
    }
}

bool SessionMgr::send_to(int64_t sid, const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end() || !it->second.is_alive) return false;
    if (!it->second.is_alive) return false;
    // 实际发送由 broadcaster 回调
    (void)data; (void)len;
    return true;
}

size_t SessionMgr::send_to_multiple(const std::vector<int64_t>& sids,
                                   const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t sent = 0;
    for (int64_t sid : sids) {
        auto it = sessions_.find(sid);
        if (it != sessions_.end() && it->second.is_alive) {
            sent++;
        }
    }
    (void)data; (void)len;
    return sent;
}

size_t SessionMgr::cleanup_timeout(uint64_t now_ms, uint64_t timeout_ms) {
    std::vector<int64_t> to_remove;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : sessions_) {
            if (!kv.second.is_alive) continue;
            if (kv.second.last_msg_time + timeout_ms < now_ms) {
                to_remove.push_back(kv.first);
            }
        }
    }

    size_t count = 0;
    for (int64_t sid : to_remove) {
        LOG_INFO("SessionMgr: timeout cleanup sid=%lld", (long long)sid);
        remove(sid);
        count++;
    }
    return count;
}

SessionMgr::Stats SessionMgr::stats() const {
    Stats s;
    s.alive_count = count();
    s.total_count = total_count_.load(std::memory_order_relaxed);
    s.peak_count = peak_count_.load(std::memory_order_relaxed);
    return s;
}

} // namespace framework
/*
 * session_mgr.cpp
 */
#include "session_mgr.h"
#include "adapter/platform.h"

namespace framework {

void SessionMgr::add(int64_t sid, const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    SessionInfo info;
    info.session_id = sid;
    info.remote_ip = ip;
    info.remote_port = port;
    info.connect_time = get_tick_ms();
    info.last_recv_time = get_tick_ms();
    info.is_alive = true;
    sessions_[sid] = info;
}

void SessionMgr::remove(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(sid);
}

bool SessionMgr::exists(int64_t sid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(sid) != sessions_.end();
}

size_t SessionMgr::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

SessionInfo* SessionMgr::get(int64_t sid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sid);
    if (it != sessions_.end()) return &it->second;
    return nullptr;
}

std::vector<int64_t> SessionMgr::all_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int64_t> ids;
    ids.reserve(sessions_.size());
    for (auto& kv : sessions_) ids.push_back(kv.first);
    return ids;
}

std::vector<SessionInfo> SessionMgr::alive_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionInfo> result;
    result.reserve(sessions_.size());
    for (auto& kv : sessions_) result.push_back(kv.second);
    return result;
}

} // namespace framework
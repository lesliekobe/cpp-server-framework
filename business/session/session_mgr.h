/*
 * session_mgr.h - 会话管理器
 *
 * 管理所有在线连接，支持广播和单点消息下发
 */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <string>

namespace framework {

// 轻量级会话信息
struct SessionInfo {
    int64_t  session_id;
    std::string remote_ip;
    int      remote_port;
    uint64_t connect_time;
    uint64_t last_recv_time;
    bool     is_alive;
};

class SessionMgr {
public:
    SessionMgr() = default;

    // 注册 / 注销会话
    void               add(int64_t sid, const std::string& ip, int port);
    void               remove(int64_t sid);
    bool               exists(int64_t sid) const;

    // 查询
    size_t             count() const;
    SessionInfo*       get(int64_t sid);
    std::vector<int64_t> all_ids() const;

    // 在线列表
    std::vector<SessionInfo> alive_sessions() const;

private:
    mutable std::mutex               mutex_;
    std::unordered_map<int64_t, SessionInfo> sessions_;
};

} // namespace framework
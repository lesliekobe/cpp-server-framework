/*
 * session_mgr.h - 会话 & 连接管理器
 *
 * 功能：
 *   - 连接上下文存储（session_id, socket, remote_ip/port, connect_time, last_msg_time, heartbeat_count）
 *   - 自动清理超时连接（后台定时扫描）
 *   - 踢出指定会话
 *   - 广播消息
 *   - 单点发送
 *   - 会话计数统计
 */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

namespace framework {

// ============ 连接上下文 ============
struct ConnectionContext {
    int64_t         session_id{0};
    int             socket_fd{-1};
    std::string     remote_ip;
    int             remote_port{0};
    uint64_t        connect_time{0};     // ms
    uint64_t        last_msg_time{0};   // ms
    uint32_t        heartbeat_count{0};
    bool            is_alive{true};
    void*           user_data{nullptr}; // 业务扩展

    // 标记更新
    void update_time() { last_msg_time = get_tick_ms(); }
};

// ============ 消息广播器 ============
using Broadcaster = std::function<void(int64_t sid, const uint8_t* data, size_t len)>;

// ============ SessionMgr 主类 ============
class SessionMgr {
public:
    SessionMgr();
    ~SessionMgr();

    // 禁止拷贝
    SessionMgr(const SessionMgr&) = delete;
    SessionMgr& operator=(const SessionMgr&) = delete;

    // ---- 生命周期 ----
    bool init();
    void shutdown();

    // ---- 注册 / 注销会话 ----
    int64_t add(int socket_fd, const std::string& ip, int port);
    void    remove(int64_t sid);
    bool    exists(int64_t sid) const;

    // ---- 查询 ----
    size_t               count() const;              // 在线数
    ConnectionContext*   get(int64_t sid);
    const ConnectionContext* get(int64_t sid) const;
    std::vector<int64_t> all_ids() const;
    std::vector<ConnectionContext> alive_sessions() const;

    // ---- 会话操作 ----
    // 更新心跳
    void               heartbeat(int64_t sid);

    // 踢出指定会话
    bool               kick(int64_t sid);

    // 标记会话死亡（不再收发）
    void               mark_dead(int64_t sid);

    // ---- 广播 & 单发 ----
    // 广播（通过 broadcaster 发送）
    void broadcast(const std::vector<uint8_t>& data, Broadcaster broadcaster);

    // 发送消息给指定会话
    bool send_to(int64_t sid, const uint8_t* data, size_t len);

    // 发送消息给多个会话
    size_t send_to_multiple(const std::vector<int64_t>& sids,
                            const uint8_t* data, size_t len);

    // ---- 超时清理 ----
    // 扫描超时连接并移除（定时调用，如每 30s）
    size_t cleanup_timeout(uint64_t now_ms, uint64_t timeout_ms);

    // ---- 统计 ----
    struct Stats {
        size_t   alive_count{0};
        size_t   total_count{0};
        size_t   peak_count{0};
    };
    Stats stats() const;

private:
    int64_t               next_sid_{1};
    mutable std::mutex     mutex_;
    std::unordered_map<int64_t, ConnectionContext> sessions_;

    std::atomic<size_t>   total_count_{0};
    std::atomic<size_t>   peak_count_{0};
};

// ============ 全局会话管理器 ============
SessionMgr* global_session_mgr();

} // namespace framework
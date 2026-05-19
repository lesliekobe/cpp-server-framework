/*
 * router.h - 消息路由器
 *
 * 根据命令字路由到对应业务处理函数
 */

#pragma once

#include <functional>
#include <unordered_map>
#include <mutex>
#include "protocol/packet.h"

namespace framework {

class Router {
public:
    // 业务回调类型
    using Handler = std::function<void(int64_t session_id, const std::string& data)>;

    // 注册路由
    void reg(uint16_t cmd, Handler handler);

    // 注销路由
    void unreg(uint16_t cmd);

    // 路由调用
    void route(int64_t session_id, const protocol::Packet& pkt);

    // 是否存在该命令路由
    bool has(uint16_t cmd) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, Handler> handlers_;
};

} // namespace framework
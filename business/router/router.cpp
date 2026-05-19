/*
 * router.cpp
 */
#include "router.h"

namespace framework {

void Router::reg(uint16_t cmd, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[cmd] = std::move(handler);
}

void Router::unreg(uint16_t cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(cmd);
}

void Router::route(int64_t session_id, const protocol::Packet& pkt) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(pkt.cmd);
    if (it != handlers_.end()) {
        it->second(session_id, pkt.data);
    }
}

bool Router::has(uint16_t cmd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handlers_.find(cmd) != handlers_.end();
}

} // namespace framework
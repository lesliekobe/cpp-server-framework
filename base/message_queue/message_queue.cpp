/*
 * message_queue.cpp
 */
#include "base/message_queue/message_queue.h"
#include "logger/logger.h"

namespace framework {

MessageQueue::MessageQueue() = default;
MessageQueue::~MessageQueue() = default;

size_t MessageQueue::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return queue_.size();
}

bool MessageQueue::empty() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return queue_.empty();
}

void MessageQueue::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!queue_.empty()) queue_.pop();
}

// ============ 全局 Hub ============
static SubscribeHub g_hub;

SubscribeHub* global_hub() { return &g_hub; }

} // namespace framework
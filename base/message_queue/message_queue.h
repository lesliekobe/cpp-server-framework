/*
 * message_queue.h - 进程内线程间消息队列
 *
 * 支持：
 *   - 多生产者-多消费者（MPMC）
 *   - 消息订阅/广播（基于 topic）
 *   - std::variant 多类型消息
 *   - 模板化，支持任意类型消息
 */

#pragma once

#include <variant>
#include <queue>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <typeindex>
#include <memory>

namespace framework {

// ============ 基础消息类型 ============
struct MessageBase {
    virtual ~MessageBase() = default;
    virtual std::type_index type() const = 0;
};

template<typename T>
struct Message : public MessageBase {
    T data;
    explicit Message(T d) : data(std::move(d)) {}
    std::type_index type() const override { return std::type_index(typeid(T)); }
};

// ============ 消息队列 ============
class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();

    // 发送消息（入队）
    template<typename T>
    void send(T&& msg) {
        auto m = std::make_shared<Message<std::decay_t<T>>>(std::forward<T>(msg));
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(m);
        }
        cv_.notify_one();
    }

    // 接收消息（阻塞直到有消息）
    template<typename T>
    bool recv(T& out, int timeout_ms = -1) {
        std::shared_ptr<MessageBase> msg;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            if (timeout_ms < 0) {
                cv_.wait(lock, [this]() { return !queue_.empty(); });
                msg = queue_.front();
                queue_.pop();
            } else {
                if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                 [this]() { return !queue_.empty(); })) {
                    return false; // timeout
                }
                msg = queue_.front();
                queue_.pop();
            }
        }

        if (!msg) return false;
        if (msg->type() != std::type_index(typeid(T))) return false;
        auto typed = std::static_pointer_cast<Message<T>>(msg);
        out = std::move(typed->data);
        return true;
    }

    // 非阻塞获取消息
    template<typename T>
    bool try_recv(T& out) {
        std::shared_ptr<MessageBase> msg;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.empty()) return false;
            msg = queue_.front();
            queue_.pop();
        }
        if (!msg) return false;
        if (msg->type() != std::type_index(typeid(T))) return false;
        auto typed = std::static_pointer_cast<Message<T>>(msg);
        out = std::move(typed->data);
        return true;
    }

    size_t size() const;
    bool   empty() const;
    void   clear();

private:
    mutable std::mutex                   mtx_;
    std::condition_variable              cv_;
    std::queue<std::shared_ptr<MessageBase>> queue_;
};

// ============ 订阅系统 ============
template<typename T>
using SubscribeCallback = std::function<void(const T&)>;

class SubscribeHub {
public:
    // 订阅 topic
    template<typename T>
    int subscribe(SubscribeCallback<T> cb) {
        int id = next_sub_id_++;
        std::lock_guard<std::mutex> lock(hub_mtx_);
        callbacks_[std::type_index(typeid(T))].push_back(
            std::make_pair(id, [cb](const std::any& a) {
                try { cb(std::any_cast<T>(a)); } catch (...) {}
            })
        );
        return id;
    }

    // 取消订阅
    template<typename T>
    bool unsubscribe(int sub_id) {
        std::lock_guard<std::mutex> lock(hub_mtx_);
        auto it = callbacks_.find(std::type_index(typeid(T)));
        if (it == callbacks_.end()) return false;
        auto& vec = it->second;
        for (size_t i = 0; i < vec.size(); ++i) {
            if (vec[i].first == sub_id) {
                vec.erase(vec.begin() + i);
                return true;
            }
        }
        return false;
    }

    // 发布（广播给所有订阅者）
    template<typename T>
    void publish(T&& msg) {
        std::any a = std::forward<T>(msg);
        std::lock_guard<std::mutex> lock(hub_mtx_);
        auto it = callbacks_.find(std::type_index(typeid(T)));
        if (it == callbacks_.end()) return;
        for (auto& p : it->second) {
            try { p.second(a); } catch (...) {}
        }
    }

private:
    struct AnyCallback {
        int id;
        std::function<void(const std::any&)> fn;
    };

    std::unordered_map<std::type_index, std::vector<AnyCallback>> callbacks_;
    std::mutex                   hub_mtx_;
    int                          next_sub_id_{1};
};

// ============ 全局 Hub ============
SubscribeHub* global_hub();

} // namespace framework
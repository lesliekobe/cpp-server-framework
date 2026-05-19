/*
 * condition.h - 条件变量封装
 */

#pragma once

#include <condition_variable>

namespace framework {

class ConditionVariable {
public:
    void wait(std::unique_lock<std::mutex>& lock) {
        cv_.wait(lock);
    }

    template<typename Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate pred) {
        cv_.wait(lock, pred);
    }

    void notify_one() { cv_.notify_one(); }
    void notify_all() { cv_.notify_all(); }

private:
    std::condition_variable cv_;
};

} // namespace framework
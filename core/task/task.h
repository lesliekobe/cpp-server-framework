/*
 * task.h - 异步任务封装
 *
 * - 支持 std::function + 回调
 * - 任务优先级（高/中/低）
 * - 可取消
 */

#pragma once

#include <functional>
#include <memory>
#include <atomic>

namespace framework {

enum class TaskPriority : int {
    LOW    = 0,
    NORMAL = 1,
    HIGH   = 2
};

class Task {
public:
    using Func = std::function<void()>;

    explicit Task(Func f, TaskPriority pri = TaskPriority::NORMAL)
        : func_(std::move(f)), priority_(pri), cancelled_(false) {}

    void execute() {
        if (!cancelled_.load(std::memory_order_acquire)) {
            if (func_) func_();
        }
    }

    void cancel() { cancelled_.store(true, std::memory_order_release); }
    bool is_cancelled() const { return cancelled_.load(std::memory_order_acquire); }

    TaskPriority priority() const { return priority_; }

    // 用于优先级队列比较
    bool operator<(const Task& other) const {
        return priority_ < other.priority_;  // 高优先级值大，优先被弹出
    }

private:
    Func                  func_;
    TaskPriority          priority_;
    std::atomic<bool>     cancelled_;
};

using TaskPtr = std::shared_ptr<Task>;

} // namespace framework
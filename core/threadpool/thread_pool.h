/*
 * thread_pool.h - 通用线程池
 *
 * 特性：
 * - 固定工作线程数
 * - 阻塞任务队列（有界/无界）
 * - 支持 std::function<void()> 任意任务
 * - 支持优雅停止（等队列清空）
 * - 支持有锁 / 无锁模式（默认有锁）
 */

#pragma once

#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include "task/task.h"

namespace framework {

class ThreadPool {
public:
    // threads: 工作线程数，queue_size: 队列最大长度（0=无界）
    explicit ThreadPool(int threads = 4, size_t queue_size = 0);
    ~ThreadPool();

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 投递任务（普通）
    template<typename F>
    auto append(F&& f) -> std::future<decltype(f())> {
        using RetType = decltype(f());
        auto task = std::make_shared<std::packaged_task<RetType()>>(std::forward<F>(f));
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task]() { (*task)(); });
        }
        task_cv_.notify_one();
        return task->get_future();
    }

    // 投递带优先级的任务
    void append_priority(TaskPtr task);

    // 优雅停止：等队列中任务全部执行完毕
    void shutdown();

    // 强制停止：不等队列
    void shutdown_now();

    size_t queued_count() const;
    int    worker_count() const { return threads_; }

private:
    void worker_loop();

    int                           threads_;
    size_t                        queue_size_;
    std::vector<std::thread>      workers_;
    std::queue<TaskPtr>           tasks_;          // 优先队列用 TaskPtr 比较
    mutable std::mutex            queue_mutex_;
    std::condition_variable       task_cv_;
    std::atomic<bool>             stop_{ false };
    std::atomic<bool>             shutdown_done_{ false };
};

} // namespace framework
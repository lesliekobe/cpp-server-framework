/*
 * thread_pool.cpp
 */
#include "threadpool/thread_pool.h"
#include "logger/logger.h"

namespace framework {

ThreadPool::ThreadPool(int threads, size_t queue_size)
    : threads_(threads), queue_size_(queue_size)
{
    // 预创建工作线程
    for (int i = 0; i < threads_; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    if (!stop_.load(std::memory_order_acquire)) {
        shutdown();
    }
}

void ThreadPool::append_priority(TaskPtr task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // 优先队列（std::priority_queue）可按优先级排序
        // 但 std::queue + vector 需要手动插入排序
        // 这里用简单方式：HIGH 插前面，LOW 插后面
        bool inserted = false;
        if (task->priority() == TaskPriority::HIGH) {
            // 找第一个非HIGH的位置
            std::queue<TaskPtr> tmp;
            while (!tasks_.empty() && tasks_.front()->priority() == TaskPriority::HIGH) {
                tmp.push(tasks_.front()); tasks_.pop();
            }
            tmp.push(task);
            while (!tasks_.empty()) {
                tmp.push(tasks_.front()); tasks_.pop();
            }
            tasks_ = std::move(tmp);
            inserted = true;
        }
        if (!inserted) {
            tasks_.push(task);
        }
    }
    task_cv_.notify_one();
}

void ThreadPool::worker_loop() {
    while (true) {
        TaskPtr task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            task_cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_acquire) || !tasks_.empty();
            });

            if (stop_.load(std::memory_order_acquire) && tasks_.empty()) {
                break;
            }

            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }

        if (task) {
            task->execute();
        }
    }
}

void ThreadPool::shutdown() {
    stop_.store(true, std::memory_order_release);
    task_cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    shutdown_done_.store(true, std::memory_order_release);
}

void ThreadPool::shutdown_now() {
    stop_.store(true, std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // 清空队列
        std::queue<TaskPtr> empty;
        tasks_.swap(empty);
    }
    task_cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

size_t ThreadPool::queued_count() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

} // namespace framework
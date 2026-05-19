/*
 * timer.cpp
 */
#include "base/timer/timer.h"
#include "logger/logger.h"
#include "adapter/platform.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

namespace framework {

// ============ Timer（无锁，适合单线程 event loop） ============
Timer::Timer() = default;
Timer::~Timer() = default;

int64_t Timer::add(int64_t after_ms, TimerCallback cb) {
    if (!cb || after_ms <= 0) return -1;

    int64_t now = (int64_t)get_tick_ms();
    TimerItem item;
    item.id = ++next_id_;
    item.expire_at = now + after_ms;
    item.interval_ms = 0;
    item.callback = std::move(cb);
    item.cancelled = false;

    items_.push_back(std::move(item));
    std::push_heap(items_.begin(), items_.end(),
                   [](const TimerItem& a, const TimerItem& b) {
                       return a.expire_at > b.expire_at; // min-heap
                   });
    return item.id;
}

int64_t Timer::add_repeating(int64_t interval_ms, TimerCallback cb) {
    if (!cb || interval_ms <= 0) return -1;

    int64_t now = (int64_t)get_tick_ms();
    TimerItem item;
    item.id = ++next_id_;
    item.expire_at = now + interval_ms;
    item.interval_ms = interval_ms;
    item.callback = std::move(cb);
    item.cancelled = false;

    items_.push_back(std::move(item));
    std::push_heap(items_.begin(), items_.end(),
                   [](const TimerItem& a, const TimerItem& b) {
                       return a.expire_at > b.expire_at;
                   });
    return item.id;
}

bool Timer::cancel(int64_t timer_id) {
    for (auto& item : items_) {
        if (item.id == timer_id) {
            item.cancelled = true;
            return true;
        }
    }
    return false;
}

void Timer::clear() {
    items_.clear();
}

int64_t Timer::poll(int64_t now_ms) {
    std::vector<TimerItem> fired;

    while (!items_.empty()) {
        TimerItem& top = items_.front();
        if (top.cancelled) {
            std::pop_heap(items_.begin(), items_.end(),
                         [](const TimerItem& a, const TimerItem& b) {
                             return a.expire_at > b.expire_at;
                         });
            items_.pop_back();
            continue;
        }
        if (top.expire_at > now_ms) break;

        std::pop_heap(items_.begin(), items_.end(),
                     [](const TimerItem& a, const TimerItem& b) {
                         return a.expire_at > b.expire_at;
                     });
        TimerItem t = std::move(items_.back());
        items_.pop_back();
        fired.push_back(std::move(t));
    }

    for (auto& t : fired) {
        if (t.cancelled) continue;
        if (t.callback) {
            try {
                t.callback();
            } catch (const std::exception& e) {
                LOG_ERROR("Timer: callback exception: %s", e.what());
            }
        }
        // 重复型：重新入堆
        if (t.interval_ms > 0) {
            TimerItem nt;
            nt.id = ++next_id_;
            nt.expire_at = now_ms + t.interval_ms;
            nt.interval_ms = t.interval_ms;
            nt.callback = t.callback;
            nt.cancelled = false;
            items_.push_back(std::move(nt));
            std::push_heap(items_.begin(), items_.end(),
                         [](const TimerItem& a, const TimerItem& b) {
                             return a.expire_at > b.expire_at;
                         });
        }
    }

    return items_.empty() ? -1 : (int64_t)items_.front().expire_at - now_ms;
}

int64_t Timer::nearest_expire() const {
    if (items_.empty()) return -1;
    // 跳过已取消的
    for (auto& item : items_) {
        if (!item.cancelled) return item.expire_at;
    }
    return -1;
}

size_t Timer::size() const {
    size_t count = 0;
    for (auto& item : items_) {
        if (!item.cancelled) ++count;
    }
    return count;
}

// ============ TimerLoop（独立后台线程） ============
struct TimerLoop::Impl {
    std::atomic<bool>         running{false};
    std::thread               thread;
    std::vector<TimerItem>    items;
    std::mutex                mtx;
    int64_t                   next_id{0};

    // 待加入的条目（push 来自外部线程）
    std::vector<TimerItem>    pending_add;
    std::mutex                pending_mtx;

    Impl() = default;

    int64_t add_impl(int64_t after_ms, TimerCallback cb, int64_t interval_ms) {
        int64_t now = (int64_t)get_tick_ms();
        int64_t id = ++next_id;
        TimerItem item;
        item.id = id;
        item.expire_at = now + after_ms;
        item.interval_ms = interval_ms;
        item.callback = std::move(cb);
        item.cancelled = false;

        std::lock_guard<std::mutex> lock(pending_mtx);
        pending_add.push_back(std::move(item));
        return id;
    }

    bool cancel_impl(int64_t id) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& item : items) {
            if (item.id == id) {
                item.cancelled = true;
                return true;
            }
        }
        return false;
    }
};

TimerLoop::TimerLoop() : impl_(std::make_unique<Impl>()) {}
TimerLoop::~TimerLoop() { stop(); }

void TimerLoop::start() {
    if (impl_->running.load()) return;
    impl_->running.store(true, std::memory_order_release);
    impl_->thread = std::thread([this]() { thread_loop(); });
}

void TimerLoop::stop() {
    if (!impl_->running.load()) return;
    impl_->running.store(false, std::memory_order_release);
    if (impl_->thread.joinable()) impl_->thread.join();
}

int64_t TimerLoop::add(int64_t after_ms, TimerCallback cb) {
    return impl_->add_impl(after_ms, std::move(cb), 0);
}

int64_t TimerLoop::add_repeating(int64_t interval_ms, TimerCallback cb) {
    return impl_->add_impl(interval_ms, std::move(cb), interval_ms);
}

bool TimerLoop::cancel(int64_t id) {
    return impl_->cancel_impl(id);
}

void TimerLoop::thread_loop() {
    LOG_INFO("TimerLoop: started");
    while (impl_->running.load(std::memory_order_acquire)) {
        // 合并待加入的条目
        {
            std::lock_guard<std::mutex> lock(impl_->pending_mtx);
            if (!impl_->pending_add.empty()) {
                std::lock_guard<std::mutex> lock2(impl_->mtx);
                for (auto& item : impl_->pending_add) {
                    impl_->items.push_back(std::move(item));
                }
                impl_->pending_add.clear();
                std::push_heap(impl_->items.begin(), impl_->items.end(),
                              [](const TimerItem& a, const TimerItem& b) {
                                  return a.expire_at > b.expire_at;
                              });
            }
        }

        int64_t now = (int64_t)get_tick_ms();

        // 找到最近到期时间
        int64_t sleep_ms = 100; // 默认 100ms
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            if (!impl_->items.empty()) {
                sleep_ms = std::max<int64_t>(1, impl_->items.front().expire_at - now);
            }
        }

        sleep_ms = std::min<int64_t>(sleep_ms, 500); // 最多等 500ms
        sleep_ms = std::max<int64_t>(sleep_ms, 1);
        sleep_ms = (int)sleep_ms;
        framework::sleep_ms(sleep_ms);

        // 处理到期定时器
        std::vector<TimerItem> fired;
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            int64_t now2 = (int64_t)get_tick_ms();
            while (!impl_->items.empty()) {
                TimerItem& top = impl_->items.front();
                if (top.cancelled) {
                    std::pop_heap(impl_->items.begin(), impl_->items.end(),
                                 [](const TimerItem& a, const TimerItem& b) {
                                     return a.expire_at > b.expire_at;
                                 });
                    impl_->items.pop_back();
                    continue;
                }
                if (top.expire_at > now2) break;

                std::pop_heap(impl_->items.begin(), impl_->items.end(),
                             [](const TimerItem& a, const TimerItem& b) {
                                 return a.expire_at > b.expire_at;
                             });
                TimerItem t = std::move(impl_->items.back());
                impl_->items.pop_back();
                fired.push_back(std::move(t));
            }
        }

        for (auto& t : fired) {
            if (t.cancelled) continue;
            if (t.callback) {
                try {
                    t.callback();
                } catch (const std::exception& e) {
                    LOG_ERROR("TimerLoop: callback exception: %s", e.what());
                }
            }
            if (t.interval_ms > 0) {
                int64_t now3 = (int64_t)get_tick_ms();
                TimerItem nt;
                nt.id = ++impl_->next_id;
                nt.expire_at = now3 + t.interval_ms;
                nt.interval_ms = t.interval_ms;
                nt.callback = t.callback;
                nt.cancelled = false;
                std::lock_guard<std::mutex> lock(impl_->mtx);
                impl_->items.push_back(std::move(nt));
                std::push_heap(impl_->items.begin(), impl_->items.end(),
                             [](const TimerItem& a, const TimerItem& b) {
                                 return a.expire_at > b.expire_at;
                             });
            }
        }
    }
    LOG_INFO("TimerLoop: stopped");
}

} // namespace framework
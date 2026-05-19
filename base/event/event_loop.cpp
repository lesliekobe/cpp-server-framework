/*
 * event_loop.cpp
 */
#include "base/event/event_loop.h"
#include "logger/logger.h"
#include <cstring>
#include <algorithm>
#include <sys/timerfd.h>
#include <unistd.h>

namespace framework {

// ============ 构造 / 析构 ============
EventLoop::EventLoop(bool external)
    : external_(external), running_(false)
{
#ifdef PLATFORM_LINUX
    events_.reserve(64);
#endif
}

EventLoop::~EventLoop() {
    stop();
#ifdef PLATFORM_LINUX
    if (epoll_fd_ >= 0) close(epoll_fd_), epoll_fd_ = -1;
    if (timer_fd_ >= 0) close(timer_fd_), timer_fd_ = -1;
#elif defined(PLATFORM_WINDOWS)
    if (iocp_ != nullptr) CloseHandle(iocp_), iocp_ = nullptr;
#endif
}

// ============ 初始化 ============
bool EventLoop::init() {
#ifdef PLATFORM_LINUX
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        LOG_ERROR("EventLoop: epoll_create1 failed: %d", errno);
        return false;
    }

    // 创建 timerfd
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ < 0) {
        LOG_WARN("EventLoop: timerfd_create failed, using internal timer");
        timer_fd_ = -1;
    } else {
        // timerfd 由内部管理，不暴露给用户
        // 添加到 epoll（但由内部处理，不触发用户回调）
        (void)add_fd; // suppress warning
    }
    return true;

#elif defined(PLATFORM_WINDOWS)
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp_) {
        LOG_ERROR("EventLoop: CreateIoCompletionPort failed");
        return false;
    }
    return true;
#endif
}

// ============ 主循环 ============
void EventLoop::run() {
    if (running_.load()) return;
    running_.store(true, std::memory_order_release);

    LOG_INFO("EventLoop: started");
    while (is_running()) {
        run_once(1000); // 1s timeout for graceful check
    }
    LOG_INFO("EventLoop: stopped");
}

void EventLoop::run_once(int timeout_ms) {
    // 处理异步任务
    process_tasks();

    // 处理定时器
    process_timers();

#ifdef PLATFORM_LINUX
    if (epoll_fd_ < 0) return;

    int nfds = epoll_wait(epoll_fd_, events_.data(), (int)events_.size(), timeout_ms);
    if (nfds < 0) {
        if (errno == EINTR) return;
        LOG_ERROR("EventLoop: epoll_wait error: %d", errno);
        return;
    }

    for (int i = 0; i < nfds; ++i) {
        struct epoll_event& ev = events_[i];
        int fd = ev.data.fd;
        uint32_t flags = ev.events;
        EventHandler* handler = (EventHandler*)ev.data.ptr;

        if (!handler) continue;

        EventType type = EventType::READ; // default
        if (flags & EPOLLIN)  type = type | EventType::READ;
        if (flags & EPOLLOUT) type = type | EventType::WRITE;
        if (flags & EPOLLERR) type = type | EventType::ERROR;
        if (flags & EPOLLHUP) type = type | EventType::CLOSED;

        handler->on_event(fd, type);
    }

#elif defined(PLATFORM_WINDOWS)
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED ov = nullptr;
    BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, timeout_ms);
    if (!ok && ov == nullptr) {
        // timeout
        return;
    }

    if (key == 0 && ov != nullptr) {
        // 定时器事件（自定义 OVERLAPPED）
        TimerEntry* te = (TimerEntry*)ov;
        if (te && te->callback && !te->cancelled) {
            te->callback();
        }
    } else {
        EventHandler* handler = (EventHandler*)key;
        if (handler) {
            int fd = (int)(intptr_t)ov;
            DWORD flags = bytes; // 复用 bytes 存储 flags
            EventType type = EventType::READ;
            if (flags & FD_READ)  type = type | EventType::READ;
            if (flags & FD_WRITE) type = type | EventType::WRITE;
            if (flags & FD_ERROR) type = type | EventType::ERROR;
            if (flags & FD_CLOSE) type = type | EventType::CLOSED;
            handler->on_event(fd, type);
        }
    }
#endif
}

void EventLoop::stop() {
    running_.store(false, std::memory_order_release);
}

// ============ FD 事件 ============
bool EventLoop::add_fd(int fd, EventType type, EventHandler* handler) {
#ifdef PLATFORM_LINUX
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    if (has_flag(type, EventType::READ))  ev.events |= EPOLLIN;
    if (has_flag(type, EventType::WRITE)) ev.events |= EPOLLOUT;
    ev.events |= EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;
    ev.data.ptr = handler;

    int op = fd_handlers_.count(fd) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(epoll_fd_, op, fd, &ev) < 0) {
        LOG_ERROR("EventLoop: epoll_ctl add/mod failed fd=%d", fd);
        return false;
    }
    fd_handlers_[fd] = handler;
    return true;

#elif defined(PLATFORM_WINDOWS)
    HANDLE h = CreateIoCompletionPort((HANDLE)(uintptr_t)fd, iocp_, (ULONG_PTR)handler, 0);
    if (!h) {
        LOG_ERROR("EventLoop: CreateIoCompletionPort associate failed fd=%d", fd);
        return false;
    }
    fd_handlers_[fd] = handler;
    return true;
#endif
}

bool EventLoop::del_fd(int fd) {
#ifdef PLATFORM_LINUX
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        // 不记录错误，因为可能已关闭
    }
    fd_handlers_.erase(fd);
    return true;
#elif defined(PLATFORM_WINDOWS)
    fd_handlers_.erase(fd);
    return true;
#endif
}

bool EventLoop::mod_fd(int fd, EventType type) {
#ifdef PLATFORM_LINUX
    auto it = fd_handlers_.find(fd);
    if (it == fd_handlers_.end()) return false;
    return add_fd(fd, type, it->second);
#elif defined(PLATFORM_WINDOWS)
    (void)type;
    return true;
#endif
}

// ============ 定时器 ============
int64_t EventLoop::add_timer(int64_t after_ms, std::function<void()> cb, int64_t interval_ms) {
    if (!cb) return -1;

    int64_t now = (int64_t)get_tick_ms();
    int64_t id = ++timer_id_counter_;

    TimerEntry entry;
    entry.id = id;
    entry.expire_at = now + after_ms;
    entry.interval = interval_ms;
    entry.callback = std::move(cb);
    entry.cancelled = false;

    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        timers_.push_back(std::move(entry));
        std::push_heap(timers_.begin(), timers_.end()); // min-heap by expire_at
    }

    LOG_DEBUG("EventLoop: add timer id=%lld after=%lldms", (long long)id, (long long)after_ms);
    return id;
}

bool EventLoop::cancel_timer(int64_t id) {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    for (auto& t : timers_) {
        if (t.id == id) {
            t.cancelled = true;
            return true;
        }
    }
    return false;
}

void EventLoop::process_timers() {
    int64_t now = (int64_t)get_tick_ms();
    std::vector<TimerEntry> fired;

    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        while (!timers_.empty()) {
            TimerEntry& top = timers_.front();
            if (top.cancelled) {
                std::pop_heap(timers_.begin(), timers_.end());
                timers_.pop_back();
                continue;
            }
            if (top.expire_at > now) break;

            std::pop_heap(timers_.begin(), timers_.end());
            TimerEntry t = std::move(timers_.back());
            timers_.pop_back();
            fired.push_back(std::move(t));
        }
    }

    for (auto& t : fired) {
        if (t.cancelled) continue;
        if (t.callback) {
            try {
                t.callback();
            } catch (const std::exception& e) {
                LOG_ERROR("EventLoop: timer callback exception: %s", e.what());
            }
        }
        // 周期定时器：重新入队
        if (t.interval > 0) {
            TimerEntry nt;
            nt.id = ++timer_id_counter_;
            nt.expire_at = now + t.interval;
            nt.interval = t.interval;
            nt.callback = t.callback;
            nt.cancelled = false;
            std::lock_guard<std::mutex> lock(timers_mutex_);
            timers_.push_back(std::move(nt));
            std::push_heap(timers_.begin(), timers_.end());
        }
    }
}

// ============ 异步任务 ============
void EventLoop::push_task(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_.push_back(std::move(task));
    }
}

void EventLoop::process_tasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (pending_tasks_.empty()) return;
        tasks.swap(pending_tasks_);
    }
    for (auto& t : tasks) {
        try {
            t();
        } catch (const std::exception& e) {
            LOG_ERROR("EventLoop: task exception: %s", e.what());
        }
    }
}

// ============ 信号（Unix） ============
bool EventLoop::add_signal(int sig, EventHandler* handler) {
#ifdef PLATFORM_LINUX
    (void)sig; (void)handler;
    // 信号处理依赖于 sigaction，简化处理：
    // 使用 eventfd 或 pipe 唤醒 epoll
    return false;
#elif defined(PLATFORM_WINDOWS)
    (void)sig; (void)handler;
    return false;
#endif
}

int EventLoop::poll_fd() const {
#ifdef PLATFORM_LINUX
    return epoll_fd_;
#elif defined(PLATFORM_WINDOWS)
    return -1;
#endif
}

// ============ 工厂 ============
std::shared_ptr<EventLoop> create_event_loop() {
    auto loop = std::make_shared<EventLoop>();
    if (!loop->init()) return nullptr;
    return loop;
}

} // namespace framework
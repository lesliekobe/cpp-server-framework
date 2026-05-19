/*
 * event_loop.h - 事件驱动核心（epoll/Kqueue/IOCP 封装）
 *
 * 支持：
 *   - Linux: epoll LT/ET
 *   - Windows: IOCP
 *   - 文件描述符事件（读/写/错误/关闭）
 *   - 定时器（内部时间轮）
 *   - 信号（Unix）
 */

#pragma once

#include "adapter/platform.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>

namespace framework {

// ============ 事件类型 ============
enum class EventType : int {
    READ   = 1 << 0,
    WRITE  = 1 << 1,
    ERROR  = 1 << 2,
    CLOSED = 1 << 3,
    TIMER  = 1 << 4,
    SIGNAL = 1 << 5,
};

constexpr EventType operator|(EventType a, EventType b) {
    return static_cast<EventType>(static_cast<int>(a) | static_cast<int>(b));
}
constexpr bool has_flag(EventType flags, EventType f) {
    return (static_cast<int>(flags) & static_cast<int>(f)) != 0;
}

// ============ 事件处理器接口 ============
class EventHandler {
public:
    virtual ~EventHandler() = default;

    // 事件回调：fd < 0 表示定时器/信号
    virtual void on_read(int fd)     {}
    virtual void on_write(int fd)    {}
    virtual void on_timeout(int fd)  {}
    virtual void on_signal(int sig)  {}
    virtual void on_error(int fd)    {}
    virtual void on_close(int fd)    {}

    // 通用错误/关闭
    virtual void on_event(int fd, EventType type) {
        if (has_flag(type, EventType::READ))  on_read(fd);
        if (has_flag(type, EventType::WRITE)) on_write(fd);
        if (has_flag(type, EventType::ERROR)) on_error(fd);
        if (has_flag(type, EventType::CLOSED)) on_close(fd);
        if (has_flag(type, EventType::TIMER))  on_timeout(fd);
        if (has_flag(type, EventType::SIGNAL)) on_signal(fd);
    }

    // 用于识别 handler（可选）
    virtual const void* tag() const { return nullptr; }
};

// ============ 内部任务条目 ============
struct TimerEntry {
    int64_t id{0};
    int64_t expire_at{0};    // ms absolute
    int64_t interval{0};     // 0 = one-shot
    std::function<void()> callback;
    bool cancelled{false};

    bool operator<(const TimerEntry& o) const { return expire_at > o.expire_at; } // min-heap
};

// ============ EventLoop 主类 ============
class EventLoop {
public:
    explicit EventLoop(bool external = false);
    ~EventLoop();

    // 禁止拷贝
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // ---- 生命周期 ----
    bool init();
    void run();                     // 阻塞事件循环
    void run_once(int timeout_ms);  // 单次 epoll_wait
    void stop();                    // 退出事件循环

    // ---- 注册事件 ----
    // 注册 fd 事件（可重复调用更新）
    bool add_fd(int fd, EventType type, EventHandler* handler);
    bool del_fd(int fd);
    bool mod_fd(int fd, EventType type);

    // ---- 定时器 ----
    // 返回定时器 ID（>0），cancel_timer(id) 可取消
    int64_t add_timer(int64_t after_ms, std::function<void()> cb, int64_t interval_ms = 0);
    bool     cancel_timer(int64_t id);

    // ---- 信号（Unix） ----
    bool add_signal(int sig, EventHandler* handler);

    // ---- 异步任务（可在其他线程调用） ----
    void push_task(std::function<void()> task);

    // ---- 状态 ----
    bool is_running() const { return running_.load(std::memory_order_acquire); }
    int poll_fd() const; // 用于外部 poll 集成

private:
    void process_timers();           // 处理到期定时器
    void process_tasks();            // 处理异步任务队列
    void update_timer_fd();          // 更新 timerfd

    bool                                     running_{false};
    bool                                     external_{false};

#ifdef PLATFORM_LINUX
    int                                      epoll_fd_{-1};
    std::unordered_map<int, EventHandler*>   fd_handlers_;
    std::vector<struct epoll_event>          events_;
    int                                      timer_fd_{-1};
#elif defined(PLATFORM_WINDOWS)
    HANDLE                                   iocp_{nullptr};
    std::unordered_map<int, EventHandler*>   fd_handlers_;
#endif

    // 定时器（内存时间轮，Unix 也可用 timerfd）
    std::vector<TimerEntry>                  timers_;
    int64_t                                  timer_id_counter_{0};
    std::mutex                               timers_mutex_;

    // 异步任务队列
    std::vector<std::function<void()>>       pending_tasks_;
    std::mutex                               tasks_mutex_;

    // 信号（Unix）
    std::unordered_map<int, EventHandler*>  signal_handlers_;
    int                                      signal_fd_{-1};
};

// ============ 工厂函数 ============
std::shared_ptr<EventLoop> create_event_loop();

} // namespace framework
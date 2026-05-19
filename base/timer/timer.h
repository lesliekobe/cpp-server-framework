/*
 * timer.h - 高精度定时器模块
 *
 * 基于 eventfd / timerfd（Linux）或内部时间轮实现
 * 支持：
 *   - 一次性延时任务
 *   - 周期任务（固定间隔）
 *   - 取消定时器
 *   - 线程安全
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>

namespace framework {

// ============ 定时器回调 ============
using TimerCallback = std::function<void()>;

// ============ 定时器条目 ============
struct TimerItem {
    int64_t         id{0};
    int64_t         expire_at{0};  // 毫秒绝对时间
    int64_t         interval_ms{0}; // 0=一次性
    TimerCallback   callback;
    bool            cancelled{false};

    bool is_repeating() const { return interval_ms > 0; }
};

// ============ 定时器管理器 ============
class Timer {
public:
    Timer();
    ~Timer();

    // 禁止拷贝
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // 添加一次性定时器，after_ms 毫秒后触发
    // 返回定时器 ID（>0），可传给 cancel()
    int64_t add(int64_t after_ms, TimerCallback cb);

    // 添加周期定时器，每 interval_ms 毫秒触发
    int64_t add_repeating(int64_t interval_ms, TimerCallback cb);

    // 取消定时器（已触发的自动移除）
    bool cancel(int64_t timer_id);

    // 取消所有定时器
    void clear();

    // 驱动一次（从 EventLoop 或主循环调用）
    // 返回距下一次到期的时间（毫秒），-1 表示无定时器
    int64_t poll(int64_t now_ms);

    // 获取最近一个定时器到期时间（绝对毫秒）
    int64_t nearest_expire() const;

    size_t size() const;

private:
    int64_t next_id_{0};
    std::vector<TimerItem> items_;
};

// ============ 便捷定时器（独立运行） ============
class TimerLoop {
public:
    TimerLoop();
    ~TimerLoop();

    // 启动后台定时器线程
    void start();

    // 停止定时器线程
    void stop();

    // 添加定时器（线程安全）
    int64_t add(int64_t after_ms, TimerCallback cb);
    int64_t add_repeating(int64_t interval_ms, TimerCallback cb);
    bool    cancel(int64_t id);

private:
    void thread_loop();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace framework
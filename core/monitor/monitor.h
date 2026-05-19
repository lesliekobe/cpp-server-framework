/*
 * monitor.h - 监控统计模块
 *
 * 统计：
 *   - 连接数（在线/总计/峰值）
 *   - 任务队列长度
 *   - 线程池负载
 *   - 消息收发计数
 *   - 内存使用（RSS）
 *   - 定时打点到日志
 *   - HTTP 状态页面（可选）
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace framework {

// ============ 统计数据快照 ============
struct MonitorStats {
    int64_t  timestamp{0};

    // 连接
    int      connections_alive{0};
    int      connections_total{0};
    int      connections_peak{0};

    // 任务队列
    size_t   task_queue_length{0};

    // 线程池
    int      threadpool_active{0};
    int      threadpool_total{0};

    // 消息
    uint64_t msgs_sent{0};
    uint64_t msgs_recv{0};
    uint64_t msgs_error{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_recv{0};

    // 内存
    size_t   memory_rss_kb{0};
    size_t   memory_peak_rss_kb{0};

    // 日志
    uint64_t total_logs{0};
};

// ============ 监控器 ============
class Monitor {
public:
    Monitor();
    ~Monitor();

    // 连接统计
    void on_connect() {
        connections_alive_.fetch_add(1, std::memory_order_relaxed);
        int total = connections_total_.fetch_add(1, std::memory_order_relaxed) + 1;
        int peak = connections_peak_.load(std::memory_order_relaxed);
        while (total > peak && !connections_peak_.compare_exchange_weak(peak, total)) {}
    }
    void on_disconnect() {
        connections_alive_.fetch_sub(1, std::memory_order_relaxed);
    }

    // 任务队列
    void set_task_queue_length(size_t len) {
        task_queue_length_.store(len, std::memory_order_relaxed);
    }

    // 线程池
    void set_threadpool_stats(int active, int total) {
        threadpool_active_.store(active, std::memory_order_relaxed);
        threadpool_total_.store(total, std::memory_order_relaxed);
    }

    // 消息统计
    void on_msg_sent(size_t bytes = 0) {
        msgs_sent_.fetch_add(1, std::memory_order_relaxed);
        if (bytes) bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
    }
    void on_msg_recv(size_t bytes = 0) {
        msgs_recv_.fetch_add(1, std::memory_order_relaxed);
        if (bytes) bytes_recv_.fetch_add(bytes, std::memory_order_relaxed);
    }
    void on_msg_error() {
        msgs_error_.fetch_add(1, std::memory_order_relaxed);
    }

    // 刷新统计（收集系统内存等）
    void refresh();

    // 获取快照
    MonitorStats snapshot() const;

    // 定时打点日志
    void start_logging(int interval_seconds = 60);

    // 停止打点
    void stop_logging();

    // HTTP 状态页面（可选，:8080/monitor）
    void start_http_server(int port = 8080);
    void stop_http_server();

    // 转为 JSON 字符串
    std::string to_json() const;

private:
    void logging_loop();

    std::atomic<int>      connections_alive_{0};
    std::atomic<int>      connections_total_{0};
    std::atomic<int>      connections_peak_{0};
    std::atomic<size_t>   task_queue_length_{0};
    std::atomic<int>      threadpool_active_{0};
    std::atomic<int>      threadpool_total_{0};
    std::atomic<uint64_t> msgs_sent_{0};
    std::atomic<uint64_t> msgs_recv_{0};
    std::atomic<uint64_t> msgs_error_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_recv_{0};
    std::atomic<size_t>   memory_rss_kb_{0};
    std::atomic<size_t>   memory_peak_rss_kb_{0};

    std::atomic<bool>     logging_{false};
    std::thread           logging_thread_;

    int                   http_port_{-1};
    std::atomic<bool>     http_running_{false};
    std::thread           http_thread_;
};

// ============ 全局 Monitor ============
Monitor* global_monitor();

} // namespace framework
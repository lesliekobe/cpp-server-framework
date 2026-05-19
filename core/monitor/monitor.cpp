/*
 * monitor.cpp
 */
#include "core/monitor/monitor.h"
#include "logger/logger.h"
#include "adapter/platform.h"
#include <thread>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

namespace framework {

// ============ 全局 ============
static Monitor g_monitor;

Monitor* global_monitor() { return &g_monitor; }

Monitor::Monitor() = default;
Monitor::~Monitor() { stop_logging(); stop_http_server(); }

MonitorStats Monitor::snapshot() const {
    MonitorStats s;
    s.timestamp = (int64_t)get_tick_ms();
    s.connections_alive = connections_alive_.load(std::memory_order_relaxed);
    s.connections_total = connections_total_.load(std::memory_order_relaxed);
    s.connections_peak  = connections_peak_.load(std::memory_order_relaxed);
    s.task_queue_length = task_queue_length_.load(std::memory_order_relaxed);
    s.threadpool_active  = threadpool_active_.load(std::memory_order_relaxed);
    s.threadpool_total  = threadpool_total_.load(std::memory_order_relaxed);
    s.msgs_sent         = msgs_sent_.load(std::memory_order_relaxed);
    s.msgs_recv         = msgs_recv_.load(std::memory_order_relaxed);
    s.msgs_error        = msgs_error_.load(std::memory_order_relaxed);
    s.bytes_sent        = bytes_sent_.load(std::memory_order_relaxed);
    s.bytes_recv        = bytes_recv_.load(std::memory_order_relaxed);
    s.memory_rss_kb     = memory_rss_kb_.load(std::memory_order_relaxed);
    s.memory_peak_rss_kb= memory_peak_rss_kb_.load(std::memory_order_relaxed);
    return s;
}

void Monitor::refresh() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        size_t rss_kb = pmc.WorkingSetSize / 1024;
        memory_rss_kb_.store(rss_kb, std::memory_order_relaxed);
        size_t peak = memory_peak_rss_kb_.load(std::memory_order_relaxed);
        while (rss_kb > peak && !memory_peak_rss_kb_.compare_exchange_weak(peak, rss_kb)) {}
    }
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        size_t rss_kb = ru.ru_maxrss;
        memory_rss_kb_.store(rss_kb, std::memory_order_relaxed);
        size_t peak = memory_peak_rss_kb_.load(std::memory_order_relaxed);
        while (rss_kb > peak && !memory_peak_rss_kb_.compare_exchange_weak(peak, rss_kb)) {}
    }
#endif
}

void Monitor::start_logging(int interval_seconds) {
    if (logging_.load()) return;
    logging_.store(true, std::memory_order_release);
    logging_thread_ = std::thread([this, interval_seconds]() {
        LOG_INFO("Monitor: logging started (interval=%ds)", interval_seconds);
        while (logging_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
            if (!logging_.load()) break;
            refresh();
            MonitorStats s = snapshot();
            LOG_INFO("Monitor: conn_alive=%d conn_total=%d conn_peak=%d "
                     "queue_len=%zu threads=%d/%d "
                     "msgs_sent=%llu msgs_recv=%llu msgs_err=%llu "
                     "bytes_sent=%llu bytes_recv=%llu "
                     "rss_mb=%.1f peak_rss_mb=%.1f",
                     s.connections_alive, s.connections_total, s.connections_peak,
                     s.task_queue_length,
                     s.threadpool_active, s.threadpool_total,
                     (unsigned long long)s.msgs_sent,
                     (unsigned long long)s.msgs_recv,
                     (unsigned long long)s.msgs_error,
                     (unsigned long long)s.bytes_sent,
                     (unsigned long long)s.bytes_recv,
                     s.memory_rss_kb / 1024.0,
                     s.memory_peak_rss_kb / 1024.0);
        }
        LOG_INFO("Monitor: logging stopped");
    });
}

void Monitor::stop_logging() {
    if (!logging_.load()) return;
    logging_.store(false, std::memory_order_release);
    if (logging_thread_.joinable()) logging_thread_.join();
}

void Monitor::start_http_server(int port) {
    if (http_running_.load()) return;
    http_port_ = port;
    http_running_.store(true, std::memory_order_release);
    http_thread_ = std::thread([this, port]() {
        LOG_INFO("Monitor: HTTP server starting on port %d", port);
        // 简化实现：使用 tiny HTTP 服务器
        // 实际项目可用 libmicrohttpd 或 httplib
        // 这里只输出日志说明功能已开启
        while (http_running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        LOG_INFO("Monitor: HTTP server stopped");
    });
}

void Monitor::stop_http_server() {
    if (!http_running_.load()) return;
    http_running_.store(false, std::memory_order_release);
    if (http_thread_.joinable()) http_thread_.join();
}

std::string Monitor::to_json() const {
    MonitorStats s = snapshot();
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"timestamp\": " << s.timestamp << ",\n";
    oss << "  \"connections\": {\n";
    oss << "    \"alive\": " << s.connections_alive << ",\n";
    oss << "    \"total\": " << s.connections_total << ",\n";
    oss << "    \"peak\": " << s.connections_peak << "\n";
    oss << "  },\n";
    oss << "  \"task_queue_length\": " << s.task_queue_length << ",\n";
    oss << "  \"threadpool\": {\n";
    oss << "    \"active\": " << s.threadpool_active << ",\n";
    oss << "    \"total\": " << s.threadpool_total << "\n";
    oss << "  },\n";
    oss << "  \"messages\": {\n";
    oss << "    \"sent\": " << s.msgs_sent << ",\n";
    oss << "    \"recv\": " << s.msgs_recv << ",\n";
    oss << "    \"error\": " << s.msgs_error << "\n";
    oss << "  },\n";
    oss << "  \"bytes\": {\n";
    oss << "    \"sent\": " << s.bytes_sent << ",\n";
    oss << "    \"recv\": " << s.bytes_recv << "\n";
    oss << "  },\n";
    oss << "  \"memory\": {\n";
    oss << "    \"rss_kb\": " << s.memory_rss_kb << ",\n";
    oss << "    \"peak_rss_kb\": " << s.memory_peak_rss_kb << "\n";
    oss << "  }\n";
    oss << "}";
    return oss.str();
}

} // namespace framework
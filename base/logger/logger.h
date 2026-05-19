/*
 * logger.h - 高级日志模块
 *
 * 特性：
 *   - 六级日志：TRACE / DEBUG / INFO / WARN / ERROR / FATAL
 *   - 异步落盘（独立日志线程，不阻塞主线程）
 *   - 按天切割日志文件
 *   - GZip 压缩归档旧日志
 *   - Console + File 双输出，输出级别独立可控
 *   - 日志染色（不同级别颜色不同）
 *   - 输出格式可配置
 */

#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>

namespace framework {

// ============ 日志级别 ============
enum class LogLevel : int {
    TRACE   = 0,
    DEBUG   = 1,
    INFO    = 2,
    WARN    = 3,
    ERROR   = 4,
    FATAL   = 5,
    NONE    = 6  // 关闭
};

constexpr const char* log_level_str(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKN ";
    }
}

// ============ 日志条目 ============
struct LogEntry {
    int64_t   timestamp_ms{0};
    LogLevel  level{LogLevel::INFO};
    int       thread_id{0};
    std::string tag;
    std::string message;
};

// ============ 日志格式配置 ============
struct LogFormatter {
    bool show_timestamp{true};
    bool show_thread_id{true};
    bool show_level{true};
    bool show_tag{true};
    bool colorful{true};          // 控制台颜色
    std::string time_format{"%Y-%m-%d %H:%M:%S"};
};

// ============ 日志输出目标 ============
struct LogSink {
    std::string path;             // 日志目录（空=不写文件）
    std::string filename_prefix; // 文件名前缀
    size_t      max_file_size_mb{200};
    int         max_history_days{7};  // 保留天数
    bool        compress{true};   // GZip 压缩归档
    bool        async{true};      // 异步写盘
};

// ============ Logger 主类 ============
class Logger {
public:
    // 单例
    static Logger* instance();

    // 初始化（多 sink）
    bool init(const std::vector<LogSink>& sinks,
               LogLevel console_level = LogLevel::INFO,
               LogLevel file_level = LogLevel::DEBUG,
               const LogFormatter& fmt = LogFormatter{});

    // 简化初始化
    bool init(const std::string& log_dir,
              const std::string& filename_prefix,
              LogLevel console_level = LogLevel::INFO,
              LogLevel file_level = LogLevel::DEBUG);

    // 设置级别
    void set_console_level(LogLevel lvl);
    void set_file_level(LogLevel lvl);
    void set_level(LogLevel lvl);

    // 日志输出
    void trace(const char* fmt, ...);
    void debug(const char* fmt, ...);
    void info (const char* fmt, ...);
    void warn (const char* fmt, ...);
    void error(const char* fmt, ...);
    void fatal(const char* fmt, ...);

    // 带 tag 的日志
    void log(LogLevel lvl, const char* tag, const char* fmt, ...);

    // 同步刷新（阻塞直到所有日志写入）
    void flush();

    // 安全关闭（析构前自动调用）
    void shutdown();

    // 设置额外 hook（每次写日志前回调，可用于自定义染色）
    using Hook = std::function<void(LogEntry&)>;
    void set_hook(Hook h) { hook_ = std::move(h); }

    ~Logger();

private:
    Logger() = default;

    // 内部写入
    void write(LogLevel lvl, const char* tag, const char* msg, size_t len);

    // 日志线程循环
    void log_thread_loop();

    // 轮转检查
    void check_rotate();

    // 压缩归档旧日志
    void compress_old_logs();

    std::string format_entry(const LogEntry& e);

    // 异步队列
    void enqueue(LogEntry e);

    // 格式化
    LogFormatter                          fmt_;
    std::vector<LogSink>                  sinks_;

    std::atomic<LogLevel>                 console_level_{LogLevel::INFO};
    std::atomic<LogLevel>                 file_level_{LogLevel::DEBUG};

    std::atomic<bool>                    running_{false};
    std::thread                          log_thread_;
    std::vector<LogEntry>                queue_;
    std::mutex                           queue_mtx_;
    std::condition_variable              queue_cv_;
    size_t                               queue_max_{8192};

    std::atomic<int64_t>                 last_flush_time_{0};

    std::atomic<int>                     today_yday_{-1};
    std::string                          current_log_path_;

    Hook                                 hook_;

    std::atomic<size_t>                  dropped_{0};

    // 统计
    std::atomic<uint64_t>                total_logged_{0};
};

// ============ 便捷宏 ============
#define LOG_TRACE(...)  framework::Logger::instance()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)  framework::Logger::instance()->debug(__VA_ARGS__)
#define LOG_INFO(...)   framework::Logger::instance()->info(__VA_ARGS__)
#define LOG_WARN(...)   framework::Logger::instance()->warn(__VA_ARGS__)
#define LOG_ERROR(...)  framework::Logger::instance()->error(__VA_ARGS__)
#define LOG_FATAL(...)  framework::Logger::instance()->fatal(__VA_ARGS__)

// 带 tag 的宏
#define LOG_TAG(lvl, tag, ...)  framework::Logger::instance()->log(lvl, tag, __VA_ARGS__)

} // namespace framework
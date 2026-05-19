/*
 * logger.cpp - 高级日志实现
 */
#include "logger/logger.h"
#include "adapter/platform.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace framework {

// ============ 单例 ============
Logger::~Logger() { shutdown(); }

Logger* Logger::instance() {
    static Logger inst;
    return &inst;
}

// ============ 初始化 ============
bool Logger::init(const std::vector<LogSink>& sinks,
                  LogLevel console_level, LogLevel file_level,
                  const LogFormatter& fmt) {
    sinks_ = sinks;
    console_level_.store(console_level, std::memory_order_release);
    file_level_.store(file_level, std::memory_order_release);
    fmt_ = fmt;

    time_t now = time(nullptr);
    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    today_yday_.store(t.tm_yday, std::memory_order_release);

    for (const auto& sink : sinks_) {
        if (!sink.path.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(sink.path, ec);
        }
    }

    running_.store(true, std::memory_order_release);
    log_thread_ = std::thread([this]() { log_thread_loop(); });
    return true;
}

bool Logger::init(const std::string& log_dir,
                  const std::string& filename_prefix,
                  LogLevel console_level, LogLevel file_level) {
    LogSink sink;
    sink.path = log_dir;
    sink.filename_prefix = filename_prefix;
    sink.async = true;
    sink.compress = true;
    return init({sink}, console_level, file_level);
}

// ============ 格式化 ============
std::string Logger::format_entry(const LogEntry& e) {
    std::ostringstream oss;
    if (fmt_.show_timestamp) {
        time_t t = e.timestamp_ms / 1000;
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        char buf[64];
        strftime(buf, sizeof(buf), fmt_.time_format.c_str(), &lt);
        oss << buf << " ";
    }
    if (fmt_.show_level) {
        oss << "[" << log_level_str(e.level) << "] ";
    }
    if (fmt_.show_thread_id) {
        oss << "[T" << e.thread_id << "] ";
    }
    if (fmt_.show_tag && !e.tag.empty()) {
        oss << "[" << e.tag << "] ";
    }
    oss << e.message;
    return oss.str();
}

// ============ 内部写入 ============
void Logger::write(LogLevel lvl, const char* tag, const char* msg, size_t len) {
    if (lvl < console_level_.load(std::memory_order_acquire) &&
        lvl < file_level_.load(std::memory_order_acquire)) return;

    LogEntry entry;
    entry.timestamp_ms = (int64_t)get_tick_ms();
    entry.level = lvl;
    entry.thread_id = (int)get_current_thread_id();
    entry.tag = tag ? tag : "";
    entry.message.assign(msg, len);

    if (hook_) hook_(entry);

    enqueue(std::move(entry));
}

void Logger::enqueue(LogEntry e) {
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        if (queue_.size() >= queue_max_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        queue_.push_back(std::move(e));
    }
    queue_cv_.notify_one();
}

// ============ 异步日志线程 ============
void Logger::log_thread_loop() {
    while (running_.load(std::memory_order_acquire)) {
        std::vector<LogEntry> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mtx_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                             [this]() { return !queue_.empty() || !running_.load(); });
            if (queue_.empty()) continue;
            batch = std::move(queue_);
            queue_.clear();
        }

        for (const auto& e : batch) {
            std::string line = format_entry(e);
            line += '\n';

            // Console
            if (e.level >= console_level_.load(std::memory_order_acquire)) {
                FILE* out = (e.level >= LogLevel::ERROR) ? stderr : stdout;
                if (fmt_.colorful && isatty(fileno(out))) {
                    const char* color = "";
                    switch (e.level) {
                        case LogLevel::TRACE: color = "\033[90m"; break; // 灰色
                        case LogLevel::DEBUG: color = "\033[36m"; break; // 青色
                        case LogLevel::INFO:  color = "\033[32m"; break; // 绿色
                        case LogLevel::WARN:  color = "\033[33m"; break; // 黄色
                        case LogLevel::ERROR: color = "\033[31m"; break; // 红色
                        case LogLevel::FATAL: color = "\033[35;1m"; break; // 亮紫
                        default: color = "";
                    }
                    fprintf(out, "%s%s\033[0m", color, line.c_str());
                } else {
                    fwrite(line.c_str(), 1, line.size(), out);
                }
            }

            // File
            if (e.level >= file_level_.load(std::memory_order_acquire)) {
                for (const auto& sink : sinks_) {
                    if (sink.path.empty() || !sink.async) continue;
                    if (current_log_path_.empty()) continue;
                    std::ofstream ofs(current_log_path_,
                                     std::ios::app | std::ios::binary);
                    if (ofs) ofs.write(line.c_str(), line.size());
                }
            }
        }

        total_logged_.fetch_add(batch.size(), std::memory_order_relaxed);
    }
}

// ============ 轮转检查 ============
void Logger::check_rotate() {
    time_t now = time(nullptr);
    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif

    if (t.tm_yday != today_yday_.load(std::memory_order_acquire)) {
        today_yday_.store(t.tm_yday, std::memory_order_release);

        // 压缩昨天的日志
        if (running_.load()) {
            compress_old_logs();
        }
    }
}

// ============ 压缩旧日志 ============
void Logger::compress_old_logs() {
    for (const auto& sink : sinks_) {
        if (sink.path.empty() || !sink.compress) continue;
        namespace fs = std::filesystem;
        try {
            for (const auto& entry : fs::directory_iterator(sink.path)) {
                if (!entry.is_regular_file()) continue;
                std::string name = entry.path().filename().string();
                if (name.find(".gz") != std::string::npos) continue;
                // 检查是否是旧日志（不含今天）
                // 简化：只压缩 .1, .2 等后缀文件
                if (name.find('.') != std::string::npos) {
                    std::string cmd = "gzip \"" + entry.path().string() + "\"";
                    std::system(cmd.c_str());
                }
            }
        } catch (...) {}
    }
}

void Logger::flush() {
    std::vector<LogEntry> batch;
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        batch = std::move(queue_);
    }

    for (const auto& e : batch) {
        std::string line = format_entry(e);
        line += '\n';
        for (const auto& sink : sinks_) {
            if (sink.path.empty()) continue;
            FILE* f = fopen(sink.path.c_str(), "a");
            if (f) { fwrite(line.c_str(), 1, line.size(), f); fclose(f); }
        }
    }
}

void Logger::shutdown() {
    if (!running_.load()) return;
    running_.store(false, std::memory_order_release);
    queue_cv_.notify_one();
    if (log_thread_.joinable()) log_thread_.join();
    flush();
}

void Logger::set_console_level(LogLevel lvl) {
    console_level_.store(lvl, std::memory_order_release);
}
void Logger::set_file_level(LogLevel lvl) {
    file_level_.store(lvl, std::memory_order_release);
}
void Logger::set_level(LogLevel lvl) {
    console_level_.store(lvl, std::memory_order_release);
    file_level_.store(lvl, std::memory_order_release);
}

// ============ variadic helpers ============
static void vformat_to(std::string& out, const char* fmt, va_list ap) {
    char buf[8192];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    out = buf;
}

void Logger::trace(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap); write(LogLevel::TRACE, nullptr, s.c_str(), s.size()); }
void Logger::debug(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap); write(LogLevel::DEBUG, nullptr, s.c_str(), s.size()); }
void Logger::info (const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap); write(LogLevel::INFO,  nullptr, s.c_str(), s.size()); }
void Logger::warn (const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap); write(LogLevel::WARN,  nullptr, s.c_str(), s.size()); }
void Logger::error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap); write(LogLevel::ERROR, nullptr, s.c_str(), s.size()); }
void Logger::fatal(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap); write(LogLevel::FATAL, nullptr, s.c_str(), s.size()); }

void Logger::log(LogLevel lvl, const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); std::string s; vformat_to(s, fmt, ap); va_end(ap);
    write(lvl, tag, s.c_str(), s.size());
}

} // namespace framework
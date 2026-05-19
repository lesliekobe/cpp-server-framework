/*
 * logger.cpp
 */
#include "logger/logger.h"
#include "adapter/platform.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <sys/stat.h>
#include <algorithm>

namespace framework {

static const char* LEVEL_STR[] = { "DEBUG", "INFO",  "WARN",  "ERROR" };

Logger::~Logger() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

Logger* Logger::instance() {
    static Logger inst;
    return &inst;
}

bool Logger::init(const std::string& filename, LogLevel level, size_t max_size_mb, size_t max_files) {
    level_.store(level, std::memory_order_release);
    filename_base_ = filename;
    max_size_ = max_size_mb * 1024 * 1024;
    max_files_ = max_files;
    file_size_.store(0, std::memory_order_release);
    return true;
}

void Logger::set_level(LogLevel level) {
    level_.store(level, std::memory_order_release);
}

void Logger::write(LogLevel lvl, const char* msg, size_t len) {
    if (lvl < level_.load(std::memory_order_acquire)) return;

    // 时间戳
    time_t now = time(nullptr);
    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char header[64];
    int n = snprintf(header, sizeof(header), "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
                     t.tm_year + 1900, t.tm_month + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec, LEVEL_STR[(int)lvl]);

    // 控制台输出
    fwrite(header, 1, n, stdout);
    fwrite(msg, 1, len, stdout);
    fwrite("\n", 1, 1, stdout);

    // 文件输出（带滚动）
    if (fd_ >= 0) {
        if (file_size_.load(std::memory_order_relaxed) + n + len + 1 > max_size_) {
            rotate_file();
        }
        ::write(fd_, header, n);
        ::write(fd_, msg, len);
        ::write(fd_, "\n", 1);
        file_size_.fetch_add(n + len + 1, std::memory_order_relaxed);
    }
}

void Logger::rotate_file() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        file_size_.store(0, std::memory_order_relaxed);
    }

    // 轮转旧文件
    char old_path[256], new_path[256];
    for (size_t i = max_files_; i > 1; --i) {
        snprintf(old_path, sizeof(old_path), "%s.%zu", filename_base_.c_str(), i - 1);
        snprintf(new_path, sizeof(new_path), "%s.%zu", filename_base_.c_str(), i);
        std::remove(new_path);
        std::rename(old_path, new_path);
    }

    // 当前日志文件
    std::string cur = filename_base_;
    fd_ = ::open(cur.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

void Logger::flush() {
    if (fd_ >= 0) {
#ifndef _WIN32
        fsync(fd_);
#endif
    }
}

// variadic helpers
static void vformat_and_write(Logger* log, LogLevel lvl, const char* fmt, va_list ap) {
    char msg[4096];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    size_t len = strlen(msg);
    log->write(lvl, msg, len);
}

void Logger::debug(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vformat_and_write(this, LogLevel::DEBUG, fmt, ap); va_end(ap); }
void Logger::info (const char* fmt, ...) { va_list ap; va_start(ap, fmt); vformat_and_write(this, LogLevel::INFO,  fmt, ap); va_end(ap); }
void Logger::warn (const char* fmt, ...) { va_list ap; va_start(ap, fmt); vformat_and_write(this, LogLevel::WARN,  fmt, ap); va_end(ap); }
void Logger::error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vformat_and_write(this, LogLevel::ERROR, fmt, ap); va_end(ap); }

} // namespace framework
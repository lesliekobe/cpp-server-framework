/*
 * logger.h - 分级日志 + 按大小滚动
 *
 * 特性：
 * - 4 级：DEBUG / INFO / WARN / ERROR
 * - 控制台 + 文件同时输出
 * - 文件按大小滚动（超过 max_size 则关闭重命名新建）
 * - 线程安全
 */

#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <atomic>

namespace framework {

enum class LogLevel : int {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger* instance();

    // 初始化：filename 日志文件名（自动加日期后缀），max_size 单文件最大字节数
    bool init(const std::string& filename, LogLevel level = LogLevel::INFO,
             size_t max_size_mb = 10, size_t max_files = 5);

    void set_level(LogLevel level);

    void debug(const char* fmt, ...);
    void info (const char* fmt, ...);
    void warn (const char* fmt, ...);
    void error(const char* fmt, ...);

    void flush();

private:
    Logger()  = default;
    ~Logger();

    void write(LogLevel level, const char* msg, size_t len);
    void rotate_file();

    std::atomic<LogLevel> level_{LogLevel::INFO};
    int fd_{ -1 };
    std::string filename_base_;
    size_t max_size_{ 10 * 1024 * 1024 };
    size_t max_files_{ 5 };
    std::atomic<size_t> file_size_{ 0 };
    char  buf_[4096];
};

// 便捷宏
#define LOG_DEBUG(...) framework::Logger::instance()->debug(__VA_ARGS__)
#define LOG_INFO(...)  framework::Logger::instance()->info(__VA_ARGS__)
#define LOG_WARN(...)  framework::Logger::instance()->warn(__VA_ARGS__)
#define LOG_ERROR(...) framework::Logger::instance()->error(__VA_ARGS__)

} // namespace framework
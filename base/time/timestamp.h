/*
 * timestamp.h - 时间工具
 *
 * - 时间戳（秒/毫秒/微秒）
 * - 格式化
 * - TimeT <-> DateTime 互转
 */

#pragma once

#include <cstdint>
#include <string>

namespace framework {
namespace time {

// 获取时间戳
int64_t now_seconds();          // Unix 秒
int64_t now_milliseconds();     // Unix 毫秒
int64_t now_microseconds();     // Unix 微秒

// 格式化
std::string format_now(const char* fmt = "%Y-%m-%d %H:%M:%S");         // 默认 "2025-01-01 12:00:00"
std::string format_timestamp(int64_t ts, const char* fmt = "%Y-%m-%d %H:%M:%S");

// sleep
void sleep_ms(int64_t ms);

} // namespace time
} // namespace framework
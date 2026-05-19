/*
 * timestamp.cpp
 */
#include "timestamp.h"
#include "adapter/platform.h"
#include <cstdio>
#include <sys/time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <time.h>
#endif

namespace framework {
namespace time {

int64_t now_seconds() {
    return (int64_t)time(nullptr);
}

int64_t now_milliseconds() {
    return framework::get_tick_ms();
}

int64_t now_microseconds() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    int64_t t = (int64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
    return t / 10;  // 100-ns to us
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

std::string format_now(const char* fmt) {
    return format_timestamp(now_seconds(), fmt);
}

std::string format_timestamp(int64_t ts, const char* fmt) {
    time_t t = (time_t)ts;
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), fmt, &tm_buf);
    return std::string(buf);
}

void sleep_ms(int64_t ms) {
    framework::sleep_ms((int)ms);
}

} // namespace time
} // namespace framework
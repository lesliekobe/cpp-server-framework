/*
 * mutex.h - 互斥锁 RAII 封装
 */

#pragma once

#include "adapter/platform.h"
#include <mutex>

namespace framework {

using Mutex = std::mutex;

class LockGuard {
public:
    explicit LockGuard(Mutex& mtx) : mtx_(mtx) { mtx_.lock(); }
    ~LockGuard() { mtx_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& mtx_;
};

} // namespace framework
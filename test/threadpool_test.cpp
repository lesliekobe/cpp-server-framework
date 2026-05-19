/*
 * test/threadpool_test.cpp
 */

#include <iostream>
#include <chrono>
#include "core/threadpool/thread_pool.h"
#include "base/logger/logger.h"

using namespace framework;

int main() {
    Logger::instance()->init("test.log", LogLevel::DEBUG);

    ThreadPool pool(4);
    std::atomic<int> counter{0};

    LOG_INFO("submitting 20 tasks...");
    for (int i = 0; i < 20; ++i) {
        pool.append([i, &counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            counter.fetch_add(1, std::memory_order_release);
            LOG_DEBUG("task %d done, counter=%d", i, counter.load());
        });
    }

    while (counter.load() < 20) {
        sleep_ms(100);
    }

    LOG_INFO("all 20 tasks completed");
    pool.shutdown();
    LOG_INFO("test passed");
    return 0;
}
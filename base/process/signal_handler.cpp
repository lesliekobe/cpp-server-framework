/*
 * signal_handler.cpp
 */
#include "base/process/signal_handler.h"
#include "logger/logger.h"
#include "base/event/event_loop.h"
#include <csignal>
#include <csetjmp>
#include <thread>

namespace framework {

// ============ SignalHandler ============
SignalHandler::SignalHandler() = default;
SignalHandler::~SignalHandler() = default;

void SignalHandler::on_signal(Signal sig, std::function<void(Signal)> cb) {
    // 设置信号处理（简化：使用 sigaction 或 signal()）
    (void)sig; (void)cb;
    // 实际实现依赖于平台 sigaction
    // 这里注册到全局
}

void SignalHandler::on_shutdown(ShutdownCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mtx_);
    shutdown_callbacks_.push_back(std::move(cb));
}

void SignalHandler::start() {
    // 在 Linux 上使用 sigaction
#ifdef PLATFORM_LINUX
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int sig) {
        // 设置标志位，由主循环检测
        (void)sig;
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
#elif defined(PLATFORM_WINDOWS)
    // Windows 上用 SetConsoleCtrlHandler
    SetConsoleCtrlHandler([](DWORD ctrl_type) -> BOOL {
        (void)ctrl_type;
        return FALSE; // 不阻止，让默认处理
    }, TRUE);
#endif
    LOG_INFO("SignalHandler: started");
}

void SignalHandler::request_shutdown() {
    if (shutdown_requested_.load()) return;
    shutdown_requested_.store(true, std::memory_order_release);
    LOG_INFO("SignalHandler: shutdown requested");
}

void SignalHandler::wait_for_shutdown() {
    while (!shutdown_requested_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void SignalHandler::invoke_shutdown() {
    LOG_INFO("SignalHandler: invoking shutdown callbacks...");
    std::vector<ShutdownCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(callbacks_mtx_);
        callbacks = shutdown_callbacks_;
    }
    for (auto& cb : callbacks) {
        try {
            cb();
        } catch (const std::exception& e) {
            LOG_ERROR("SignalHandler: shutdown callback exception: %s", e.what());
        }
    }
    LOG_INFO("SignalHandler: shutdown complete");
}

} // namespace framework
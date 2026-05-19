/*
 * signal_handler.h - 信号处理 & 优雅退出
 *
 * 捕获 SIGINT/SIGTERM/SIGHUP，平滑关闭
 */

#pragma once

#include <functional>
#include <atomic>
#include <csignal>

namespace framework {

// ============ 信号类型 ============
enum class Signal {
    INT  = SIGINT,
    TERM = SIGTERM,
    HUP  = SIGHUP,
#if !defined(_WIN32)
    QUIT = SIGQUIT,
    USR1 = SIGUSR1,
    USR2 = SIGUSR2,
#endif
};

// ============ 信号处理器 ============
class SignalHandler {
public:
    SignalHandler();
    ~SignalHandler();

    // 禁止拷贝
    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    // 注册信号回调
    void on_signal(Signal sig, std::function<void(Signal)> cb);

    // 注册优雅退出回调（按顺序执行）
    using ShutdownCallback = std::function<void()>;
    void on_shutdown(ShutdownCallback cb);

    // 开始监听信号（调用后主线程需要自行处理退出）
    void start();

    // 请求关闭（可从信号回调调用）
    void request_shutdown();

    // 是否已请求关闭
    bool is_shutting_down() const {
        return shutdown_requested_.load(std::memory_order_acquire);
    }

    // 等待关闭完成
    void wait_for_shutdown();

    // 执行所有 shutdown 回调
    void invoke_shutdown();

private:
    std::atomic<bool>          shutdown_requested_{false};
    std::vector<ShutdownCallback> shutdown_callbacks_;
    std::mutex                 callbacks_mtx_;

    // 内部信号处理
    static void global_handler(int sig);
};

} // namespace framework
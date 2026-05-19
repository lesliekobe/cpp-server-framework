/*
 * example/server/main.cpp - TCP 服务端示例
 */

#include <iostream>
#include <csignal>
#include "tcp/tcp_server.h"
#include "core/threadpool/thread_pool.h"
#include "base/logger/logger.h"
#include "protocol/packet.h"

using namespace framework;

static std::unique_ptr<TcpServer> g_server;

void signal_handler(int) {
    LOG_INFO("received signal, shutting down...");
    if (g_server) g_server->stop();
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化日志
    Logger::instance()->init("server.log", LogLevel::INFO);

    int port = (argc > 1) ? std::atoi(argv[1]) : 8080;
    LOG_INFO("starting TCP server on port %d", port);

    // 创建线程池（4个工作线程）
    ThreadPool pool(4);
    LOG_INFO("thread pool created with %d workers", pool.worker_count());

    // 创建并启动 TCP 服务端
    g_server = std::make_unique<TcpServer>(port, 4);
    g_server->set_message_handler([&pool](int64_t sid, const protocol::Packet& pkt) {
        // 业务逻辑投递到线程池异步执行
        pool.append([sid, pkt]() {
            LOG_INFO("session %ld cmd=0x%04X data_len=%zu",
                     sid, pkt.cmd, pkt.data.size());

            // 这里处理具体业务
            // 根据 cmd 分发到不同业务处理
        });
    });

    g_server->set_connected_handler([](int64_t sid) {
        LOG_INFO("client connected: session %ld", sid);
    });

    g_server->set_close_handler([](int64_t sid) {
        LOG_INFO("client disconnected: session %ld", sid);
    });

    g_server->set_heartbeat_interval(10000);
    g_server->set_idle_timeout(30000);

    if (!g_server->start()) {
        LOG_ERROR("server start failed");
        return 1;
    }

    LOG_INFO("server running, press Ctrl+C to stop");

    // 主线程阻塞
    while (true) {
        sleep_ms(5000);
        LOG_DEBUG("server alive, sessions=%zu", g_server->session_count());
    }

    return 0;
}
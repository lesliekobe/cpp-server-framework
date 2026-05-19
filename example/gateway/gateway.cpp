/*
 * example/gateway/gateway.cpp - 网关示例
 *
 * 功能：
 * - TCP 服务端接收客户端连接
 * - 客户端登录认证
 * - 消息路由到线程池处理
 * - 响应客户端
 * - 会话管理
 */

#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>
#include "tcp/tcp_server.h"
#include "core/threadpool/thread_pool.h"
#include "base/logger/logger.h"
#include "protocol/packet.h"
#include "business/session/session_mgr.h"
#include "business/router/router.h"

using namespace framework;

static std::unique_ptr<TcpServer>     g_server;
static std::unique_ptr<ThreadPool>    g_pool;
static std::unique_ptr<SessionMgr>     g_sessions;
static std::unique_ptr<Router>        g_router;
static std::atomic<bool>              g_running{false};

constexpr uint16_t CMD_LOGIN   = 0x0002;
constexpr uint16_t CMD_LOGOUT  = 0x0003;
constexpr uint16_t CMD_DATA    = 0x0004;

void signal_handler(int) {
    LOG_INFO("shutting down gateway...");
    g_running = false;
    if (g_server) g_server->stop();
    if (g_pool)   g_pool->shutdown();
    exit(0);
}

void handle_login(int64_t sid, const std::string& data) {
    LOG_INFO("session %ld login request: %s", sid, data.c_str());
    // 简单验证（实际应解析 JSON/Protobuf）
    std::string response = "login_ok";
    g_server->send_to(sid, CMD_LOGIN, response);
    LOG_INFO("session %ld login success", sid);
}

void handle_data(int64_t sid, const std::string& data) {
    LOG_INFO("session %ld data: %zu bytes", sid, data.size());
    // 模拟处理：原样回发
    g_server->send_to(sid, CMD_DATA, data);
}

void handle_logout(int64_t sid, const std::string& data) {
    LOG_INFO("session %ld logout", sid);
    g_sessions->remove(sid);
}

int main(int argc, char* argv[]) {
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    Logger::instance()->init("gateway.log", LogLevel::INFO);

    int port = (argc > 1) ? std::atoi(argv[1]) : 8888;
    LOG_INFO("starting gateway on port %d", port);

    g_pool      = std::make_unique<ThreadPool>(8);
    g_sessions  = std::make_unique<SessionMgr>();
    g_router    = std::make_unique<Router>();

    // 注册路由
    g_router->reg(CMD_LOGIN,  handle_login);
    g_router->reg(CMD_LOGOUT, handle_logout);
    g_router->reg(CMD_DATA,  handle_data);

    g_server = std::make_unique<TcpServer>(port, 8);

    g_server->set_message_handler([](int64_t sid, const protocol::Packet& pkt) {
        g_pool->append([sid, pkt]() {
            LOG_DEBUG("routing cmd=0x%04X for session %ld", pkt.cmd, sid);
            g_router->route(sid, pkt);
        });
    });

    g_server->set_connected_handler([](int64_t sid) {
        LOG_INFO("client connected: session %ld", sid);
        g_sessions->add(sid, "0.0.0.0", 0);
    });

    g_server->set_close_handler([](int64_t sid) {
        LOG_INFO("client disconnected: session %ld", sid);
        g_sessions->remove(sid);
    });

    g_server->set_heartbeat_interval(10000);
    g_server->set_idle_timeout(30000);

    if (!g_server->start()) {
        LOG_ERROR("gateway start failed");
        return 1;
    }

    g_running = true;
    LOG_INFO("gateway running on port %d", port);

    // 定期上报状态
    while (g_running.load()) {
        sleep_ms(10000);
        LOG_INFO("status: sessions=%zu, queued=%zu",
                g_server->session_count(), g_pool->queued_count());
    }

    return 0;
}
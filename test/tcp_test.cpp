/*
 * test/tcp_test.cpp - TCP 网络测试
 */

#include <iostream>
#include <csignal>
#include "tcp/tcp_server.h"
#include "tcp/tcp_client.h"
#include "protocol/packet.h"
#include "base/logger/logger.h"

using namespace framework;

static std::unique_ptr<TcpServer> g_server;

void signal_handler(int) {
    if (g_server) g_server->stop();
    exit(0);
}

int main() {
    signal(SIGINT, signal_handler);
    Logger::instance()->init("tcp_test.log", LogLevel::INFO);

    g_server = std::make_unique<TcpServer>(9999, 4);

    g_server->set_connected_handler([](int64_t sid) {
        LOG_INFO("client connected: session %ld", sid);
    });

    g_server->set_message_handler([](int64_t sid, const protocol::Packet& pkt) {
        LOG_INFO("session %ld cmd=0x%04X data=%s", sid, pkt.cmd, pkt.data.c_str());
        g_server->send_to(sid, pkt.cmd, "echo: " + pkt.data);
    });

    g_server->set_close_handler([](int64_t sid) {
        LOG_INFO("session %ld closed", sid);
    });

    if (!g_server->start()) {
        LOG_ERROR("server start failed");
        return 1;
    }

    LOG_INFO("TCP test server running on port 9999");
    while (true) sleep_ms(1000);
    return 0;
}
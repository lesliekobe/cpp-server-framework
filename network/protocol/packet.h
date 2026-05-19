/*
 * packet.h - 自定义数据包协议
 *
 * 协议格式：
 *   [Length: 4B] [Cmd: 2B] [Data: N B]
 *   Length = 整个包字节数（大端序）
 *   Cmd    = 命令字
 *   Data   = 业务数据
 *
 * 防粘包：基于长度域分包
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace framework {
namespace protocol {

// 当前协议版本
constexpr uint16_t PROTOCOL_VERSION = 1;

// 默认命令字
constexpr uint16_t CMD_HEARTBEAT     = 0x0001;
constexpr uint16_t CMD_LOGIN        = 0x0002;
constexpr uint16_t CMD_LOGOUT       = 0x0003;
constexpr uint16_t CMD_DATA         = 0x0004;

// 包结构
struct Packet {
    uint16_t    cmd;            // 命令字
    std::string data;           // 数据

    Packet() : cmd(0) {}
    Packet(uint16_t c, const std::string& d) : cmd(c), data(d) {}
};

// 编码：Packet -> bytes
std::vector<uint8_t> encode(const Packet& pkt);

// 解码：bytes -> Packet（不完整返回空）
// 若缓冲中有多个包，逐个返回
bool decode(const std::vector<uint8_t>& buf, Packet& pkt, size_t& consumed);

// 从容器的指定偏移量开始解码（用于 TcpConnection 内部）
bool decode_from(const uint8_t* data, size_t len, Packet& pkt, size_t& consumed);

// 获取完整包长度（读取 length 字段）
// 返回 0 表示数据不完整
size_t peek_packet_length(const uint8_t* data, size_t len);

// 解析包头
bool parse_header(const uint8_t* data, size_t len, uint32_t& out_length, uint16_t& out_cmd);

} // namespace protocol
} // namespace framework
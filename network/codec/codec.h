/*
 * codec.h - 协议编解码模块
 *
 * Binary Protocol Format:
 *   [Length:4B][Version:1B][CRC32:4B][Seq:4B][Cmd:2B][Data:N]
 *
 * Features:
 *   - CRC32 校验
 *   - Sequence number（防重放）
 *   - ByteOrder 大小端转换
 *   - Protobuf 兼容扩展点（codec 注册接口）
 */

#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <unordered_map>
#include <string>
#include <optional>

namespace framework {

// ============ 版本 & 常量 ============
constexpr uint8_t  PROTOCOL_VERSION = 1;
constexpr size_t   HEADER_SIZE = 4 + 1 + 4 + 4 + 2; // 15 bytes
constexpr size_t   MAX_PACKET_SIZE = 10 * 1024 * 1024; // 10MB

// ============ 字节序 ============
class ByteOrder {
public:
    static uint16_t htons(uint16_t v);
    static uint16_t ntohs(uint16_t v);
    static uint32_t htonl(uint32_t v);
    static uint32_t ntohl(uint32_t v);
    static uint64_t htonll(uint64_t v);
    static uint64_t ntohll(uint64_t v);

    static bool is_little_endian();
    static void init();
};

// ============ CRC32 ============
class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t len);
    static bool     verify(const void* data, size_t len, uint32_t expected);
};

// ============ 协议头 ============
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t length;   // 整个包长度（不含 length 自身）
    uint8_t  version;  // 协议版本
    uint32_t crc32;    // CRC32（从 version 到 data 末尾）
    uint32_t seq;      // 序列号
    uint16_t cmd;      // 命令字
    // Data follows
};
#pragma pack(pop)

// ============ 原始数据包 ============
struct Packet {
    uint32_t    seq{0};
    uint16_t    cmd{0};
    std::string data;  // 原始载荷

    std::string encode() const;   // 编码为二进制
    bool       decode(const uint8_t* raw, size_t len); // 解码（内部验证 CRC）
};

// ============ Codec 编解码器 ============
class Codec {
public:
    Codec();

    // 注册命令字处理器
    using Handler = std::function<void(uint32_t seq, const uint8_t* data, size_t len)>;
    void register_handler(uint16_t cmd, Handler h);
    void unregister_handler(uint16_t cmd);

    // 编码
    std::vector<uint8_t> encode(uint16_t cmd, const void* data, size_t len, uint32_t seq = 0);

    // 解码输入流，返回已处理的包
    std::optional<Packet> decode(const uint8_t* buf, size_t len);

    // 完整的 decode（输入完整数据）
    std::optional<Packet> decode_packet(const std::vector<uint8_t>& buf);

    // 获取当前解码器状态（未完成的数据）
    const std::vector<uint8_t>& buffered() const { return buffer_; }

    // 清空 buffer
    void clear_buffer() { buffer_.clear(); }

    // CRC 校验
    uint32_t compute_crc(const void* data, size_t len) const {
        return CRC32::calculate(data, len);
    }

private:
    // 内部解码逻辑
    std::optional<Packet> try_decode();

    std::vector<uint8_t>              buffer_;
    std::unordered_map<uint16_t, Handler> handlers_;
    uint32_t                          next_seq_{1};

    // 解码状态
    enum class DecodeState { READ_HEADER, READ_BODY };
    DecodeState                      state_{DecodeState::READ_HEADER};
    size_t                           header_pos_{0};
    size_t                           body_pos_{0};
    PacketHeader                     header_;
    std::vector<uint8_t>             body_buffer_;
};

// ============ Protobuf 兼容层（扩展点） ============
// 可注册自定义序列化器，用于 PB 或其他格式
using ProtoSerializer = std::function<std::vector<uint8_t>(const void* msg)>;
using ProtoDeserializer = std::function<bool(void* msg, const uint8_t* data, size_t len)>;

class ProtoRegistry {
public:
    static ProtoRegistry& instance();

    void register_serializer(const std::string& type_name, ProtoSerializer s);
    void register_deserializer(const std::string& type_name, ProtoDeserializer d);

    std::optional<std::vector<uint8_t>> serialize(const std::string& type_name, const void* msg);
    bool deserialize(const std::string& type_name, void* msg, const uint8_t* data, size_t len);

private:
    std::unordered_map<std::string, ProtoSerializer>   serializers_;
    std::unordered_map<std::string, ProtoDeserializer> deserializers_;
};

// ============ Protobuf 兼容消息封装 ============
// 简单的 PB-like 接口，真实 PB 需要 protoc 生成的代码
class ProtoMessage {
public:
    virtual ~ProtoMessage() = default;
    virtual std::string type_name() const = 0;
    virtual std::vector<uint8_t> Serialize() const = 0;
    virtual bool ParseFrom(const uint8_t* data, size_t len) = 0;
};

} // namespace framework
/*
 * codec.cpp
 */
#include "network/codec/codec.h"
#include "logger/logger.h"
#include <cstring>
#include <algorithm>

namespace framework {

// ============ ByteOrder ============
static bool g_little_endian = false;

bool ByteOrder::is_little_endian() { return g_little_endian; }

void ByteOrder::init() {
    uint16_t v = 0x1234;
    g_little_endian = (*(uint8_t*)&v == 0x34);
}

uint16_t ByteOrder::htons(uint16_t v) {
    if (!g_little_endian) return v;
    return (v >> 8) | (v << 8);
}
uint16_t ByteOrder::ntohs(uint16_t v) { return htons(v); }
uint32_t ByteOrder::htonl(uint32_t v) {
    if (!g_little_endian) return v;
    return ((v & 0xFF000000) >> 24) |
           ((v & 0x00FF0000) >> 8) |
           ((v & 0x0000FF00) << 8) |
           ((v & 0x000000FF) << 24);
}
uint32_t ByteOrder::ntohl(uint32_t v) { return htonl(v); }
uint64_t ByteOrder::htonll(uint64_t v) {
    if (!g_little_endian) return v;
    return ((v & 0xFF00000000000000ULL) >> 56) |
           ((v & 0x00FF000000000000ULL) >> 40) |
           ((v & 0x0000FF0000000000ULL) >> 24) |
           ((v & 0x000000FF00000000ULL) >> 8) |
           ((v & 0x00000000FF000000ULL) << 8) |
           ((v & 0x0000000000FF0000ULL) << 24) |
           ((v & 0x000000000000FF00ULL) << 40) |
           ((v & 0x00000000000000FFULL) << 56);
}
uint64_t ByteOrder::ntohll(uint64_t v) { return htonll(v); }

// ============ CRC32 ============
static uint32_t crc32_table[256];

static void init_crc32_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
}

uint32_t CRC32::calculate(const void* data, size_t len) {
    static bool init = false;
    if (!init) { init_crc32_table(); init = true; }

    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool CRC32::verify(const void* data, size_t len, uint32_t expected) {
    return calculate(data, len) == expected;
}

// ============ Codec ============
Codec::Codec() {
    ByteOrder::init();
}

void Codec::register_handler(uint16_t cmd, Handler h) {
    handlers_[cmd] = std::move(h);
}

void Codec::unregister_handler(uint16_t cmd) {
    handlers_.erase(cmd);
}

std::vector<uint8_t> Codec::encode(uint16_t cmd, const void* data, size_t len, uint32_t seq) {
    std::vector<uint8_t> out;
    out.reserve(HEADER_SIZE + len);

    // placeholder for header
    out.resize(HEADER_SIZE, 0);

    // body
    const uint8_t* udata = (const uint8_t*)data;
    out.insert(out.end(), udata, udata + len);

    // fill header
    uint32_t body_len = (uint32_t)(HEADER_SIZE - 4 + len); // length field excludes itself
    uint32_t total_len = (uint32_t)(HEADER_SIZE + len);

    // CRC from version to end
    uint32_t crc = CRC32::calculate(out.data() + 4 + 1, total_len - 4 - 1);

    // 注意：需要小端处理
    uint32_t length_be = ByteOrder::htonl(total_len);
    uint8_t  version_be = PROTOCOL_VERSION;
    uint32_t crc_be = ByteOrder::htonl(crc);
    uint32_t seq_be = ByteOrder::htonl(seq);
    uint16_t cmd_be = ByteOrder::htons(cmd);

    memcpy(out.data(), &length_be, 4);
    out[4] = version_be;
    memcpy(out.data() + 5, &crc_be, 4);
    memcpy(out.data() + 9, &seq_be, 4);
    memcpy(out.data() + 13, &cmd_be, 2);

    return out;
}

std::optional<Packet> Codec::decode(const uint8_t* buf, size_t len) {
    // 追加到 buffer
    buffer_.insert(buffer_.end(), buf, buf + len);
    return try_decode();
}

std::optional<Packet> Codec::decode_packet(const std::vector<uint8_t>& buf) {
    return decode(buf.data(), buf.size());
}

std::optional<Packet> Codec::try_decode() {
    while (buffer_.size() >= HEADER_SIZE) {
        // 检查长度字段
        uint32_t total_len;
        memcpy(&total_len, buffer_.data(), 4);
        total_len = ByteOrder::ntohl(total_len);

        if (total_len < HEADER_SIZE || total_len > MAX_PACKET_SIZE) {
            LOG_WARN("Codec: invalid packet length %u, clearing buffer", total_len);
            buffer_.clear();
            return std::nullopt;
        }

        if (buffer_.size() < total_len) {
            // 数据不完整
            return std::nullopt;
        }

        // 解析 header
        PacketHeader hdr;
        memcpy(&hdr, buffer_.data(), HEADER_SIZE);
        hdr.length = ByteOrder::ntohl(hdr.length);
        hdr.crc32  = ByteOrder::ntohl(hdr.crc32);
        hdr.seq    = ByteOrder::ntohl(hdr.seq);
        hdr.cmd    = ByteOrder::ntohs(hdr.cmd);

        // CRC 校验
        uint32_t crc = CRC32::calculate(buffer_.data() + 4 + 1, total_len - 4 - 1);
        if (crc != hdr.crc32) {
            LOG_WARN("Codec: CRC mismatch expected=%08x got=%08x", hdr.crc32, crc);
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1); // 跳过 1 字节尝试对齐
            continue;
        }

        // 提取 body
        std::string body;
        if (total_len > HEADER_SIZE) {
            body.assign((char*)buffer_.data() + HEADER_SIZE, total_len - HEADER_SIZE);
        }

        // 移除已处理数据
        buffer_.erase(buffer_.begin(), buffer_.begin() + total_len);

        Packet pkt;
        pkt.seq = hdr.seq;
        pkt.cmd = hdr.cmd;
        pkt.data = std::move(body);
        return pkt;
    }
    return std::nullopt;
}

// ============ Packet ============
std::string Packet::encode() const {
    Codec codec;
    auto vec = codec.encode(cmd, data.data(), data.size(), seq);
    return std::string((char*)vec.data(), vec.size());
}

bool Packet::decode(const uint8_t* raw, size_t len) {
    Codec codec;
    auto pkt = codec.decode(raw, len);
    if (!pkt) return false;
    seq = pkt->seq;
    cmd = pkt->cmd;
    data = std::move(pkt->data);
    return true;
}

// ============ ProtoRegistry ============
ProtoRegistry& ProtoRegistry::instance() {
    static ProtoRegistry r;
    return r;
}
void ProtoRegistry::register_serializer(const std::string& type_name, ProtoSerializer s) {
    serializers_[type_name] = std::move(s);
}
void ProtoRegistry::register_deserializer(const std::string& type_name, ProtoDeserializer d) {
    deserializers_[type_name] = std::move(d);
}
std::optional<std::vector<uint8_t>> ProtoRegistry::serialize(const std::string& type_name, const void* msg) {
    auto it = serializers_.find(type_name);
    if (it == serializers_.end()) return std::nullopt;
    return it->second(msg);
}
bool ProtoRegistry::deserialize(const std::string& type_name, void* msg, const uint8_t* data, size_t len) {
    auto it = deserializers_.find(type_name);
    if (it == deserializers_.end()) return false;
    return it->second(msg, data, len);
}

} // namespace framework
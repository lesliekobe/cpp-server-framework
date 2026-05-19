/*
 * packet.cpp
 */
#include "packet.h"
#include <cstring>
#include <arpa/inet.h>

namespace framework {
namespace protocol {

std::vector<uint8_t> encode(const Packet& pkt) {
    std::vector<uint8_t> buf;
    uint32_t total_len = 4 + 2 + pkt.data.size();  // length(4) + cmd(2) + data(N)
    uint32_t len_be = htonl(total_len);
    uint16_t cmd_be = htons(pkt.cmd);

    buf.resize(4 + 2 + pkt.data.size());
    std::memcpy(&buf[0], &len_be, 4);
    std::memcpy(&buf[4], &cmd_be, 2);
    if (!pkt.data.empty()) {
        std::memcpy(&buf[6], pkt.data.data(), pkt.data.size());
    }
    return buf;
}

bool parse_header(const uint8_t* data, size_t len, uint32_t& out_length, uint16_t& out_cmd) {
    if (len < 6) return false;
    uint32_t len_be, cmd_be;
    std::memcpy(&len_be, data, 4);
    std::memcpy(&cmd_be, data + 4, 2);
    out_length = ntohl(len_be);
    out_cmd    = ntohs(cmd_be);
    return true;
}

size_t peek_packet_length(const uint8_t* data, size_t len) {
    if (len < 4) return 0;
    uint32_t total_len_be;
    std::memcpy(&total_len_be, data, 4);
    uint32_t total_len = ntohl(total_len_be);
    return total_len;
}

bool decode_from(const uint8_t* data, size_t len, Packet& pkt, size_t& consumed) {
    if (len < 4) return false;

    uint32_t total_len = peek_packet_length(data, len);
    if (total_len == 0) return false;

    // 数据不完整
    if (len < total_len) return false;

    // 解析命令字
    uint16_t cmd_be;
    std::memcpy(&cmd_be, data + 4, 2);
    pkt.cmd = ntohs(cmd_be);

    // 复制数据
    size_t data_len = total_len - 6;
    if (data_len > 0) {
        pkt.data.assign((const char*)data + 6, data_len);
    } else {
        pkt.data.clear();
    }

    consumed = total_len;
    return true;
}

bool decode(const std::vector<uint8_t>& buf, Packet& pkt, size_t& consumed) {
    return decode_from(buf.data(), buf.size(), pkt, consumed);
}

} // namespace protocol
} // namespace framework
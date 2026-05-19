/*
 * crc.h - CRC32 校验（独立头文件，供外部使用）
 */
#pragma once
#include <cstdint>
#include <cstddef>

namespace framework {

uint32_t crc32_calculate(const void* data, size_t len);
bool     crc32_verify(const void* data, size_t len, uint32_t expected);

} // namespace framework
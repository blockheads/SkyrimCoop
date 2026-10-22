#pragma once

#include <cstddef>
#include <cstdint>

// CRC-64 (ECMA-182) of the bytes: the key of an animation graph's descriptor (encoding's AnimationGraphDescriptor tables).
uint64_t Crc64(const unsigned char* acpData, std::size_t aSize);

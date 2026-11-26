#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace ScriptAssignments
{
// Interpreter 23329: opcode 0x15, 'G', little-endian reference-list index. Skip/scan never execute the store.
inline std::optional<uint16_t> GlobalTarget(std::span<const uint8_t> aData, uint32_t aOpcode, uint32_t aOffset,
                                            bool aSkip, bool aScan) noexcept
{
    if (aSkip || aScan || aOpcode != 0x15 || aOffset > aData.size() || aData.size() - aOffset < 3 ||
        aData[aOffset] != 'G')
        return std::nullopt;
    const uint16_t cIndex = aData[aOffset + 1] | (static_cast<uint16_t>(aData[aOffset + 2]) << 8);
    return cIndex && cIndex <= INT16_MAX ? std::optional<uint16_t>{cIndex} : std::nullopt;
}
} // namespace ScriptAssignments

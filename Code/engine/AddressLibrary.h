#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <limits>
#include <map>
#include <string>

namespace AddressLibrary
{
struct Data
{
    std::string Module;
    std::map<unsigned long long, unsigned long long> Offsets;
};

template <class T> bool Read(std::istream& aFile, T& aValue)
{
    return static_cast<bool>(aFile.read(reinterpret_cast<char*>(&aValue), sizeof(aValue)));
}

inline bool Delta(std::istream& aFile, uint8_t aKind, uint64_t aPrevious, uint64_t& aValue)
{
    uint8_t byte{};
    uint16_t word{};
    uint32_t dword{};
    uint64_t difference{};
    switch (aKind)
    {
    case 0: return Read(aFile, aValue);
    case 1: difference = 1; break;
    case 2: case 3:
        if (!Read(aFile, byte)) return false;
        difference = byte;
        break;
    case 4: case 5:
        if (!Read(aFile, word)) return false;
        difference = word;
        break;
    case 6:
        if (!Read(aFile, word)) return false;
        aValue = word;
        return true;
    case 7:
        if (!Read(aFile, dword)) return false;
        aValue = dword;
        return true;
    default: return false;
    }
    if (aKind == 3 || aKind == 5)
    {
        if (difference > aPrevious) return false;
        aValue = aPrevious - difference;
    }
    else
    {
        if (difference > std::numeric_limits<uint64_t>::max() - aPrevious) return false;
        aValue = aPrevious + difference;
    }
    return true;
}

// meh321's v2 delta records and v5 dense offsets (TiltedEvolution #885; CommonLibSSE-NG IDDB).
inline bool Load(std::istream& aFile, const std::array<int, 4>& acVersion, Data& aData)
{
    aData = {};
    Data parsed;
    int32_t format{};
    std::array<int32_t, 4> version{};
    if (!Read(aFile, format) || (format != 2 && format != 5) || !Read(aFile, version) || version != acVersion)
        return false;
    int32_t pointerSize{}, count{};
    if (format == 5)
    {
        std::array<char, 64> name{};
        int32_t dataFormat{};
        if (!Read(aFile, name) || name.back() != '\0' || !Read(aFile, pointerSize) ||
            !Read(aFile, dataFormat) || dataFormat != 0 || !Read(aFile, count))
            return false;
        parsed.Module = name.data();
    }
    else
    {
        int32_t length{};
        if (!Read(aFile, length) || length <= 0 || length > 256)
            return false;
        parsed.Module.resize(length);
        if (!aFile.read(parsed.Module.data(), length) || !Read(aFile, pointerSize) || !Read(aFile, count))
            return false;
    }
    if (parsed.Module != "SkyrimSE.exe" || pointerSize != 8 || count <= 0)
        return false;
    const auto start = aFile.tellg();
    aFile.seekg(0, std::ios::end);
    const auto end = aFile.tellg();
    aFile.seekg(start);
    const uint64_t minimumBytes = static_cast<uint64_t>(count) * (format == 5 ? 4 : 1);
    if (!aFile || start < 0 || end < start || static_cast<uint64_t>(end - start) < minimumBytes)
        return false;
    uint64_t previousId{}, previousOffset{};
    for (uint32_t index = 0; index < static_cast<uint32_t>(count); ++index)
    {
        uint64_t id = index, offset{};
        if (format == 5)
        {
            uint32_t value{};
            if (!Read(aFile, value)) return false;
            offset = value;
        }
        else
        {
            uint8_t kind{};
            if (!Read(aFile, kind) || !Delta(aFile, kind & 0xF, previousId, id)) return false;
            const bool scaled = (kind & 0x80) != 0;
            if (!Delta(aFile, (kind >> 4) & 7, scaled ? previousOffset / pointerSize : previousOffset, offset))
                return false;
            if (scaled)
            {
                if (offset > std::numeric_limits<uint64_t>::max() / pointerSize) return false;
                offset *= pointerSize;
            }
        }
        if (offset && !parsed.Offsets.emplace(id, offset).second)
            return false;
        previousId = id;
        previousOffset = offset;
    }
    if (parsed.Offsets.empty()) return false;
    aData = std::move(parsed);
    return true;
}
}

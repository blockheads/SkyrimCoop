#pragma once

#include <array>
#include <cstdint>
#include <istream>
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

// meh321's v5 dense offsets, zero = absent (TiltedEvolution #885; CommonLibSSE-NG IDDB).
inline bool Load(std::istream& aFile, const std::array<int, 4>& acVersion, Data& aData)
{
    aData = {};
    Data parsed;
    int32_t format{};
    std::array<int32_t, 4> version{};
    if (!Read(aFile, format) || format != 5 || !Read(aFile, version) || version != acVersion)
        return false;
    int32_t pointerSize{}, count{};
    std::array<char, 64> name{};
    int32_t dataFormat{};
    if (!Read(aFile, name) || name.back() != '\0' || !Read(aFile, pointerSize) || !Read(aFile, dataFormat) ||
        dataFormat != 0 || !Read(aFile, count))
        return false;
    parsed.Module = name.data();
    if (parsed.Module != "SkyrimSE.exe" || pointerSize != 8 || count <= 0)
        return false;
    const auto start = aFile.tellg();
    aFile.seekg(0, std::ios::end);
    const auto end = aFile.tellg();
    aFile.seekg(start);
    const uint64_t minimumBytes = static_cast<uint64_t>(count) * 4;
    if (!aFile || start < 0 || end < start || static_cast<uint64_t>(end - start) < minimumBytes)
        return false;
    for (uint32_t index = 0; index < static_cast<uint32_t>(count); ++index)
    {
        uint32_t offset{};
        if (!Read(aFile, offset)) return false;
        if (offset && !parsed.Offsets.emplace(index, offset).second)
            return false;
    }
    if (parsed.Offsets.empty()) return false;
    aData = std::move(parsed);
    return true;
}
}

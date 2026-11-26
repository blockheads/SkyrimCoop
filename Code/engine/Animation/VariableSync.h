#pragma once

#include <Havok/hkbVariableInfo.h>
#include <Structs/AnimationGraphDescriptor.h>
#include <Structs/AnimationVariables.h>

#include <bit>
#include <span>

// Reading and writing a graph's synced variables, each checked against its hkbVariableInfo type and bounds.
namespace VariableSync
{
inline bool NeedsDescriptor(uint64_t aHash, uint64_t aPreviousProject, uint64_t aProject) noexcept
{
    return !aHash || aPreviousProject != aProject;
}

inline bool CanAccess(uint32_t aIndex, size_t aWords, std::span<const hkbVariableInfo> acInfos,
                      hkbVariableInfo::Type aType) noexcept
{
    if (aIndex >= aWords || aIndex >= acInfos.size())
        return false;
    const auto cType = acInfos[aIndex].type;
    return aType == hkbVariableInfo::kInt32 ? cType >= hkbVariableInfo::kInt8 && cType <= hkbVariableInfo::kInt32
                                          : cType == aType;
}

inline AnimationVariables Read(const AnimationGraphDescriptor& acDescriptor, std::span<const uint32_t> acWords,
                              std::span<const hkbVariableInfo> acInfos) noexcept
{
    AnimationVariables variables;
    for (const uint32_t cIndex : acDescriptor.BooleanLookUpTable)
        variables.Booleans.push_back(CanAccess(cIndex, acWords.size(), acInfos, hkbVariableInfo::kBool) && acWords[cIndex] != 0);
    for (const uint32_t cIndex : acDescriptor.FloatLookupTable)
        variables.Floats.push_back(CanAccess(cIndex, acWords.size(), acInfos, hkbVariableInfo::kReal) ? std::bit_cast<float>(acWords[cIndex]) : 0.f);
    for (const uint32_t cIndex : acDescriptor.IntegerLookupTable)
        variables.Integers.push_back(CanAccess(cIndex, acWords.size(), acInfos, hkbVariableInfo::kInt32) ? acWords[cIndex] : 0);
    return variables;
}

inline void Write(const AnimationGraphDescriptor& acDescriptor, std::span<uint32_t> aWords,
                  std::span<const hkbVariableInfo> acInfos, const AnimationVariables& acVariables) noexcept
{
    for (size_t i = 0; i < acDescriptor.BooleanLookUpTable.size(); ++i)
        if (const auto cIndex = acDescriptor.BooleanLookUpTable[i]; CanAccess(cIndex, aWords.size(), acInfos, hkbVariableInfo::kBool))
            aWords[cIndex] = i < acVariables.Booleans.size() && acVariables.Booleans[i];
    for (size_t i = 0; i < acDescriptor.FloatLookupTable.size(); ++i)
        if (const auto cIndex = acDescriptor.FloatLookupTable[i]; CanAccess(cIndex, aWords.size(), acInfos, hkbVariableInfo::kReal))
            aWords[cIndex] = std::bit_cast<uint32_t>(i < acVariables.Floats.size() ? acVariables.Floats[i] : 0.f);
    for (size_t i = 0; i < acDescriptor.IntegerLookupTable.size(); ++i)
        if (const auto cIndex = acDescriptor.IntegerLookupTable[i]; CanAccess(cIndex, aWords.size(), acInfos, hkbVariableInfo::kInt32))
            aWords[cIndex] = i < acVariables.Integers.size() ? acVariables.Integers[i] : 0;
}
} // namespace VariableSync

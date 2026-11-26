#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace Followers
{
struct Place
{
    uint32_t World{};
    uint32_t Cell{};
    std::array<float, 3> Position{};
};

struct Member
{
    std::string Key;
    uint32_t Character{};
    bool Host{};
    bool Standing{};
    Place At;
};

// The desired member survives temporary fallback and replacement player copies; references belong to the host save.
struct Anchors
{
    void Follow(uint32_t aReference, std::string aKey) noexcept
    {
        if (aReference && !aKey.empty())
            m_bindings.insert_or_assign(aReference, std::move(aKey));
    }
    void Retain(std::span<const uint32_t> acReferences) noexcept
    {
        std::erase_if(m_bindings, [&](const auto& acBinding) {
            return std::ranges::find(acReferences, acBinding.first) == acReferences.end();
        });
    }
    [[nodiscard]] std::string_view Desired(uint32_t aReference) const noexcept
    {
        const auto it = m_bindings.find(aReference);
        return it == m_bindings.end() ? std::string_view{} : it->second;
    }
    [[nodiscard]] uint32_t Choose(uint32_t aReference, const Place& acAt,
                                  std::span<const Member> acMembers) const noexcept
    {
        const auto cDesired = Desired(aReference);
        const Member* pHost = nullptr;
        const Member* pNearest = nullptr;
        double nearestDistance = std::numeric_limits<double>::infinity();
        for (const auto& cMember : acMembers)
        {
            if (!cMember.Standing || !cMember.Character || cMember.Key.empty())
                continue;
            if (cMember.Key == cDesired)
                return cMember.Character;
            if (cMember.Host)
                pHost = &cMember;
            const bool cSameSpace =
                acAt.World ? acAt.World == cMember.At.World : !cMember.At.World && acAt.Cell == cMember.At.Cell;
            double distance = std::numeric_limits<double>::infinity();
            if (cSameSpace)
            {
                distance = 0;
                for (size_t i = 0; i < 3; ++i)
                    distance += std::pow(static_cast<double>(acAt.Position[i]) - cMember.At.Position[i], 2);
            }
            if (!pNearest || distance < nearestDistance || (distance == nearestDistance && cMember.Key < pNearest->Key))
            {
                pNearest = &cMember;
                nearestDistance = distance;
            }
        }
        return pHost ? pHost->Character : pNearest ? pNearest->Character : 0;
    }
    [[nodiscard]] const auto& All() const noexcept
    {
        return m_bindings;
    }
    void Clear() noexcept
    {
        m_bindings.clear();
    }

  private:
    std::map<uint32_t, std::string> m_bindings;
};
} // namespace Followers

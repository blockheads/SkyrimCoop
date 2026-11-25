#pragma once

#include <cstdint>
#include <map>

// Our collision groups on loose objects' bodies (docs/re/systems/physics.md, "The collision filter"): which game
// simulates a body decides what it meets here. The engine hands out groups counting up from 1; these are far above.
namespace BodyGroups
{
constexpr uint16_t kPlayed = 0xFFEC; // another game moves it: steered here
// This game moves it: one of these, the same one for bodies that shared a group (a rope's links, a chain's, a
// ragdoll's bones never meet each other: 78553, "same nonzero group").
constexpr uint16_t kMovingFirst = 0xF000;
constexpr uint16_t kMovingLast = 0xFFEB;

constexpr bool IsMoving(uint32_t aGroup) noexcept
{
    return aGroup >= kMovingFirst && aGroup <= kMovingLast;
}

// The moving group for a body by the group it had: bodies of one group share one, a body with none its own. A group
// is free again once its last body stops moving (Give): the range never wraps onto one in use.
class MovingGroups
{
public:
    [[nodiscard]] uint16_t Take(uint16_t aOwn) noexcept
    {
        if (aOwn)
            if (const auto it = m_groups.find(aOwn); it != m_groups.end())
            {
                ++m_users[it->second];
                return it->second;
            }
        for (uint32_t i = 0; i <= kMovingLast - kMovingFirst; ++i)
        {
            const uint16_t cGroup = m_next;
            m_next = m_next == kMovingLast ? kMovingFirst : static_cast<uint16_t>(m_next + 1);
            if (m_users.contains(cGroup))
                continue;
            m_users[cGroup] = 1;
            if (aOwn)
            {
                m_groups[aOwn] = cGroup;
                m_owners[cGroup] = aOwn;
            }
            return cGroup;
        }
        return aOwn; // every one in use: its own group, the engine's rules
    }
    void Give(uint16_t aMoving) noexcept
    {
        const auto it = m_users.find(aMoving);
        if (it == m_users.end() || --it->second > 0)
            return;
        m_users.erase(it);
        if (const auto cOwner = m_owners.find(aMoving); cOwner != m_owners.end())
        {
            m_groups.erase(cOwner->second);
            m_owners.erase(cOwner);
        }
    }
    [[nodiscard]] size_t InUse() const noexcept
    {
        return m_users.size();
    }
    void Clear() noexcept
    {
        m_groups.clear();
        m_owners.clear();
        m_users.clear();
        m_next = kMovingFirst;
    }

private:
    std::map<uint16_t, uint16_t> m_groups; // own -> moving
    std::map<uint16_t, uint16_t> m_owners; // moving -> own
    std::map<uint16_t, uint32_t> m_users;  // moving -> bodies in it
    uint16_t m_next = kMovingFirst;
};

enum class Meet
{
    kEngine,  // as the engine's filter says
    kCollide,
    kPass,
};

// Loose objects' layers (CommonLibSSE-NG COL_LAYER): clutter, weapon, props, small and large debris, large clutter.
constexpr bool IsLoose(uint32_t aLayer) noexcept
{
    return aLayer == 4 || aLayer == 5 || aLayer == 10 || aLayer == 19 || aLayer == 20 || aLayer == 29;
}

// Whether a body this game starts to move gets the moving group now: it changes what the body meets only against a
// played one (Decide), and writing it makes Havok redo the body's agents (77794), dropping its contacts for a step.
constexpr bool WritesMoving(bool aAnyPlayed) noexcept
{
    return aAnyPlayed;
}

// Two filter infos (layer in the low 7 bits, group in the high 16). A played body meets the world, characters and
// what this game moves; not other played bodies (their owner's game settled those), and not a loose object nobody
// moves: its owner's game never hit that one, or it would be moving too.
constexpr Meet Decide(uint32_t aFilterA, uint32_t aFilterB) noexcept
{
    const uint32_t cGroupA = aFilterA >> 16, cGroupB = aFilterB >> 16;
    const bool cPlayedA = cGroupA == kPlayed, cPlayedB = cGroupB == kPlayed;
    const bool cMovingA = IsMoving(cGroupA), cMovingB = IsMoving(cGroupB);
    if (cMovingA && cMovingB)
        return cGroupA == cGroupB ? Meet::kEngine : Meet::kCollide; // one group: as the engine kept them, apart
    if (!cPlayedA && !cPlayedB)
        return Meet::kEngine;
    if (cPlayedA && cPlayedB)
        return Meet::kPass;
    if (cMovingA || cMovingB)
        return Meet::kCollide;
    return IsLoose((cPlayedA ? aFilterB : aFilterA) & 0x7F) ? Meet::kPass : Meet::kEngine;
}
} // namespace BodyGroups

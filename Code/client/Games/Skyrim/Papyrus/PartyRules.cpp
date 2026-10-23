#include <Papyrus/PartyRules.h>

namespace PartyRules
{
Share Whose(uint32_t aCause, bool aStory) noexcept
{
    if (IsMirrored(aCause))
        return Share::kHere;
    if (aCause && !aStory)
        return Share::kPlayer; // a lever, fishing, a ride
    return Share::kParty;
}

Share ShareOf(Kind aKind, uint32_t aCause, bool aStory, uint32_t aAsked) noexcept
{
    const Share cShare = Whose(aCause, aStory);
    if (cShare == Share::kParty && aKind == Kind::kControls && !(aAsked & kMovement))
        return Share::kHere; // fighting, sneaking or menus only: a restriction
    return cShare;
}

void ControlHolds::Take(uint32_t aHolder, uint32_t aFlags) noexcept
{
    m_holds[aHolder] |= aFlags;
}

uint32_t ControlHolds::GiveBack(uint32_t aHolder, uint32_t aFlags) noexcept
{
    if (const auto it = m_holds.find(aHolder); it != m_holds.end())
        it->second &= ~aFlags;
    uint32_t flags = aFlags;
    for (const auto& [cHolder, cHeld] : m_holds)
        if (cHolder != aHolder)
            flags &= ~cHeld;
    return flags;
}

std::vector<ControlHolds::Returned> ControlHolds::Leave(std::optional<uint32_t> aHolder) noexcept
{
    std::vector<Returned> returned;
    for (auto it = m_holds.begin(); it != m_holds.end();)
    {
        const bool cLeaves = it->first != kThisGame && (!aHolder || it->first == *aHolder);
        if (cLeaves && it->second)
            returned.push_back({it->first, it->second, 0, false});
        it = cLeaves ? m_holds.erase(it) : std::next(it);
    }
    const uint32_t cStillHeld = Held();
    for (auto& r : returned)
    {
        r.Free = r.Held & ~cStillHeld;
        r.NoneElse = !cStillHeld;
    }
    return returned;
}

uint32_t ControlHolds::HeldBy(uint32_t aHolder) const noexcept
{
    const auto it = m_holds.find(aHolder);
    return it == m_holds.end() ? 0 : it->second;
}

uint32_t ControlHolds::Held() const noexcept
{
    uint32_t held = 0;
    for (const auto& [cHolder, cHeld] : m_holds)
        held |= cHeld;
    return held;
}

EnableControls EnableFor(uint32_t aFree, bool aNoneElse) noexcept
{
    return {(aFree & kMovement) != 0, (aFree & kFighting) != 0, aNoneElse, (aFree & kLooking) != 0,
            (aFree & kSneaking) != 0, (aFree & kMainFour) != 0, aNoneElse, aNoneElse};
}
} // namespace PartyRules

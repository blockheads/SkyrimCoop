#include <physics/BodyAuthority.h>

#include <physics/BodyTolerances.h>

#include <algorithm>

namespace
{
// Another player, as a guest hears one: the host names only "you" and "another".
constexpr uint32_t kAnother = UINT32_MAX;
} // namespace

bool BodyAuthority::Newer(const Claim& acA, const Claim& acB) noexcept
{
    if (acA.HoldSeq != acB.HoldSeq)
        return SeqAfter(acA.HoldSeq, acB.HoldSeq);
    return SeqAfter(acA.AuthSeq, acB.AuthSeq);
}

BodyAuthority::Claim BodyAuthority::Of(const BodyKey& acKey) const noexcept
{
    const auto it = m_bodies.find(acKey);
    return it != m_bodies.end() ? it->second.Is : Claim{};
}

std::vector<BodyKey> BodyAuthority::Bodies(uint32_t aPlayer) const noexcept
{
    std::vector<BodyKey> bodies;
    for (const auto& [cKey, cEntry] : m_bodies)
        if (cEntry.Is.Player == aPlayer)
            bodies.push_back(cKey);
    return bodies;
}

bool BodyAuthority::Touch(const BodyKey& acKey, uint32_t aPlayer, uint64_t aTick) noexcept
{
    Entry& entry = m_bodies[acKey];
    if (entry.Is.Held && entry.Is.Player != aPlayer)
        return false;
    entry.Touched = aTick;
    // Already moving under it: the same claim.
    if (entry.Is.Player == aPlayer && !entry.Is.AtRest)
        return true;
    entry.Is.Player = aPlayer;
    entry.Is.AtRest = false;
    ++entry.Is.AuthSeq;
    return true;
}

bool BodyAuthority::Grab(const BodyKey& acKey, uint32_t aPlayer, uint64_t aTick) noexcept
{
    Entry& entry = m_bodies[acKey];
    if (entry.Is.Held && entry.Is.Player != aPlayer)
        return false;
    entry.Touched = aTick;
    if (entry.Is.Held)
        return true;
    entry.Is.Player = aPlayer;
    entry.Is.Held = true;
    entry.Is.AtRest = false;
    ++entry.Is.HoldSeq;
    return true;
}

void BodyAuthority::Release(const BodyKey& acKey, uint32_t aPlayer) noexcept
{
    if (const auto it = m_bodies.find(acKey); it != m_bodies.end() && it->second.Is.Player == aPlayer)
        it->second.Is.Held = false;
}

std::vector<BodyKey> BodyAuthority::Spread(uint32_t aPlayer, const std::vector<std::pair<BodyKey, BodyKey>>& acContacts, uint64_t,
                                         const std::function<bool(const BodyKey&)>& acMoved) noexcept
{
    std::vector<BodyKey> taken;
    const auto cMoving = [&](const BodyKey& acKey) {
        const auto it = m_bodies.find(acKey);
        return it != m_bodies.end() && it->second.Is.Player == aPlayer && !it->second.Is.AtRest;
    };
    // Until none is added: a knocked body knocks the next.
    for (bool added = true; added;)
    {
        added = false;
        for (const auto& [cA, cB] : acContacts)
            for (const auto& [cMine, cOther] : {std::pair{cA, cB}, std::pair{cB, cA}})
            {
                if (!cMoving(cMine) || Of(cOther).Player != kNobody || (acMoved && !acMoved(cOther)))
                    continue;
                Entry& entry = m_bodies[cOther];
                entry.Is.Player = aPlayer;
                entry.Is.AtRest = false;
                ++entry.Is.AuthSeq;
                entry.StillSince.reset();
                taken.push_back(cOther);
                added = true;
            }
    }
    return taken;
}

std::vector<BodyKey> BodyAuthority::Rest(uint32_t aPlayer, const std::set<BodyKey>& acStill, const std::vector<std::pair<BodyKey, BodyKey>>& acContacts,
                                         uint64_t aTick) noexcept
{
    // aPlayer's moving bodies, not held, grouped by contact (a union of the pairs).
    std::map<BodyKey, BodyKey> parent;
    for (auto& [cKey, entry] : m_bodies)
        if (entry.Is.Player == aPlayer && !entry.Is.AtRest && !entry.Is.Held)
            parent[cKey] = cKey;
    const auto cRoot = [&](BodyKey aKey) {
        while (parent[aKey] != aKey)
            aKey = parent[aKey] = parent[parent[aKey]];
        return aKey;
    };
    for (const auto& [cA, cB] : acContacts)
        if (parent.contains(cA) && parent.contains(cB))
            parent[cRoot(cA)] = cRoot(cB);
    std::map<BodyKey, std::vector<BodyKey>> groups;
    for (const auto& [cKey, cParent] : parent)
        groups[cRoot(cKey)].push_back(cKey);

    std::vector<BodyKey> rested;
    for (const auto& [cRootKey, cGroup] : groups)
    {
        const bool cStill = std::ranges::all_of(cGroup, [&](const BodyKey& acKey) { return acStill.contains(acKey); });
        if (!cStill)
        {
            for (const BodyKey& cKey : cGroup)
                m_bodies[cKey].StillSince.reset();
            continue;
        }
        uint64_t since = aTick, touched = 0;
        for (const BodyKey& cKey : cGroup)
        {
            Entry& entry = m_bodies[cKey];
            if (!entry.StillSince)
                entry.StillSince = aTick;
            since = std::min(since, *entry.StillSince);
            touched = std::max(touched, entry.Touched);
        }
        // A touch keeps the pile awake: a capsule leaning on it; but not for good.
        if (aTick - touched < BodyTolerances::kLeanMs && aTick - since < BodyTolerances::kRestWaitMs)
            continue;
        for (const BodyKey& cKey : cGroup)
        {
            Entry& entry = m_bodies[cKey];
            entry.Is.AtRest = true;
            ++entry.Is.AuthSeq;
            entry.StillSince.reset();
            rested.push_back(cKey);
        }
    }
    return rested;
}

bool BodyAuthority::Accept(uint32_t aPlayer, const BodyState& acState) noexcept
{
    Entry& entry = m_bodies[acState.Frame.Key];
    const Claim cHeard{aPlayer, acState.AuthSeq, acState.HoldSeq, acState.Held, acState.AtRest};
    // Held is exclusive: only its holder speaks for it.
    if (entry.Is.Held && entry.Is.Player != aPlayer)
        return false;
    if (!Newer(cHeard, entry.Is) && !(Same(cHeard, entry.Is) && entry.Is.Player == aPlayer))
        return false;
    entry.Is = cHeard;
    // Its rest: nobody's again, which the stream tells it (Fiedler's confirmation).
    if (acState.AtRest)
        entry.Is.Player = kNobody;
    return true;
}

void BodyAuthority::Unclaim(const BodyKey& acKey) noexcept
{
    Entry& entry = m_bodies[acKey];
    entry.Is.Player = kNobody;
    entry.Is.Held = false;
    ++entry.Is.AuthSeq;
    entry.StillSince.reset();
}

void BodyAuthority::Moved(const BodyKey& acKey) noexcept
{
    m_bodies[acKey].Is.AtRest = false;
}

void BodyAuthority::Rested(const BodyKey& acKey) noexcept
{
    Entry& entry = m_bodies[acKey];
    if (entry.Is.Player != kNobody)
    {
        entry.Is.Player = kNobody;
        entry.Is.Held = false;
        ++entry.Is.AuthSeq;
    }
    entry.Is.AtRest = true;
    entry.StillSince.reset();
}

BodyAuthority::Heard BodyAuthority::Hear(const BodyState& acState, uint32_t aMe) noexcept
{
    const uint32_t cPlayer = acState.Author == BodyAuthor::kYou ? aMe : acState.Author == BodyAuthor::kOther ? kAnother : kNobody;
    const Claim cHost{cPlayer, acState.AuthSeq, acState.HoldSeq, acState.Held, acState.AtRest};
    Entry& entry = m_bodies[acState.Frame.Key];
    const Claim cMine = entry.Is;
    // This game's rest, given back by the host with the same numbers.
    if (cMine.Player == aMe && cMine.AtRest && cHost.AtRest && cPlayer == kNobody && Same(cHost, cMine))
    {
        entry.Is.Player = kNobody;
        return Heard::kConfirmed;
    }
    if (cMine.Player != aMe || Newer(cHost, cMine) || (Same(cHost, cMine) && cPlayer != aMe))
    {
        // The host's stands (this game yields a body it took when another was heard first). Its own state come
        // back as "you" is no news.
        entry.Is = cHost;
        if (cPlayer == aMe)
            return Heard::kOld;
        entry.StillSince.reset();
        return Heard::kApply;
    }
    return Heard::kOld;
}

BodyState BodyAuthority::Stamp(const BodyFrame& acFrame, uint32_t aReceiver) const noexcept
{
    const Claim cClaim = Of(acFrame.Key);
    BodyState state{.Frame = acFrame, .AuthSeq = cClaim.AuthSeq, .HoldSeq = cClaim.HoldSeq, .Held = cClaim.Held, .AtRest = cClaim.AtRest};
    state.Author = cClaim.Player == kNobody ? BodyAuthor::kNone : cClaim.Player == aReceiver ? BodyAuthor::kYou : BodyAuthor::kOther;
    if (state.AtRest)
        state.Frame.LinearVelocity = state.Frame.AngularVelocity = {};
    return state;
}

#include <Papyrus/PartyRules.h>

#include <Games/EngineNames.h>

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

Share ShareOf(Call aKind, uint32_t aCause, bool aStory, uint32_t aAsked) noexcept
{
    if (aKind == Call::kShow)
        return IsMirrored(aCause) ? Share::kHere : Share::kParty;
    const Share cShare = Whose(aCause, aStory);
    if (cShare == Share::kParty && aKind == Call::kControls && !(aAsked & kMovement))
        return Share::kHere; // fighting, sneaking or menus only: a restriction
    return cShare;
}

void MarkEachPlayer(LocalCall& aCall, uint32_t aCause) noexcept
{
    constexpr uint32_t cThisPlayer = 0x14;
    const uint32_t cPlayer = aCause && !IsMirrored(aCause) ? aCause : cThisPlayer;
    const auto cMark = [cPlayer](LocalValue& aValue) {
        if (auto* pForm = std::get_if<LocalForm>(&aValue); pForm && pForm->FormId == cPlayer)
            pForm->EachPlayer = true;
    };
    cMark(aCall.Self);
    for (LocalValue& argument : aCall.Arguments)
        cMark(argument);
}

Routing RouteOf(const CallFacts& acFacts) noexcept
{
    const Call cKind = acFacts.Reach.Kind;
    if (IsMirrored(acFacts.Cause))
        return {};
    if (acFacts.Guest && cKind == Call::kWorld && !acFacts.OwnQuest)
        return {.To = Destination::kHost, .Original = false};
    // Where the player is, with no player's action behind it: the party's (rule 3). Only the host's scripts.
    if (cKind == Call::kQuestion && acFacts.Hosting && acFacts.PlayerInCall && !acFacts.Cause)
        return {.Ask = true};
    // A player's progression: any script's but a body's, or only the story's (dragonborn.md). Only the host's scripts.
    const Progress& cProgress = acFacts.Reach.Progress;
    if (cKind == Call::kProgress && acFacts.Hosting && acFacts.Target != Target::kNone && !acFacts.Body &&
        (cProgress.AnyScript || Whose(acFacts.Cause, acFacts.Story) == Share::kParty))
    {
        if (acFacts.Target == Target::kThisPlayer)
            return {.To = Destination::kEveryGame, .Mark = true, .MarkFor = acFacts.TargetId};
        return {.To = Destination::kEveryGame, .Original = cProgress.OnCopy, .ThisPlayer = true, .Mark = true, .MarkFor = acFacts.TargetId};
    }
    // The controls, the screen and shows: the party's for the story (cutscenes.md); a show whoever's action ran it.
    if (cKind == Call::kControls || cKind == Call::kScreen || cKind == Call::kShow)
    {
        const Share cWhose = Whose(acFacts.Cause, acFacts.Story);
        if ((cKind == Call::kShow ? ShareOf(cKind, acFacts.Cause, acFacts.Story, 0) : cWhose) == Share::kParty)
            return {.To = Destination::kEveryGame, .Mark = cWhose == Share::kParty, .MarkFor = acFacts.Cause};
    }
    // On the player, or its screen (a message box, an image-space modifier), in a script a guest's action ran: that
    // guest's game runs it (rule 2; cutscenes.md, a player's moment).
    if (acFacts.GuestCause && (acFacts.PlayerNative || cKind == Call::kControls || cKind == Call::kScreen ||
                              cKind == Call::kPlayerMenu))
        return {.To = Destination::kCauseGame, .Original = false};
    return {};
}

std::optional<uint32_t> AskedPlayer(const std::vector<Asked>& acPlayers) noexcept
{
    const Asked* pNearest = nullptr;
    for (const Asked& cAsked : acPlayers)
        if (cAsked.Distance && (!pNearest || *cAsked.Distance < *pNearest->Distance))
            pNearest = &cAsked;
    if (pNearest)
        return pNearest->Player;
    for (const Asked& cAsked : acPlayers)
        if (cAsked.InPlace)
            return cAsked.Player;
    return std::nullopt;
}

std::vector<uint32_t> PlayersToAsk(bool aYesNo, std::optional<uint32_t> aPicked, const std::vector<uint32_t>& acGuests) noexcept
{
    if (!aYesNo)
        return {aPicked.value_or(kThisPlayerFormId)};
    std::vector<uint32_t> players{kThisPlayerFormId};
    players.insert(players.end(), acGuests.begin(), acGuests.end());
    return players;
}

LocalCall OnThisPlayer(LocalCall aCall) noexcept
{
    const auto cOwn = [](LocalValue& aValue) {
        if (auto* pForm = std::get_if<LocalForm>(&aValue); pForm && pForm->EachPlayer)
            *pForm = LocalForm{0x14};
    };
    cOwn(aCall.Self);
    for (LocalValue& argument : aCall.Arguments)
        cOwn(argument);
    return aCall;
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

void LatentCopies::Sent(uint32_t aSource, uint32_t aKey) noexcept
{
    m_queued[aSource].push_back({aKey, std::nullopt});
}

std::optional<LocalValue> LatentCopies::Started(uint32_t aSource, uint32_t aStack) noexcept
{
    auto it = m_queued.find(aSource);
    if (it == m_queued.end() || it->second.empty())
        return std::nullopt;
    Queued call = std::move(it->second.front());
    it->second.erase(it->second.begin());
    if (call.Result)
        return call.Result;
    m_waiting[{aSource, call.Key}] = aStack;
    return std::nullopt;
}

std::optional<uint32_t> LatentCopies::Ended(uint32_t aSource, uint32_t aKey, const LocalValue& acResult) noexcept
{
    if (const auto it = m_waiting.find({aSource, aKey}); it != m_waiting.end())
    {
        const uint32_t cStack = it->second;
        m_waiting.erase(it);
        return cStack;
    }
    if (auto it = m_queued.find(aSource); it != m_queued.end())
        for (Queued& call : it->second)
            if (call.Key == aKey)
                call.Result = acResult;
    return std::nullopt;
}

void LatentCopies::Returned(uint32_t aStack) noexcept
{
    std::erase_if(m_waiting, [aStack](const auto& acEntry) { return acEntry.second == aStack; });
}

void LatentCopies::Left(uint32_t aSource) noexcept
{
    m_queued.erase(aSource);
    std::erase_if(m_waiting, [aSource](const auto& acEntry) { return acEntry.first.first == aSource; });
}

EnableControls EnableFor(uint32_t aFree, bool aNoneElse) noexcept
{
    return {(aFree & kMovement) != 0, (aFree & kFighting) != 0, aNoneElse, (aFree & kLooking) != 0,
            (aFree & kSneaking) != 0, (aFree & kMainFour) != 0, aNoneElse, aNoneElse};
}
} // namespace PartyRules

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

Share ShareOf(Kind aKind, uint32_t aCause, bool aStory, uint32_t aAsked) noexcept
{
    if (aKind == Kind::kShow)
        return IsMirrored(aCause) ? Share::kHere : Share::kParty;
    const Share cShare = Whose(aCause, aStory);
    if (cShare == Share::kParty && aKind == Kind::kControls && !(aAsked & kMovement))
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

std::optional<Progress> ProgressOf(std::string_view aClass, std::string_view aFunction) noexcept
{
    struct Native
    {
        std::string_view Class;
        std::string_view Function;
        Progress Is;
    };
    // A persuade's AdvanceSkill is its picker's own (rule 2), a lever's faction its puller's: the story's only.
    static constexpr Native cNatives[] = {
        {"Game", "TeachWord", {.AnyScript = true}},
        {"Game", "UnlockWord", {.AnyScript = true}},
        {"Game", "AdvanceSkill", {}},
        {"Game", "IncrementSkill", {.AnyScript = true}},
        {"Game", "IncrementSkillBy", {.AnyScript = true}},
        {"Game", "AddPerkPoints", {.AnyScript = true}},
        {"Actor", "AddPerk", {.AnyScript = true}},
        {"Actor", "RemovePerk", {.AnyScript = true}},
        {"Actor", "ModActorValue", {.AnyScript = true}}, // the permanent modifier: souls, a stat
        {"Actor", "AddToFaction", {.OnCopy = true}},
        {"Actor", "RemoveFromFaction", {.OnCopy = true}},
        {"Actor", "SetFactionRank", {.OnCopy = true}},
        {"Actor", "AddSpell", {.OnCopy = true}},
        {"Actor", "RemoveSpell", {.OnCopy = true}},
    };
    for (const Native& cNative : cNatives)
        if (SameName(aClass, cNative.Class) && SameName(aFunction, cNative.Function))
            return cNative.Is;
    return std::nullopt;
}

Route RouteOf(const Progress& acProgress, Target aTarget, uint32_t aCause, bool aStory, bool aBody, bool aHosting) noexcept
{
    if (!aHosting || aTarget == Target::kNone || aBody || IsMirrored(aCause))
        return {};
    if (!acProgress.AnyScript && Whose(aCause, aStory) != Share::kParty)
        return {};
    if (aTarget == Target::kThisPlayer)
        return {.Relay = true};
    return {.Original = acProgress.OnCopy, .ThisPlayer = true, .Relay = true};
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

EnableControls EnableFor(uint32_t aFree, bool aNoneElse) noexcept
{
    return {(aFree & kMovement) != 0, (aFree & kFighting) != 0, aNoneElse, (aFree & kLooking) != 0,
            (aFree & kSneaking) != 0, (aFree & kMainFour) != 0, aNoneElse, aNoneElse};
}
} // namespace PartyRules

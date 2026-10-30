#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Games/EngineNames.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/SystemQuests.h>
#include <Events/SystemQuestLearnedEvent.h>
#include <ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;


namespace
{
bool Argument(const LocalCall& acCall, size_t aIndex)
{
    const auto* pValue = aIndex < acCall.Arguments.size() ? std::get_if<bool>(&acCall.Arguments[aIndex]) : nullptr;
    return pValue && *pValue;
}

// ControlMap's jumping (UserEvent kJumping): Skyrim takes it with movement.
constexpr uint32_t kJumping = 1 << 10;

// The ControlMap flags a controls native's arguments take, as Skyrim's (measured 2026-10-23, docs/design/testing.md);
// camera switch, activate and the journal take none.
uint32_t FlagsOf(const LocalCall& acCall)
{
    return (Argument(acCall, 0) ? kMovement | kJumping : 0) | (Argument(acCall, 1) ? kFighting : 0) |
           (Argument(acCall, 3) ? kLooking : 0) | (Argument(acCall, 4) ? kSneaking : 0) |
           (Argument(acCall, 5) ? kMainFour : 0);
}

LocalCall ControlsCall(const char* acpFunction, std::array<bool, 8> aFlags)
{
    LocalCall call{.Class = "Game", .Function = acpFunction};
    for (const bool cFlag : aFlags)
        call.Arguments.emplace_back(cFlag);
    call.Arguments.emplace_back(int32_t{0});
    return call;
}
} // namespace

LocalCall DisablePlayerControls(bool aMovement, bool aFighting, bool aCamSwitch, bool aLooking, bool aSneaking,
                                bool aMenu, bool aActivate)
{
    return ControlsCall("DisablePlayerControls",
                        {aMovement, aFighting, aCamSwitch, aLooking, aSneaking, aMenu, aActivate, false});
}

LocalCall EnablePlayerControls(bool aMovement, bool aFighting, bool aCamSwitch, bool aLooking, bool aSneaking,
                               bool aMenu, bool aActivate)
{
    return ControlsCall("EnablePlayerControls",
                        {aMovement, aFighting, aCamSwitch, aLooking, aSneaking, aMenu, aActivate, true});
}

// What a native's code reaches, as NativeReach reads it from the engine's code (the fake has none: by name). Matched
// in any case; an empty function is any of its class's.
Reach FakeReachOf(const LocalCall& acCall)
{
    struct Row
    {
        std::string_view Class;
        std::string_view Function;
        Reach Is;
    };
    static constexpr Reach cReward{Call::kProgress, {.AnyScript = true}};
    static constexpr Reach cStory{Call::kProgress, {}};
    static constexpr Reach cStoryOnCopy{Call::kProgress, {.OnCopy = true}};
    static constexpr Row cRows[] = {
        {"Game", "TeachWord", cReward},          {"Game", "UnlockWord", cReward},     {"Game", "IncrementSkill", cReward},
        {"Game", "IncrementSkillBy", cReward},   {"Game", "AddPerkPoints", cReward},  {"Actor", "AddPerk", cReward},
        {"Actor", "RemovePerk", cReward},        {"Actor", "ModActorValue", cReward}, {"Actor", "ForceActorValue", cReward},
        {"Game", "AdvanceSkill", cStory},        {"Actor", "AddSpell", cStoryOnCopy}, {"Actor", "RemoveSpell", cStoryOnCopy},
        {"Actor", "AddToFaction", cStoryOnCopy}, {"Actor", "SetFactionRank", cStoryOnCopy},
        {"Actor", "RemoveFromFaction", cStoryOnCopy}, {"Actor", "SetRace", cStory},
        {"VisualEffect", "", {Call::kShow}},     {"EffectShader", "", {Call::kShow}}, {"Actor", "PlaySubGraphAnimation", {Call::kShow}},
        {"Game", "ShakeCamera", {Call::kScreen}},
        {"Debug", "Notification", {Call::kScreen}},  {"Debug", "MessageBox", {Call::kScreen}}, {"Message", "Show", {Call::kScreen}},
        {"Actor", "EquipItem", {Call::kItem}},     {"Actor", "UnequipItem", {Call::kItem}},
        {"Actor", "ShowBarterMenu", {Call::kPlayerMenu}},
        {"ObjectReference", "GetDistance", {Call::kQuestion}}, {"Actor", "HasLOS", {Call::kQuestion}},
        {"Game", "DisablePlayerControls", {Call::kControls}}, {"Game", "EnablePlayerControls", {Call::kControls}},
        {"GlobalVariable", "SetValue", {Call::kWorld}},
        {"ObjectReference", "MoveTo", {Call::kBody}}, {"Actor", "PlayIdle", {Call::kBody}},
        {"Game", "SetPlayerAIDriven", {Call::kBody}},
    };
    for (const Row& cRow : cRows)
        if (SameName(acCall.Class, cRow.Class) && (cRow.Function.empty() || SameName(acCall.Function, cRow.Function)))
            return cRow.Is;
    return {};
}

// PartyNatives::Scope around the native (PartyRules::RouteOf), the native's own effect here, and the party call after.
void FakeEngine::RunScript(const LocalCall& acCall, uint32_t aCause, bool aStory, bool aBody)
{
    const Reach cReach = FakeReachOf(acCall);
    const auto* cpSelf = std::get_if<LocalForm>(&acCall.Self);
    const uint32_t cSelf = cpSelf ? cpSelf->FormId : kPlayer; // a global's player is this game's
    const auto cIsGuestCharacter = [this](uint32_t aActor) {
        const auto cIt = Actors.find(aActor);
        return !IsGuest && cIt != Actors.end() && cIt->second.Copy && cIt->second.Player;
    };
    const auto cIsPlayer = [&](const LocalValue& acValue) {
        const auto* cpForm = std::get_if<LocalForm>(&acValue);
        return cpForm && cpForm->FormId == kPlayer;
    };
    const CallFacts cFacts{.Reach = cReach,
                           .Target = cSelf == kPlayer ? Target::kThisPlayer : cIsGuestCharacter(cSelf) ? Target::kGuestCharacter : Target::kNone,
                           .TargetId = cSelf,
                           .Cause = aCause,
                           .Story = aStory,
                           .Body = aBody,
                           .GuestCause = cIsGuestCharacter(aCause),
                           .Hosting = !IsGuest,
                           .Guest = IsGuest,
                           .PlayerInCall = cIsPlayer(acCall.Self) || std::ranges::any_of(acCall.Arguments, cIsPlayer),
                           .InSystemQuest = m_inSystemQuest,
                           .SystemObject = cpSelf && (SystemQuestList.Contains(cSelf) || SystemQuestList.IsGlobal(cSelf))};
    const Routing cRouting = InSession ? RouteOf(cFacts) : Routing{};
    if (cReach.Kind == Call::kQuestion)
    {
        LastAnswer = Answer(acCall, cRouting.Ask);
        return;
    }
    uint32_t asked = 0;
    m_openedBox = 0;
    if (cRouting.Original)
        asked = Perform(acCall, cReach.Kind, cSelf, aCause);
    // A box waits for its answer (latent): another game's copy for its original's (Scope::Ran).
    if (m_openedBox && IsMirrored(aCause))
        if (const auto cEnded = Copies.Started(PlayerOf(aCause), m_openedBox))
            AnswerBox(m_openedBox, std::get<int32_t>(*cEnded));
    if (InSession && (cReach.Kind == Call::kProgress || m_inSystemQuest))
        World::Get().GetRunner().Trigger(PlayerProgressEvent{});
    if (cRouting.To == Destination::kCauseGame)
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOwner, .GuestFormId = aCause, .Call = acCall});
    if (cRouting.To == Destination::kHost)
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kHost, .Call = acCall});
    if (cRouting.To != Destination::kEveryGame)
        return;
    if (cReach.Kind == Call::kControls && ShareOf(Call::kControls, aCause, aStory, asked) != Share::kParty)
        return; // fighting, sneaking or menus only: a restriction
    LocalCall call = acCall;
    if (cRouting.Mark)
        MarkEachPlayer(call, cRouting.MarkFor);
    if (cRouting.ThisPlayer)
        PartyNatives::Run(OnThisPlayer(call), 0);
    if (m_openedBox)
        LatentOriginals.insert(m_openedBox);
    World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOthers, .ReplyId = m_openedBox, .Call = std::move(call)});
}

// ObjectReference.GetDistance between its self and its argument, Actor.HasLOS (in sight within 1000 units). A question
// asked of the party is put to the players PartyRules::PlayersToAsk names: a distance to the nearest (Scope's asker),
// a yes/no one to each until one says yes.
LocalValue FakeEngine::Answer(const LocalCall& acCall, bool aAsksParty) const
{
    const auto cIdOf = [](const LocalValue& acValue) {
        const auto* cpForm = std::get_if<LocalForm>(&acValue);
        return cpForm ? cpForm->FormId : 0u;
    };
    uint32_t self = cIdOf(acCall.Self);
    uint32_t other = acCall.Arguments.empty() ? 0 : cIdOf(acCall.Arguments[0]);
    const auto cDistance = [this](uint32_t aFrom, uint32_t aTo) -> std::optional<float> {
        const auto cFrom = Actors.find(aFrom);
        const auto cTo = Actors.find(aTo);
        if (cFrom == Actors.end() || cTo == Actors.end() || cFrom->second.Cell != cTo->second.Cell)
            return std::nullopt;
        return glm::distance(glm::vec3(cFrom->second.Position[0], cFrom->second.Position[1], cFrom->second.Position[2]),
                             glm::vec3(cTo->second.Position[0], cTo->second.Position[1], cTo->second.Position[2]));
    };
    const bool cYesNo = acCall.Function == "HasLOS";
    const auto cAnswerFor = [&](uint32_t aPlayer) -> LocalValue {
        const uint32_t cSelf = self == kPlayer ? aPlayer : self;
        const uint32_t cOther = other == kPlayer ? aPlayer : other;
        const float cDistanceNow = cDistance(cSelf, cOther).value_or(1e9f);
        return cYesNo ? LocalValue{cDistanceNow < 1000.f} : LocalValue{cDistanceNow};
    };
    if (!aAsksParty)
        return cAnswerFor(kPlayer);
    const uint32_t cAsker = self == kPlayer ? other : self;
    std::vector<Asked> players{{kPlayer, cDistance(kPlayer, cAsker)}};
    std::vector<uint32_t> guests;
    for (const uint32_t cCopy : CopiesOf())
        if (Actors.at(cCopy).Player)
        {
            players.push_back({cCopy, cDistance(cCopy, cAsker)});
            guests.push_back(cCopy);
        }
    LocalValue answer;
    for (const uint32_t cAsked : PlayersToAsk(cYesNo, cYesNo ? std::nullopt : AskedPlayer(players), guests))
        if (answer = cAnswerFor(cAsked); !cYesNo || std::get<bool>(answer))
            break;
    return answer;
}

void FakeEngine::AnswerBox(uint32_t aStack, int32_t aButton)
{
    if (!Boxes.erase(aStack))
        return;
    Answers[aStack] = aButton;
    Copies.Returned(aStack);
    if (LatentOriginals.erase(aStack))
        World::Get().GetRunner().Trigger(CallEndedEvent{aStack, aButton});
}

// The native's own effect on this game: a progression write, what an actor shows, the shake, or the controls (one
// ControlMap::ToggleControls, held for the stack's cutscene). The flags a controls call asked for.
uint32_t FakeEngine::Perform(const LocalCall& acCall, Call aKind, uint32_t aSelf, uint32_t aCause)
{
    switch (aKind)
    {
    case Call::kPlayerMenu: BarterMerchant = aSelf; return 0;
    case Call::kProgress: TakeProgress(acCall, aSelf); return 0;
    case Call::kWorld:
        if (acCall.Function == "SetValue" && !acCall.Arguments.empty())
            SetGlobal(aSelf, std::get<float>(acCall.Arguments[0]));
        return 0;
    case Call::kShow:
    {
        // An effect form's call on the actor it names, or the actor's own sub-graph animation.
        const LocalValue* cpOn = acCall.Class == "Actor" || acCall.Arguments.empty() ? nullptr : &acCall.Arguments[0];
        if (const auto* cpForm = cpOn ? std::get_if<LocalForm>(cpOn) : std::get_if<LocalForm>(&acCall.Self))
            Shows[cpForm->FormId].push_back(acCall.Class + "." + acCall.Function);
        return 0;
    }
    case Call::kScreen:
    {
        if (acCall.Function != "ShakeCamera")
        {
            // A message: the HUD's notification, a message box (its text, or the message form it shows).
            const auto* cpText = acCall.Arguments.empty() ? nullptr : std::get_if<std::string>(&acCall.Arguments[0]);
            const auto* cpForm = std::get_if<LocalForm>(&acCall.Self);
            Messages.push_back(cpText ? *cpText : std::format("{:X}", cpForm ? cpForm->FormId : 0));
            if (acCall.Class == "Message" && acCall.Function == "Show")
                Boxes[m_openedBox = m_nextStack++] = Messages.back(); // latent: its stack waits for the answer
            return 0;
        }
        // Game.ShakeCamera(source, strength, duration).
        const auto* pSeconds = acCall.Arguments.size() > 2 ? std::get_if<float>(&acCall.Arguments[2]) : nullptr;
        ShakeSeconds = pSeconds ? *pSeconds : 0.f;
        return 0;
    }
    case Call::kControls:
    {
        const bool cEnable = acCall.Function == "EnablePlayerControls";
        uint32_t flags = FlagsOf(acCall);
        const uint32_t cAsked = flags;
        if (InSession)
        {
            const uint32_t cHolder = HolderOf(aCause);
            if (cEnable)
                flags = Holds.GiveBack(cHolder, flags);
            else
                Holds.Take(cHolder, flags);
        }
        Controls = cEnable ? Controls | flags : Controls & ~flags;
        World::Get().GetRunner().Trigger(SpectatorRefreshEvent{});
        return cAsked;
    }
    case Call::kBody:
    {
        const auto* cpForm = acCall.Arguments.empty() ? nullptr : std::get_if<LocalForm>(&acCall.Arguments[0]);
        if (acCall.Function == "MoveTo" && cpForm)
        {
            // To the reference: a marker, or an actor (MoveTo_Impl; the player's queues a travel).
            const auto cMarker = References.find(cpForm->FormId);
            const auto cActor = Actors.find(cpForm->FormId);
            const uint32_t cCell = cMarker != References.end() ? cMarker->second.Cell : cActor != Actors.end() ? cActor->second.Cell : 0;
            const glm::vec3 cAt = cMarker != References.end() ? cMarker->second.Position
                                  : cActor != Actors.end()   ? glm::vec3(cActor->second.Position[0], cActor->second.Position[1], cActor->second.Position[2])
                                                             : glm::vec3{};
            m_mirroredMove = aSelf == kPlayer && IsMirrored(aCause); // GuestScripts' mark (HookMoveRun)
            MoveActorTo(aSelf, cCell, cAt.x, cAt.y, cAt.z);
            m_mirroredMove = false;
        }
        else if (acCall.Function == "PlayIdle" && cpForm)
            Idles[aSelf].push_back(cpForm->FormId);
        else if (acCall.Function == "SetPlayerAIDriven" && !acCall.Arguments.empty())
            AiDriven = std::get<bool>(acCall.Arguments[0]);
        return 0;
    }
    default: return 0;
    }
}


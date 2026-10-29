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

bool FakeEngine::IsLyingInRagdoll(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    if (it->second.Copy)
        return RagdollPosed(aFormId);
    return it->second.KnockedDown || std::ranges::any_of(Bodies, [&](const auto& acBody) {
        return acBody.second.RagdollOf == aFormId && (acBody.second.Awake || it->second.Dead);
    });
}

bool FakeEngine::RagdollPosed(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !it->second.Copy)
        return false;
    return it->second.KnockedDown ||
           (it->second.Dead && std::ranges::any_of(Bodies, [&](const auto& acBody) { return acBody.second.RagdollOf == aFormId && acBody.second.Held; }));
}

void FakeEngine::Frame()
{
    for (auto& [cId, actor] : Actors)
    {
        if (!actor.Copy)
            continue;
        if (actor.KnockIn && !--actor.KnockIn)
            actor.KnockedDown = true;
        for (const auto& cDrive : m_pendingRagdolls.Take(cId))
        {
            if (!actor.Dead && !actor.KnockedDown)
                continue;
            const auto it = Bodies.find(cDrive.Frame.Key);
            if (it == Bodies.end())
                continue;
            auto& body = it->second;
            body.Held = true;
            body.Awake = false;
            body.RagdollOf = cId;
            body.Flight.Body.Position = cDrive.Frame.Position;
            body.Flight.Body.Rotation = cDrive.Frame.Rotation;
            ++body.Drives;
        }
        // Drawn: moved since its graph update while its ragdoll poses it, its old pose shows at the new place.
        const float cTurn = std::abs(std::remainder(actor.Rotation[2] - actor.GraphPlace[3], 6.28318531f));
        const float cSnap = glm::distance(glm::vec3(actor.Position[0], actor.Position[1], actor.Position[2]),
                                          glm::vec3(actor.GraphPlace[0], actor.GraphPlace[1], actor.GraphPlace[2])) + cTurn * kSnapReach;
        if (cSnap > 0.5f && RagdollPosed(cId))
        {
            ++actor.Snaps;
            actor.WorstSnap = std::max(actor.WorstSnap, cSnap);
        }
        // Its graph update: lying in its ragdoll, the engine keeps it on its root body, turned with it (37996).
        if (RagdollPosed(cId))
            for (const auto& [cKey, cBody] : Bodies)
                if (cBody.RagdollOf == cId && cKey.Index == 0)
                {
                    std::copy_n(&cBody.Flight.Body.Position.x, 3, actor.Position);
                    actor.Rotation[2] = HeadingOf(cBody.Flight.Body.Rotation);
                }
        std::copy_n(actor.Position, 3, actor.GraphPlace);
        actor.GraphPlace[3] = actor.Rotation[2];
    }
    ShakeSeconds = std::max(0.f, ShakeSeconds - 1.f / 60);
    // Game time at the time scale for the real time since the last frame, as the engine's frame delta is.
    const auto cNow = std::chrono::steady_clock::now();
    if (m_lastFrame.time_since_epoch().count())
        Calendar.Time += Calendar.TimeScale * std::chrono::duration<float>(cNow - m_lastFrame).count() / 3600.f;
    m_lastFrame = cNow;
    if (Calendar.Time >= 24.f)
    {
        Calendar.Time -= 24.f;
        ++Calendar.Day;
        ++Calendar.DaysPassed;
    }
    if (auto& player = Actors.at(kPlayer); player.Dead)
    {
        if (player.DyingFrames && !--player.DyingFrames)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer});
        if (++player.DeadFrames >= kDeathReloadFrames && !DeathReloadHeld)
            Reloaded = true;
    }
    RunQueued();
}

void FakeEngine::Queue(LocalCall aCall, uint32_t aCause)
{
    m_queued.emplace_back(std::move(aCall), aCause);
}

void FakeEngine::RunQueued()
{
    auto queued = std::exchange(m_queued, {});
    for (const auto& [cCall, cCause] : queued)
    {
        const auto* cpSelf = std::get_if<LocalForm>(&cCall.Self);
        if (cpSelf && QuestFunctions.contains({cpSelf->FormId, cCall.Function}))
            CallsQuest(cCall, cCause, false, false);
        else
            RunScript(cCall, cCause);
    }
}

void FakeEngine::CallsQuest(const LocalCall& acCall, uint32_t aCause, bool aStory, bool aFromBody)
{
    const auto* cpQuest = std::get_if<LocalForm>(&acCall.Self);
    const uint32_t cQuest = cpQuest ? cpQuest->FormId : 0;
    const auto it = QuestFunctions.find({cQuest, acCall.Function});
    if (it == QuestFunctions.end())
        return;
    const bool cOwnFunction = IsQuestScriptFunction(acCall.Class); // not Quest.SetStage's wrapper
    if (cOwnFunction && LearnsSystemQuest(cQuest, 0, aFromBody, m_inSystemQuest, false))
        SystemQuests::Learn(cQuest, true);
    if (!cOwnFunction || !SystemQuestList.Contains(cQuest))
    {
        it->second(*this, acCall, aCause);
        return;
    }
    const auto cIsGuestCharacter = [this](uint32_t aActor) {
        const auto cIt = Actors.find(aActor);
        return !IsGuest && cIt != Actors.end() && cIt->second.Copy && cIt->second.Player;
    };
    const Routing cRouting = InSession ? RouteOfSystemCall(aCause, aStory, cIsGuestCharacter(aCause), !IsGuest, false) : Routing{};
    if (cRouting.To == Destination::kCauseGame)
    {
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOwner, .GuestFormId = aCause, .Call = acCall});
        return;
    }
    if (cRouting.To == Destination::kEveryGame)
    {
        LocalCall call = acCall;
        MarkEachPlayer(call, cRouting.MarkFor);
        if (cRouting.ThisPlayer)
            PartyNatives::Run(OnThisPlayer(call), 0);
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOthers, .Call = std::move(call)});
        if (!cRouting.Original)
            return;
    }
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerProgressEvent{});
    const bool cOuter = std::exchange(m_inSystemQuest, true);
    it->second(*this, acCall, aCause);
    m_inSystemQuest = cOuter;
}

void FakeEngine::FiresQuestTimer(uint32_t aQuest, const std::string& acFunction)
{
    // Its own event: this game's player's (WorldEvents' gate delivers it as this player's).
    CallsQuest({.Class = "Quest", .Function = acFunction, .Self = LocalForm{aQuest}}, kPlayer);
}

std::string FakeEngine::ReadSystemQuests() const
{
    SystemQuestState state;
    for (const uint32_t cQuest : SystemQuestList.All())
    {
        SystemQuestState::Quest& quest = state.Quests.emplace_back(SystemQuestState::Quest{GameId(0, cQuest)});
        if (const auto cIt = QuestVariables.find(cQuest); cIt != QuestVariables.end())
            for (const auto& [cName, cValue] : cIt->second)
                quest.Variables.push_back({"Script", cName, Utils::ToScriptValue(cValue)});
        if (const auto cIt = QuestTimers.find(cQuest); cIt != QuestTimers.end())
            for (const auto& [cEvent, cTimer] : cIt->second)
                quest.Timers.push_back(cTimer);
    }
    for (const uint32_t cGlobal : SystemQuestList.Globals())
        state.Globals.push_back({GameId(0, cGlobal), Globals.contains(cGlobal) ? Globals.at(cGlobal) : 0.f});
    return state.Encode();
}

void FakeEngine::ApplySystemQuests(std::string_view aState)
{
    const auto cState = SystemQuestState::Decode(aState);
    for (const uint32_t cQuest : SystemQuestList.All())
    {
        // As SystemQuests::ApplyState: the save's instance (the host's player's) goes; this player's comes back, or
        // its scripts start over.
        QuestVariables.erase(cQuest);
        QuestTimers.erase(cQuest);
        const SystemQuestState::Quest* cpKept = cState ? cState->Find(GameId(0, cQuest)) : nullptr;
        if (!cpKept)
            continue;
        for (const SystemQuestState::Variable& cVariable : cpKept->Variables)
            if (const auto cValue = Utils::ToLocalValue(cVariable.Value))
                QuestVariables[cQuest][cVariable.Name] = *cValue;
        for (const SystemQuestState::Timer& cTimer : cpKept->Timers)
            QuestTimers[cQuest][cTimer.Native] = cTimer;
    }
    if (cState)
        for (const SystemQuestState::Global& cGlobal : cState->Globals)
            if (SystemQuestList.IsGlobal(cGlobal.Id.BaseId))
                Globals[cGlobal.Id.BaseId] = cGlobal.Value;
}

FakeEngine& FakeEngine::Current() noexcept
{
    return World::Get().ctx().at<FakeEngine&>();
}

GameId SessionIdOf(uint32_t aFormId) noexcept
{
    return GameId(aFormId >> 24, aFormId & 0x00FFFFFF);
}

uint32_t FormIdOf(const GameId& acId) noexcept
{
    return (acId.ModId << 24) | acId.BaseId;
}

namespace
{
// A container's count of an item; none held is no entry, as the engine's lists keep it.
void SetCount(FakeEngine::Reference& aReference, uint32_t aItem, int32_t aCount)
{
    if (aCount > 0)
        aReference.Items[aItem] = aCount;
    else
        aReference.Items.erase(aItem);
}

// And its stacks with extra data, as given (ContainerItems' SetContainerStacks).
void SetCount(FakeEngine::Reference& aReference, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks)
{
    SetCount(aReference, aItem, aCount);
    std::erase_if(aReference.Stacks, [&](const Inventory::Entry& acStack) { return acStack.BaseId == SessionIdOf(aItem); });
    for (auto& stack : aStacks)
        aReference.Stacks.push_back(std::move(stack));
}

std::vector<Inventory::Entry> StacksOf(const FakeEngine::Reference& acReference, uint32_t aItem)
{
    std::vector<Inventory::Entry> stacks;
    for (const auto& cStack : acReference.Stacks)
        if (cStack.BaseId == SessionIdOf(aItem))
            stacks.push_back(cStack);
    return stacks;
}

// ReferenceStates' lock row: the level in the low byte, kLocked above it.
constexpr uint32_t kLocked = 0x100;

ObjectChange LockRow(uint32_t aReference, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kLock, .Object = SessionIdOf(aReference)};
    row.Value = (acReference.Locked ? kLocked : 0) | acReference.LockLevel;
    return row;
}

// ReferenceStates' open row: 1 open or opening.
ObjectChange OpenRow(uint32_t aDoor, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kOpenState, .Object = SessionIdOf(aDoor)};
    row.Value = acReference.Open ? 1 : 0;
    return row;
}

ObjectChange ItemsRow(uint32_t aContainer, const FakeEngine::Reference& acReference, uint32_t aItem)
{
    ObjectChange row{.Type = ObjectChange::Kind::kItems, .Object = SessionIdOf(aContainer), .Item = SessionIdOf(aItem)};
    const auto it = acReference.Items.find(aItem);
    row.Value = it != acReference.Items.end() ? static_cast<uint32_t>(it->second) : 0;
    row.Stacks = StacksOf(acReference, aItem);
    return row;
}
} // namespace

void FakeEngine::LoadCell(uint32_t aCell)
{
    Attached.insert(aCell);
    InMemory.insert(aCell);
}

// An actor Actor::Create made goes with it, unless persistent (copy-contract.md, "Players' copies"); a placeatme
// reference stays.
void FakeEngine::UnloadCell(uint32_t aCell)
{
    Attached.erase(aCell);
    std::vector<uint32_t> left;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Created && cActor.Cell == aCell && !cActor.Persistent)
            left.push_back(cId);
    for (const uint32_t cId : left)
        DeleteActor(cId);
}

void FakeEngine::PlayerSetsLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    if (IsGuest)
    {
        References.at(aReference).Locked = aLocked;
        References.at(aReference).LockLevel = aLevel;
        Changed.insert(aReference);
        World::Get().GetDispatcher().trigger(LockChangeEvent(aReference, aLocked, aLevel, true));
        return;
    }
    SetLock(aReference, aLocked, aLevel);
}

void FakeEngine::PlayerSetsItems(uint32_t aContainer, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks)
{
    if (IsGuest)
    {
        SetCount(References.at(aContainer), aItem, aCount, std::move(aStacks));
        Changed.insert(aContainer);
        World::Get().GetDispatcher().trigger(ContainerChangeEvent{aContainer, aItem});
        return;
    }
    SetItems(aContainer, aItem, aCount, std::move(aStacks));
}

void FakeEngine::SetLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    auto& reference = References.at(aReference);
    reference.Locked = aLocked;
    reference.LockLevel = aLevel;
    Changed.insert(aReference);
    Recorded.push_back(LockRow(aReference, reference));
}

void FakeEngine::SetItems(uint32_t aContainer, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks)
{
    auto& reference = References.at(aContainer);
    SetCount(reference, aItem, aCount, std::move(aStacks));
    Changed.insert(aContainer);
    Recorded.push_back(ItemsRow(aContainer, reference, aItem));
}

void FakeEngine::SetOpen(uint32_t aDoor, bool aOpen)
{
    auto& reference = References.at(aDoor);
    reference.Open = aOpen;
    Changed.insert(aDoor);
    Recorded.push_back(OpenRow(aDoor, reference));
}

void FakeEngine::ToggleDoor(uint32_t aObject)
{
    const auto it = References.find(aObject);
    if (it == References.end() || !it->second.Door)
        return;
    if (IsGuest)
        it->second.Open = !it->second.Open;
    else
        SetOpen(aObject, !it->second.Open);
}

std::vector<ObjectChange> FakeEngine::RowsOf(uint32_t aReference) const
{
    const auto& cReference = References.at(aReference);
    std::vector<ObjectChange> rows{LockRow(aReference, cReference)};
    if (cReference.Door)
        rows.push_back(OpenRow(aReference, cReference));
    for (const auto& [cItem, cCount] : cReference.Items)
        rows.push_back(ItemsRow(aReference, cReference, cItem));
    return rows;
}

bool FakeEngine::Apply(const ObjectChange& acChange, bool aLoad)
{
    const auto it = References.find(FormIdOf(acChange.Object));
    if (it == References.end() || !InMemory.contains(it->second.Cell))
        return false; // its cell never loaded here: "its object is not in this game"
    auto& reference = it->second;
    switch (acChange.Type)
    {
    case ObjectChange::Kind::kRecord: reference.RecordFlags = acChange.Value; break;
    case ObjectChange::Kind::kLock:
        reference.Locked = (acChange.Value & kLocked) != 0;
        reference.LockLevel = static_cast<uint8_t>(acChange.Value & 0xFF);
        break;
    case ObjectChange::Kind::kItems:
        SetCount(reference, FormIdOf(acChange.Item), static_cast<int32_t>(acChange.Value), acChange.Stacks);
        break;
    case ObjectChange::Kind::kOpenState: reference.Open = acChange.Value != 0; break;
    default: break;
    }
    (void)aLoad; // a state already as the host's is left as it is: the same either way here
    return true;
}

std::optional<std::vector<ObjectChange>> FakeEngine::CellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const
{
    const uint32_t cCell = FormIdOf(acCell);
    if (!Attached.contains(cCell))
        return std::nullopt;
    std::vector<ObjectChange> rows;
    for (const auto& [cId, cReference] : References)
    {
        const bool cNamed = std::ranges::any_of(acChanged, [cId](const ChangedReference& acRef) { return FormIdOf(acRef.Id) == cId; });
        if (cReference.Cell == cCell && (Changed.contains(cId) || cNamed))
            std::ranges::move(RowsOf(cId), std::back_inserter(rows));
    }
    return rows;
}

std::optional<CellStateRequest> FakeEngine::CellChanges(uint32_t aCell) const
{
    if (!Attached.contains(aCell))
        return std::nullopt;
    CellStateRequest request;
    request.Cell = SessionIdOf(aCell);
    for (const uint32_t cId : Changed)
        if (References.at(cId).Cell == aCell)
            request.Changed.push_back({SessionIdOf(cId), 1});
    return request;
}

ActorValues FakeEngine::FullValues()
{
    ActorValues values;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
        values.ActorValuesList[cIndex] = values.ActorMaxValuesList[cIndex] = 100.f;
    return values;
}

// The engine's temporary forms (a reference placed at run time, a copy's NPC): 0xFF ids; a deleted one's id goes to
// the next (docs/re/crashes/2026-10-14-reused-copy-form-id.md).
uint32_t FakeEngine::NewTemporary() noexcept
{
    ++m_made;
    if (m_freedTemporary.empty())
        return m_nextTemporary++;
    return m_freedTemporary.extract(m_freedTemporary.begin()).value();
}

uint32_t FakeEngine::LoadNpc(uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                        .Essential = EssentialBases.contains(aBase), .Made = m_made};
    return cId;
}

void FakeEngine::PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    Actors[aFormId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                            .Essential = EssentialBases.contains(aBase)};
}

void FakeEngine::CheckHealth(uint32_t aFormId, Actor& aActor) noexcept
{
    if (aActor.Copy || aActor.Dead || aActor.Values.ActorValuesList[kHealth] > 0.f)
        return;
    (aActor.Essential ? aActor.Bleeding : aActor.Dead) = true;
    if (aActor.Dead)
    {
        aActor.GraphUpdating = false;
        aActor.GraphState = "dead";
        if (aFormId == kPlayer)
        {
            aActor.DyingFrames = kDyingFrames;
            aActor.DeadFrames = 0;
        }
        auto& action = aActor.LatestAnimation;
        action.Tick = World::Get().GetTransport().GetTick();
        action.ActorId = aFormId;
        action.ActionId = 0x1337;
        action.State1 = DeathActions::kDying;
        action.EventName = "DeathAnim";
        World::Get().GetDispatcher().trigger(action);
        World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, false});
    }
}

void FakeEngine::Hit(uint32_t aFormId, float aDamage)
{
    DamageActorHealth(aFormId, 0, aDamage);
}

std::vector<uint32_t> FakeEngine::CopiesOf(uint32_t aBase) const
{
    std::vector<uint32_t> copies;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Copy && (!aBase || cActor.Base == aBase))
            copies.push_back(cId);
    return copies;
}

// The process lists: the actors in a cell loaded here (copies too: they are this game's actors), but the player.
std::vector<uint32_t> FakeEngine::GetLoadedActorFormIds() const noexcept
{
    std::vector<uint32_t> loaded;
    for (const auto& [cId, cActor] : Actors)
        if (cId != kPlayer && ActorHas3D(cId))
            loaded.push_back(cId);
    return loaded;
}

bool FakeEngine::ActorHas3D(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Loaded && !it->second.Disabled && (aFormId == kPlayer || Attached.contains(it->second.Cell));
}

void FakeEngine::PlayerTravels(uint32_t aCell, const glm::vec3& acPosition)
{
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
}

void FakeEngine::SetActorCopy(uint32_t aFormId, bool aCopy) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Copy = aCopy;
}

bool FakeEngine::IsActorCopy(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy;
}

void FakeEngine::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Player = aPlayer;
}

uint32_t FakeEngine::GetActorBaseFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Base : 0;
}

bool FakeEngine::SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !aBaseFormId)
        return false;
    it->second.Base = aBaseFormId;
    return true;
}

std::string FakeEngine::GetActorBaseName(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Name : std::string{};
}

float FakeEngine::GetActorBaseWeight(uint32_t aFormId) const noexcept
{
    if (aFormId == kPlayer)
        return Player.Weight;
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Weight : 50.f;
}

uint32_t FakeEngine::GetActorRaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Race : 0;
}

uint32_t FakeEngine::GetActorParentCellFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Cell : 0;
}

uint32_t FakeEngine::GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.WorldSpace : 0;
}

void FakeEngine::GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Position : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

void FakeEngine::GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Rotation : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

bool FakeEngine::IsActorDead(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Dead;
}

void FakeEngine::KillActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        (it->second.Essential ? it->second.Bleeding : it->second.Dead) = true;
        if (it->second.Dead)
        {
            it->second.GraphUpdating = false;
            it->second.GraphState = "dead";
        }
        if (!cWasDead && it->second.Dead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
    }
}

void FakeEngine::FinishDeath(uint32_t aFormId)
{
    Actors.at(aFormId).BlockResurrect = false;
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
}

void FakeEngine::ReplayDeathAction(uint32_t aFormId, const ActionEvent& acAction)
{
    if (!RefuseDeathActions)
        KillActor(aFormId);
    Actors.at(aFormId).LatestAnimation = acAction;
    World::Get().GetDispatcher().trigger(DeathActionReplayedEvent{aFormId, acAction.Tick});
}

ActionEvent FakeEngine::GetActorLatestAnimation(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.LatestAnimation : ActionEvent{};
}

void FakeEngine::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.LatestAnimation = acAction;
}

// Actor::Respawn: alive again, values restored; the value writers and Resurrect report it.
void FakeEngine::RespawnActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        const auto cAction = it->second.LatestAnimation;
        it->second.Dead = it->second.Bleeding = false;
        it->second.LatestAnimation = {};
        it->second.GraphState.clear();
        it->second.GraphUpdating = true;
        it->second.Values = FullValues();
        it->second.Bases = FullValues().ActorMaxValuesList;
        for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        if (cWasDead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, true, true,
                cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
    }
}

void FakeEngine::ResurrectActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ResurrectCalls;
        if (it->second.BlockResurrect)
            return;
        bool forced = false;
        DeathActions::WithLocalReset(forced, [&] {
            if (it->second.Dead && !forced)
            {
                ActionEvent reset = it->second.LatestAnimation;
                reset.Tick = World::Get().GetTransport().GetTick();
                reset.ActionId = 0xD1FDE;
                reset.EventName = "IdleForceDefaultState";
                World::Get().GetDispatcher().trigger(reset);
            }
            const auto cGraph = it->second.GraphState;
            const bool cUpdating = it->second.GraphUpdating;
            RespawnActor(aFormId);
            if (it->second.Copy)
            {
                it->second.GraphState = cGraph;
                it->second.GraphUpdating = cUpdating;
            }
        });
    }
}

bool FakeEngine::DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    const bool cWasDead = it->second.Dead;
    it->second.Values.ActorValuesList[kHealth] -= aDamage;
    World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, kHealth});
    // The engine names the killer before its death event.
    if (!cWasDead && it->second.Values.ActorValuesList[kHealth] <= 0.f)
        it->second.KilledBy = aAttackerFormId;
    CheckHealth(aFormId, it->second);
    return true;
}

bool FakeEngine::IsBleedingOut(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Bleeding;
}

bool FakeEngine::IsOtherPlayer(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy && it->second.Player;
}

bool FakeEngine::SetActorPersistent(uint32_t aFormId, bool aPersistent) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        return std::exchange(it->second.Persistent, aPersistent);
    return false;
}

// The player's faction is not modelled.
void FakeEngine::SetActorPlayerFaction(uint32_t) noexcept
{
}

// ForcePosition and SetRotation when placing, never of a copy lying in its ragdoll (the engine keeps it on its ragdoll,
// as its owner: SkyrimBridge); the graph's variables are not modelled.
bool FakeEngine::ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float,
                                     const AnimationVariables&, bool aPlace) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    if (!aPlace || RagdollPosed(aFormId))
        return true;
    std::copy_n(aPosition, 3, it->second.Position);
    std::copy_n(aRotation, 3, it->second.Rotation);
    // Outdoors its parent cell follows where it stands.
    if (it->second.WorldSpace)
        if (const uint32_t cCell = CellAt(it->second.WorldSpace, {aPosition[0], aPosition[1], aPosition[2]}))
        {
            const uint32_t cFrom = std::exchange(it->second.Cell, cCell);
            CellChanged(aFormId, cFrom);
        }
    return true;
}

float FakeEngine::GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++ValueReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++MaximumReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorMaxValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

void FakeEngine::SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ValueWrites;
        const auto cIndex = static_cast<uint32_t>(aIndex);
        auto& base = it->second.Bases[cIndex];
        const float cDelta = aValue - base;
        if (cDelta != 0.f)
        {
            base = aValue;
            it->second.Values.ActorValuesList[cIndex] += cDelta;
            it->second.Values.ActorMaxValuesList[cIndex] += cDelta;
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        }
    }
}

// A permanent modifier moves the maximum and current together; damage changes only the current value.
void FakeEngine::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    auto& values = it->second.Values;
    ++it->second.ValueWrites;
    const auto cBefore = values;
    const auto cIndex = static_cast<uint32_t>(aIndex);
    if (aForceMode == 0)
    {
        values.ActorValuesList[cIndex] += aValue - values.ActorMaxValuesList[cIndex];
        values.ActorMaxValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 2)
        values.ActorValuesList[cIndex] = std::min(aValue, values.ActorMaxValuesList[cIndex]);
    if (cBefore != values)
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
    if (cIndex == kHealth)
        CheckHealth(aFormId, it->second);
}

bool FakeEngine::IsActorDisabled(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Disabled;
}

bool FakeEngine::IsActorDeleted(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Deleted;
}

void FakeEngine::EnableActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Disabled = false;
}

void FakeEngine::DisableActor(uint32_t aFormId, bool aDelete) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Disabled = true;
        it->second.Deleted |= aDelete;
    }
}

// A copy: a new temporary actor of that NPC, where this game's player is (as PlaceAtMe from the player).
uint32_t FakeEngine::SpawnActorFromNpc(uint32_t aNpcFormId) noexcept
{
    ++Spawned[aNpcFormId];
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aNpcFormId, .Cell = Actors.at(kPlayer).Cell, .WorldSpace = Actors.at(kPlayer).WorldSpace,
                        .Created = true, .Essential = EssentialBases.contains(aNpcFormId), .Made = m_made};
    return cId;
}

namespace
{
// The race a player's appearance data holds (PlayerSwitchesRace writes "race <id>"), 0 for none.
uint32_t RaceInLooks(const std::string& acAppearance)
{
    uint32_t race = 0;
    return std::sscanf(acAppearance.c_str(), "race %X", &race) == 1 ? race : 0;
}
} // namespace

// A player's copy: a temporary NPC made from their appearance, and an actor of it.
uint32_t FakeEngine::CreateCustomActor(const std::string& acAppearance, uint32_t, float aWeight) noexcept
{
    const uint32_t cBase = NewTemporary();
    Npcs[cBase].Weight = aWeight;
    Npcs[cBase].Looks = acAppearance;
    Npcs[cBase].Race = RaceInLooks(acAppearance);
    return SpawnActorFromNpc(cBase);
}

uint32_t FakeEngine::SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept
{
    if (aFormId != kPlayer)
        return 0;
    aBuffer = Player.Base;
    return Player.BaseFlags;
}

// TESForm::MarkChanged: the sections are saved from now on.
void FakeEngine::MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept
{
    if (aFormId == kPlayer)
        Player.BaseFlags |= aFlags;
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // MoveTo tears down the native mount interaction, including when the moved reference is the horse.
    std::erase_if(Mounts, [&](const auto& acMount) { return acMount.first == aFormId || acMount.second == aFormId; });
    const uint32_t cFrom = it->second.Cell;
    const auto cExterior = Exteriors.find(aCellId);
    it->second.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    // Outdoors the cell at the position, as MoveTo picks it.
    const uint32_t cAt = it->second.WorldSpace ? CellAt(it->second.WorldSpace, {aX, aY, aZ}) : 0;
    it->second.Cell = cAt ? cAt : aCellId;
    it->second.Position[0] = aX;
    it->second.Position[1] = aY;
    it->second.Position[2] = aZ;
    CellChanged(aFormId, cFrom);
    // A created actor landing in a cell not loaded goes as with its cell's unload (measured: a player's copy spawned
    // four cells off is gone that frame, before SetActorPersistent).
    if (aFormId != kPlayer && it->second.Created && !it->second.Persistent && !Attached.contains(it->second.Cell))
    {
        DeleteActor(aFormId);
        return;
    }
    if (aFormId == kPlayer)
    {
        // Party travel announces it (PlayerCharacter.cpp's 40438), but another game's party call's move.
        if (World::Get().GetTransport().IsOnline() && !m_mirroredMove)
            World::Get().GetRunner().Trigger(PlayerTravelQueuedEvent{
                it->second.WorldSpace, it->second.WorldSpace ? 0u : it->second.Cell, {aX, aY, aZ}, Heading});
        World::Get().ctx().at<FakeDialogue&>().End();
        LoadAroundPlayer(cFrom);
        Teleported = true;
        ++Travels;
    }
}

void FakeEngine::CellChanged(uint32_t aFormId, uint32_t aFrom)
{
    const Actor& cActor = Actors.at(aFormId);
    const auto cLocation = [&](uint32_t aCell) {
        const auto it = Locations.find(aCell);
        return it != Locations.end() ? it->second : 0u;
    };
    if (aFormId == kPlayer && cLocation(aFrom) != cLocation(cActor.Cell))
        RaisesStoryEvent(StoryEvent::TypeOf("CLOC"), {kPlayer, cLocation(aFrom), cLocation(cActor.Cell)});
}

void FakeEngine::RaisesStoryEvent(uint32_t aType, std::vector<uint64_t> aMembers)
{
    const auto it = std::ranges::find(StoryTypes, aType, &RegisteredStory::Id);
    if (DecidesWorld || it == StoryTypes.end())
        return;
    OwnStoryEvent event{static_cast<uint32_t>(it - StoryTypes.begin())};
    std::ranges::copy(aMembers | std::views::take(event.Members.size()), event.Members.begin());
    World::Get().GetRunner().Trigger(event);
}

void FakeEngine::Exterior(uint32_t aWorldSpace, uint32_t aFirstCell, int32_t aRadius)
{
    uint32_t cell = aFirstCell;
    for (int32_t x = -aRadius; x <= aRadius; ++x)
        for (int32_t y = -aRadius; y <= aRadius; ++y)
            Exteriors[cell++] = {aWorldSpace, x, y};
}

uint32_t FakeEngine::LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept
{
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpaceFormId && cCell.X == aX && cCell.Y == aY)
            return cId;
    return 0;
}

uint32_t FakeEngine::CellAt(uint32_t aWorldSpace, const glm::vec3& acPosition) const noexcept
{
    const auto cAt = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    return LookupCellByCoords(aWorldSpace, cAt.X, cAt.Y);
}

// The loaded grid's centre is the player's cell (TES::centerGridX/Y; the engine's own lag behind it not modelled).
void FakeEngine::LoadAroundPlayer(uint32_t aFrom)
{
    const auto& cPlayer = Actors.at(kPlayer);
    std::set<uint32_t> cells{cPlayer.Cell};
    if (const auto cCentre = Exteriors.find(cPlayer.Cell); cCentre != Exteriors.end())
        for (const auto& [cId, cCell] : Exteriors)
            if (cCell.WorldSpace == cCentre->second.WorldSpace &&
                GridCellCoords::IsCellInGridCell({cCell.X, cCell.Y}, {cCentre->second.X, cCentre->second.Y}, false))
                cells.insert(cId);
    std::set<uint32_t> left = std::exchange(m_playerCells, cells);
    left.insert(aFrom);
    for (const uint32_t cCell : left)
        if (!cells.contains(cCell))
            UnloadCell(cCell);
    for (const uint32_t cCell : cells)
        LoadCell(cCell);
}

void FakeEngine::PlayerStands(uint32_t aCell, const glm::vec3& acPosition)
{
    auto& player = Actors.at(kPlayer);
    const uint32_t cFrom = player.Cell;
    player.Cell = aCell;
    const auto cExterior = Exteriors.find(aCell);
    player.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    std::copy_n(&acPosition.x, 3, player.Position);
    LoadAroundPlayer(cFrom);
}

void FakeEngine::PlayerWalks(const glm::vec3& acPosition)
{
    const auto& cPlayer = Actors.at(kPlayer);
    PlayerStands(CellAt(cPlayer.WorldSpace, acPosition), acPosition);
}

void FakeEngine::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Rotation[0] = aX;
        it->second.Rotation[2] = aZ;
    }
}

// As SkyrimBridge's: only a reference made at run time is deleted, never the plugins' or the player.
void FakeEngine::DeleteActor(uint32_t aFormId) noexcept
{
    if (!IsActorTemporary(aFormId) || !Actors.erase(aFormId))
        return;
    m_freedTemporary.insert(aFormId);
}

bool FakeEngine::IsActorNpcTemporary(uint32_t aFormId) const noexcept
{
    return GetActorBaseFormId(aFormId) >= 0xFF000000;
}

Inventory FakeEngine::GetActorInventory(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Items : Inventory{};
}

void FakeEngine::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Items = acInventory;
}

// As Actor::GetEssentialActorValues: the pools, and this game's player's skills (6 to 23).
ActorValues FakeEngine::GetActorEssentialValues(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return {};
    const auto cEssential = [aFormId](uint32_t aIndex) {
        return (aIndex >= kHealth && aIndex <= kStamina) || (aFormId == kPlayer && aIndex >= 6 && aIndex <= 23);
    };
    ActorValues values;
    for (const auto& [cIndex, cValue] : it->second.Values.ActorValuesList)
        if (cEssential(cIndex))
            values.ActorValuesList[cIndex] = cValue;
    for (const auto& [cIndex, cValue] : it->second.Values.ActorMaxValuesList)
        if (cEssential(cIndex))
            values.ActorMaxValuesList[cIndex] = cValue;
    return values;
}

// Each value listed; the others stay.
void FakeEngine::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    for (const auto& [cIndex, cValue] : acValues.ActorMaxValuesList)
        it->second.Values.ActorMaxValuesList[cIndex] = cValue;
    for (const auto& [cIndex, cValue] : acValues.ActorValuesList)
        it->second.Values.ActorValuesList[cIndex] = cValue;
}

// Resurrect: alive, its pools full (its maximums kept), its death action over, reported as the Resurrect hook reports
// it; placement is the party follower move.
void FakeEngine::RevivePlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    if (!player.Dead)
        return;
    player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
    {
        player.Values.ActorValuesList[cIndex] = player.Values.ActorMaxValuesList[cIndex];
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{kPlayer, cIndex});
    }
    const auto cAction = std::exchange(player.LatestAnimation, {});
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer, true, true,
        cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
}

void FakeEngine::HoldDeathControls(bool aHold) noexcept
{
    constexpr uint32_t flags = 0x1 | 0x4 | 0x20 | 0x40 | 0x80 | 0x400;
    if (aHold && !DeathControlsHeld)
    {
        DeathControlsBefore = Controls & flags;
        Holds.Take(0xFFFFFFFE, flags);
        Controls &= ~flags;
    }
    else if (!aHold && DeathControlsHeld)
        Controls |= Holds.GiveBack(0xFFFFFFFE, flags) & DeathControlsBefore;
    DeathControlsHeld = aHold;
}

// PartyNatives' entry points (Papyrus/PartyNatives.h) on the current game's fake engine.
namespace PartyNatives
{
void SetInSession(bool aInSession) noexcept
{
    FakeEngine::Current().InSession = aInSession;
}

bool InSession() noexcept
{
    return FakeEngine::Current().InSession;
}

bool Run(const LocalCall& acCall, uint32_t aSource, uint32_t aKey) noexcept
{
    auto& engine = FakeEngine::Current();
    if (aKey)
        engine.Copies.Sent(aSource, aKey);
    engine.Queue(acCall, MirroredTag(aSource));
    return true;
}

void EndCopy(uint32_t aSource, uint32_t aKey, const LocalValue& acResult) noexcept
{
    auto& engine = FakeEngine::Current();
    if (const auto cStack = engine.Copies.Ended(aSource, aKey, acResult))
        engine.AnswerBox(*cStack, std::get<int32_t>(acResult));
}

void ForgetCopies(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    if (aSource)
        engine.Copies.Left(*aSource);
    else
        engine.Copies = {};
}

void GiveBackControls(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    for (const auto& cReturned : engine.Holds.Leave(aSource ? std::optional(MirroredTag(*aSource)) : std::nullopt))
    {
        if (!cReturned.Free)
            continue;
        const auto c = EnableFor(cReturned.Free, cReturned.NoneElse);
        engine.Queue(
            EnablePlayerControls(c.Movement, c.Fighting, c.CamSwitch, c.Looking, c.Sneaking, c.Menu, c.Activate),
            cReturned.Holder);
    }
}
} // namespace PartyNatives

// SystemQuests' seam: this game's list and instances (FakeEngine's).
namespace SystemQuests
{
bool Learn(uint32_t aQuest, bool aTell) noexcept
{
    auto& engine = FakeEngine::Current();
    if (!aQuest || !engine.SystemQuestList.Learn(aQuest))
        return false;
    if (const auto it = engine.QuestGlobals.find(aQuest); it != engine.QuestGlobals.end())
        engine.SystemQuestList.SetGlobals(aQuest, it->second);
    if (aTell)
        World::Get().GetRunner().Trigger(SystemQuestLearnedEvent{aQuest});
    return true;
}

bool Any() noexcept
{
    return FakeEngine::Current().SystemQuestList.Any();
}

bool Is(uint32_t aQuest) noexcept
{
    return FakeEngine::Current().SystemQuestList.Contains(aQuest);
}

bool IsGlobal(uint32_t aGlobal) noexcept
{
    return FakeEngine::Current().SystemQuestList.IsGlobal(aGlobal);
}

std::vector<uint32_t> All() noexcept
{
    return FakeEngine::Current().SystemQuestList.All();
}

void Clear() noexcept
{
    FakeEngine::Current().SystemQuestList.Clear();
}

void TimerCalled(uint32_t aQuest, std::string_view aNative, float aInterval) noexcept
{
    auto& engine = FakeEngine::Current();
    if (engine.SystemQuestList.Contains(aQuest))
        engine.QuestTimers[aQuest][std::string(aNative)] = {std::string(aNative), aInterval};
}

std::string ReadState() noexcept
{
    return FakeEngine::Current().ReadSystemQuests();
}

void ApplyState(std::string_view aState) noexcept
{
    FakeEngine::Current().ApplySystemQuests(aState);
}
} // namespace SystemQuests

namespace
{
GameId ServerId(uint32_t aFormId)
{
    return ToSession(aFormId).value_or(GameId{});
}
} // namespace

uint8_t FakeEngine::GetFormType(uint32_t aFormId) const noexcept
{
    const auto it = ItemForms.find(aFormId);
    return it != ItemForms.end() ? it->second.Type : 0;
}

Inventory::Entry& FakeEngine::EntryOf(Actor& aActor, uint32_t aItem)
{
    const GameId cId = ServerId(aItem);
    for (auto& entry : aActor.Items.Entries)
        if (entry.BaseId == cId)
            return entry;
    Inventory::Entry entry;
    entry.BaseId = cId;
    entry.Count = 1;
    return aActor.Items.Entries.emplace_back(entry);
}

void FakeEngine::Equips(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    Equip(aActor, aItem, aSlot);
}

void FakeEngine::Unequips(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    Unequip(aActor, aItem, aSlot);
}

void FakeEngine::ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Equip(aActorFormId, aItemFormId, aEquipSlotFormId);
}

void FakeEngine::ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Unequip(aActorFormId, aItemFormId, aEquipSlotFormId);
}

// EquipHook, then the engine: a hand holds one weapon, an armor takes its slots from what wore them; what it
// replaces comes off through the unequip, under the hook's ScopedUnequipOverride (no event).
void FakeEngine::Equip(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    const auto cActor = Actors.find(aActor);
    const auto cForm = ItemForms.find(aItem);
    if (cActor == Actors.end() || cForm == ItemForms.end())
        return;
    if (cActor->second.Copy && !ScopedEquipOverride::IsOverriden())
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(
            EquipmentChangeEvent{.ActorId = aActor, .ItemId = aItem, .EquipSlotId = aSlot, .Count = 1});

    ScopedUnequipOverride _;
    const bool cWeapon = cForm->second.Type == kWeapon;
    const bool cLeft = cWeapon && aSlot == kLeftHand;
    std::vector<std::pair<uint32_t, uint32_t>> replaced; // item, slot
    for (const auto& cEntry : cActor->second.Items.Entries)
    {
        const uint32_t cWornId = ToGame(cEntry.BaseId);
        const auto cWornForm = ItemForms.find(cWornId);
        if (cWornForm == ItemForms.end() || cWornId == aItem)
            continue;
        if (cWeapon && cWornForm->second.Type == kWeapon && (cLeft ? cEntry.ExtraWornLeft : cEntry.ExtraWorn))
            replaced.emplace_back(cWornId, aSlot);
        else if (!cWeapon && cEntry.ExtraWorn && (cWornForm->second.Slots & cForm->second.Slots))
            replaced.emplace_back(cWornId, 0);
    }
    // Ours (EquipManager::Equip, not applied at once) puts no armor over armor: measured, the copy of a player that
    // swapped iron armor for steel wears the steel after InventoryService's unequip-all, equip, re-equip.
    if (!cWeapon && !replaced.empty() && ScopedEquipOverride::IsOverriden())
        return;
    for (const auto& [cItem, cSlot] : replaced)
        Unequip(aActor, cItem, cSlot);

    auto& entry = EntryOf(cActor->second, aItem);
    (cLeft ? entry.ExtraWornLeft : entry.ExtraWorn) = true;
}

// UnEquipHook, then the engine: off that hand (both for no hand's slot).
void FakeEngine::Unequip(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    const auto cActor = Actors.find(aActor);
    if (cActor == Actors.end())
        return;
    if (cActor->second.Copy && !ScopedEquipOverride::IsOverriden() && !ScopedInventoryOverride::IsOverriden() &&
        !ScopedItemRemovalOverride::IsOverriden())
        return;
    if (!cActor->second.Copy && !ScopedUnequipOverride::IsOverriden())
        World::Get().GetRunner().Trigger(EquipmentChangeEvent{
            .ActorId = aActor, .ItemId = aItem, .EquipSlotId = aSlot, .Count = 1, .Unequip = true});

    const GameId cId = ServerId(aItem);
    for (auto& entry : cActor->second.Items.Entries)
        if (entry.BaseId == cId)
        {
            if (aSlot != kLeftHand)
                entry.ExtraWorn = false;
            if (aSlot != kRightHand)
                entry.ExtraWornLeft = false;
        }
}

std::set<std::tuple<uint32_t, uint32_t, bool>> FakeEngine::Worn(uint32_t aActor) const
{
    std::set<std::tuple<uint32_t, uint32_t, bool>> worn;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
        {
            if (cEntry.ExtraWorn)
                worn.emplace(cEntry.BaseId.ModId, cEntry.BaseId.BaseId, false);
            if (cEntry.ExtraWornLeft)
                worn.emplace(cEntry.BaseId.ModId, cEntry.BaseId.BaseId, true);
        }
    return worn;
}

// Actor::GetEquipment: the worn entries.
Inventory FakeEngine::GetActorEquipment(uint32_t aActorFormId) const noexcept
{
    Inventory equipment;
    if (const auto it = Actors.find(aActorFormId); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            if (cEntry.IsWorn())
                equipment.Entries.push_back(cEntry);
    return equipment;
}

// TESObjectREFR::GetWornArmor: the worn armors.
Inventory FakeEngine::GetActorWornArmor(uint32_t aActorFormId) const noexcept
{
    Inventory armor = GetActorEquipment(aActorFormId);
    std::erase_if(armor.Entries, [&](const Inventory::Entry& acEntry) {
        return GetFormType(ToGame(acEntry.BaseId)) != kArmor;
    });
    return armor;
}

void FakeEngine::AddItems(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    const auto cActor = Actors.find(aActor);
    if (cActor == Actors.end() || !aCount)
        return;
    const GameId cId = ServerId(aItem);
    auto& entries = cActor->second.Items.Entries;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& acEntry) { return acEntry.BaseId == cId; });
    if (it == entries.end())
    {
        if (aCount < 0)
            return;
        Inventory::Entry entry;
        entry.BaseId = cId;
        entries.push_back(entry);
        it = std::prev(entries.end());
    }
    if (it->Count + aCount <= 0)
    {
        if (it->IsWorn())
            Unequip(aActor, aItem, 0);
        std::erase_if(entries, [&](const auto& acEntry) { return acEntry.BaseId == cId; });
        return;
    }
    it->Count += aCount;
}

void FakeEngine::GetsItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        Inventory::Entry item;
        item.BaseId = ServerId(aItem);
        item.Count = aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    }
    AddItems(aActor, aItem, aCount);
}

void FakeEngine::GetsItem(uint32_t aActor, const Inventory::Entry& acItem)
{
    if (!ScopedInventoryOverride::IsOverriden())
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, acItem));
    AddEntry(aActor, acItem);
}

void FakeEngine::LosesItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        Inventory::Entry item;
        item.BaseId = ServerId(aItem);
        item.Count = -aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    }
    AddItems(aActor, aItem, -aCount);
}

void FakeEngine::Drops(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    Inventory::Entry item;
    item.BaseId = ServerId(aItem);
    item.Count = -aCount;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item, true));
    ScopedInventoryOverride _;
    AddItems(aActor, aItem, -aCount);
}

uint32_t FakeEngine::DropsInWorld(const Inventory::Entry& acItem)
{
    Inventory::Entry removed = acItem;
    removed.Count = -acItem.Count;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(kPlayer, removed, true));
    {
        ScopedInventoryOverride _;
        AddEntry(kPlayer, removed);
    }
    const uint32_t cId = PlaceLoose(Actors.at(kPlayer).Cell, acItem);
    if (IsGuest)
        World::Get().GetRunner().Trigger(ItemDroppedEvent{cId});
    else
        RecordCreated(cId);
    return cId;
}

void FakeEngine::PicksUp(uint32_t aObject)
{
    const auto it = Loose.find(aObject);
    if (it == Loose.end())
        return;
    if (IsGuest && !PickUpGranted && HostDecides.contains(aObject))
    {
        World::Get().GetRunner().Trigger(ItemPickUpEvent{aObject});
        return;
    }
    const Inventory::Entry cItem = it->second.Item;
    Loose.erase(it);
    HostDecides.erase(aObject);
    m_freedTemporary.insert(aObject);
    World::Get().GetRunner().Trigger(InventoryChangeEvent(kPlayer, cItem));
    {
        ScopedInventoryOverride _;
        AddEntry(kPlayer, cItem);
    }
    if (!IsGuest)
        RecordCreated(aObject);
}

std::optional<GameId> FakeEngine::CreateLoose(const Inventory::Entry& acItem, uint32_t aCell)
{
    if (!Attached.contains(aCell))
        return std::nullopt;
    if (MissingForms.contains(ToGame(acItem.BaseId)))
        return GameId{};
    const uint32_t cId = PlaceLoose(aCell, acItem);
    RecordCreated(cId);
    return CreatedId(cId);
}

uint32_t FakeEngine::PlaceLoose(uint32_t aCell, const Inventory::Entry& acItem)
{
    const uint32_t cId = NewTemporary();
    Loose[cId] = {aCell, acItem};
    return cId;
}

std::optional<Inventory::Entry> FakeEngine::TakeLoose(const GameId& acObject)
{
    const uint32_t cId = 0xFF000000 | acObject.BaseId;
    const auto it = acObject.ModId == ObjectChange::kCreatedModId ? Loose.find(cId) : Loose.end();
    if (it == Loose.end())
        return std::nullopt;
    const Inventory::Entry cItem = it->second.Item;
    Loose.erase(it);
    m_freedTemporary.insert(cId);
    RecordCreated(cId);
    return cItem;
}

void FakeEngine::RecordCreated(uint32_t aObject)
{
    const auto it = Loose.find(aObject);
    if (it == Loose.end())
    {
        // ReferenceStates::Deleted
        Recorded.push_back({.Type = ObjectChange::Kind::kEnabled, .Object = CreatedId(aObject), .Value = 2});
        return;
    }
    ObjectChange create{.Type = ObjectChange::Kind::kCreate, .Object = CreatedId(aObject), .Item = it->second.Item.BaseId};
    create.Data = ObjectChange::PackPlacement({.Cell = SessionIdOf(it->second.Cell)});
    create.Stacks = {it->second.Item};
    Recorded.push_back(std::move(create));
}

std::vector<Inventory::Entry> FakeEngine::EntriesOf(uint32_t aActor, uint32_t aItem) const
{
    std::vector<Inventory::Entry> entries;
    for (const auto& cEntry : Actors.at(aActor).Items.Entries)
        if (cEntry.BaseId == ServerId(aItem))
            entries.push_back(cEntry);
    return entries;
}

void FakeEngine::AddEntry(uint32_t aActor, const Inventory::Entry& acItem)
{
    Inventory::Entry stack = acItem;
    stack.ExtraWorn = stack.ExtraWornLeft = false;
    if (!stack.ContainsExtraData())
        return AddItems(aActor, ToGame(acItem.BaseId), acItem.Count);
    auto& entries = Actors.at(aActor).Items.Entries;
    const auto it = std::ranges::find_if(entries, [&](const Inventory::Entry& acEntry) { return acEntry.CanBeMerged(stack); });
    if (it == entries.end())
    {
        if (stack.Count > 0)
            entries.push_back(stack);
        return;
    }
    it->Count += stack.Count;
    if (it->Count <= 0)
        entries.erase(it);
}

int32_t FakeEngine::CountOf(uint32_t aActor, uint32_t aItem) const
{
    int32_t count = 0;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            if (cEntry.BaseId.BaseId == (aItem & 0x00FFFFFF))
                count += cEntry.Count;
    return count;
}

std::map<std::pair<uint32_t, uint32_t>, int32_t> FakeEngine::Carried(uint32_t aActor) const
{
    std::map<std::pair<uint32_t, uint32_t>, int32_t> carried;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            carried[{cEntry.BaseId.ModId, cEntry.BaseId.BaseId}] += cEntry.Count;
    return carried;
}

// Added (a worn one equipped, as SetInventory's adds do) or removed.
void FakeEngine::ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
{
    const uint32_t cItem = ToGame(aItem.BaseId);
    AddEntry(aObjectFormId, aItem);
    if (aItem.Count > 0 && aItem.IsWorn())
    {
        ScopedEquipOverride _;
        Equip(aObjectFormId, cItem, aItem.ExtraWornLeft ? kLeftHand : kRightHand);
    }
}

WeaponState FakeEngine::GetWeaponState(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Weapon : WeaponState::Sheathed;
}

void FakeEngine::SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Weapon = aDraw ? WeaponState::Drawn : WeaponState::Sheathed;
}

void FakeEngine::PlacesMarker(const glm::vec3& acPosition, uint32_t aWorldSpace)
{
    Vector3_NetQuantize position;
    position.x = acPosition.x;
    position.y = acPosition.y;
    position.z = acPosition.z;
    World::Get().GetRunner().Trigger(SetWaypointEvent(position, aWorldSpace));
    Waypoint = Marker{acPosition, aWorldSpace};
}

void FakeEngine::ChoosesDifficulty(uint32_t aDifficulty)
{
    Difficulty = aDifficulty;
    World::Get().GetRunner().Trigger(DifficultyChangeEvent{aDifficulty});
}

void FakeEngine::PlayerSwitchesRace(uint32_t aRace)
{
    Npcs[Actors.at(kPlayer).Base].Race = aRace;
    Player.Base = std::format("race {:X}", aRace);
    Player.BaseFlags |= 0x2000000; // CHANGE_NPC_RACE
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerFormChangeEvent{});
}

void FakeEngine::PlayerUsesRaceMenu(const std::function<void()>& acChanges)
{
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", true});
    acChanges();
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", false});
}

std::string FakeEngine::PlayerId() noexcept
{
    return std::format("id:{:016x}", std::hash<std::string>{}(Name));
}

namespace
{
// The skills' actor values from 6 on, as Papyrus names them (ActorValueInfo).
constexpr std::array<std::string_view, 18> kSkills{
    "OneHanded",  "TwoHanded",  "Marksman",    "Block",    "Smithing",    "HeavyArmor",
    "LightArmor", "Pickpocket", "Lockpicking", "Sneak",    "Alchemy",     "Speechcraft",
    "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"};
constexpr std::array<std::pair<std::string_view, int32_t>, 4> kStats{{{"Health", 24}, {"Magicka", 25}, {"Stamina", 26}, {"CarryWeight", 32}}};

template <class T> T ArgumentOf(const LocalCall& acCall, size_t aIndex, T aDefault = {})
{
    const auto* pValue = aIndex < acCall.Arguments.size() ? std::get_if<T>(&acCall.Arguments[aIndex]) : nullptr;
    return pValue ? *pValue : aDefault;
}

int32_t SkillOf(const LocalCall& acCall)
{
    const auto cName = ArgumentOf<std::string>(acCall, 0);
    const auto cIt = std::ranges::find_if(kSkills, [&](std::string_view aSkill) { return SameName(cName, aSkill); });
    return cIt == kSkills.end() ? -1 : 6 + static_cast<int32_t>(cIt - kSkills.begin());
}
} // namespace

void FakeEngine::TakeProgress(const LocalCall& acCall, uint32_t aActor)
{
    const auto cIs = [&](std::string_view aFunction) { return SameName(acCall.Function, aFunction); };
    const uint32_t cForm = ArgumentOf<LocalForm>(acCall, 0).FormId;
    const bool cPlayer = aActor == kPlayer;
    if (cIs("TeachWord") || cIs("UnlockWord"))
    {
        if (cIs("TeachWord"))
            ++WordsLearnedShown;
        WordsKnown.insert(cForm);
        if (cIs("UnlockWord"))
            WordsUnlocked.insert(cForm);
        return;
    }
    if (cIs("SetRace"))
    {
        if (cPlayer)
            PlayerSwitchesRace(cForm);
        else
            Npcs[Actors.at(aActor).Base].Race = cForm;
        return;
    }
    if (cIs("AddToFaction") || cIs("SetFactionRank"))
        Factions[aActor][cForm] = ArgumentOf<int32_t>(acCall, 1);
    else if (cIs("RemoveFromFaction"))
        Factions[aActor].erase(cForm);
    else if (cIs("AddSpell"))
        AddedSpells[aActor].insert(cForm);
    else if (cIs("RemoveSpell"))
        AddedSpells[aActor].erase(cForm);
    else if (cIs("ModActorValue") && SameName(ArgumentOf<std::string>(acCall, 0), "DragonSouls"))
    {
        const float cAmount = ArgumentOf<float>(acCall, 1);
        Souls[aActor] += cAmount;
        if (cPlayer && cAmount > 0.f)
            ++SoulsAbsorbedShown;
    }
    else if (cPlayer)
    {
        RewardCalls.push_back(acCall);
        if (cIs("AdvanceSkill"))
            RewardExperience[SkillOf(acCall)] += ArgumentOf<float>(acCall, 1) * RewardMultiplier;
        else if (cIs("IncrementSkill") || cIs("IncrementSkillBy"))
            RewardLevels[SkillOf(acCall)] += cIs("IncrementSkill") ? 1u : static_cast<uint32_t>(ArgumentOf<int32_t>(acCall, 1));
        else if (cIs("AddPerkPoints"))
            RewardPoints += ArgumentOf<int32_t>(acCall, 0);
        else if (cIs("AddPerk"))
        {
            RewardPerks.insert(cForm);
            const auto cId = SessionIdOf(cForm);
            if (std::ranges::none_of(Player.Perks, [&](const auto& cPerk) { return cPerk.Id == cId; }))
                Player.Perks.push_back({cId, 1});
        }
        else if (cIs("RemovePerk"))
        {
            RewardPerks.erase(cForm);
            std::erase_if(Player.Perks, [&](const auto& cPerk) { return cPerk.Id == SessionIdOf(cForm); });
        }
        else if (cIs("ModActorValue"))
            for (const auto& [cName, cValue] : kStats)
                if (SameName(ArgumentOf<std::string>(acCall, 0), cName))
                    RewardStats[cValue] += ArgumentOf<float>(acCall, 1);
    }
}

void FakeEngine::TeachesWord(uint32_t aWord, uint32_t aCause)
{
    RunScript({.Class = "Game", .Function = "TeachWord", .Arguments = {LocalForm{aWord}}}, aCause);
}

void FakeEngine::UnlocksWord(uint32_t aWord, uint32_t aCause)
{
    RunScript({.Class = "Game", .Function = "UnlockWord", .Arguments = {LocalForm{aWord}}}, aCause);
}

// The engine's own, in the Magic menu: no script, so this player's own.
void FakeEngine::SpendsSoul(uint32_t aWord)
{
    if (Souls[kPlayer] < 1.f || !WordsKnown.contains(aWord) || !WordsUnlocked.insert(aWord).second)
        return;
    Souls[kPlayer] -= 1.f;
}

void FakeEngine::ModifiesSouls(uint32_t aActor, float aAmount, uint32_t aCause)
{
    RunScript({.Class = "Actor", .Function = "ModActorValue", .Self = LocalForm{aActor}, .Arguments = {std::string("DragonSouls"), aAmount}},
              aCause);
}

void FakeEngine::SetsFaction(uint32_t aActor, uint32_t aFaction, int32_t aRank, uint32_t aCause, bool aStory, uint32_t aQuest)
{
    LocalCall call{.Class = "Actor", .Function = aRank < 0 ? "RemoveFromFaction" : "SetFactionRank", .Self = LocalForm{aActor},
                   .Arguments = {LocalForm{aFaction}}};
    if (aRank >= 0)
        call.Arguments.emplace_back(aRank);
    RunScript(call, aCause, aStory, BodyQuests.contains(aQuest));
}

Perks FakeEngine::GetPlayerPerks() const noexcept
{
    Perks perks{Player.Perks};
    if (const auto it = AddedSpells.find(kPlayer); it != AddedSpells.end())
        for (const uint32_t cSpell : it->second)
            if (const auto cId = ToSession(cSpell))
                perks.Spells.push_back(*cId);
    return perks;
}

void FakeEngine::SetCopyPerks(uint32_t aFormId, const Perks& acPerks) noexcept
{
    ActorPerks[aFormId] = acPerks.Ranks;
    auto& spells = AddedSpells[aFormId];
    spells.clear();
    for (const GameId& cId : acPerks.Spells)
        spells.insert(ToGame(cId));
}

void FakeEngine::AddsSpell(uint32_t aActor, uint32_t aSpell, bool aAdded, uint32_t aCause, bool aStory, uint32_t aQuest)
{
    RunScript({.Class = "Actor", .Function = aAdded ? "AddSpell" : "RemoveSpell", .Self = LocalForm{aActor}, .Arguments = {LocalForm{aSpell}}},
              aCause, aStory, BodyQuests.contains(aQuest));
}

PlayerRecord FakeEngine::ReadPlayer() const noexcept
{
    PlayerRecord record = Player;
    if (record.Name.empty()) record.Name = Name;
    const Actor& cPlayer = Actors.at(kPlayer);
    // As JoinBridge: what the player carries, from its inventory; no place. Its souls and the words it unlocked.
    record.Items = cPlayer.Items;
    std::erase_if(record.Values, [](const PlayerRecord::Value& acValue) { return acValue.Index == 133; });
    record.Values.push_back({133, Souls.contains(kPlayer) ? Souls.at(kPlayer) : 0.f});
    for (const auto& [cIndex, cValue] : RewardStats)
    {
        std::erase_if(record.PermanentValues, [&](const PlayerRecord::Value& acValue) { return acValue.Index == cIndex; });
        record.PermanentValues.push_back({static_cast<uint8_t>(cIndex), cValue});
    }
    record.Words.clear();
    for (const uint32_t cWord : WordsUnlocked)
        record.Words.push_back(GameId(0, cWord));
    if (const auto cSpells = AddedSpells.find(kPlayer); cSpells != AddedSpells.end())
        for (const uint32_t cSpell : cSpells->second)
            if (std::ranges::find(record.Spells, GameId(0, cSpell)) == record.Spells.end())
                record.Spells.push_back(GameId(0, cSpell));
    // As JoinBridge: its own system quests' instances.
    record.Extras.erase(SystemQuestState::kExtra);
    if (SystemQuestList.Any())
        record.Extras[SystemQuestState::kExtra] = ReadSystemQuests();
    return record;
}

void FakeEngine::ApplyPlayer(const PlayerRecord& acRecord) noexcept
{
    Actor& cPlayer = Actors.at(kPlayer);
    Player = acRecord;
    Player.Key.clear();
    Player.Items = {};
    cPlayer.Items = acRecord.Items;
    AddedSpells[kPlayer].clear();
    for (const auto& cSpell : acRecord.Spells)
        AddedSpells[kPlayer].insert(cSpell.BaseId);
    // As JoinBridge: its own souls and unlocked words, replacing the save's purchased flags with no HUD.
    for (const PlayerRecord::Value& cValue : acRecord.Values)
        if (cValue.Index == 133)
            Souls[kPlayer] = cValue.Base;
    WordsUnlocked.clear();
    for (const GameId& cWord : acRecord.Words)
        WordsUnlocked.insert(cWord.BaseId);
    RewardStats.clear();
    for (const PlayerRecord::Value& cValue : acRecord.PermanentValues)
        RewardStats[cValue.Index] = cValue.Base;
    Player.Words.clear();
    // As JoinBridge: at the record's place (kept in the host's save), else where the save put it, the host's spot.
    if (acRecord.Cell)
    {
        cPlayer.Cell = acRecord.Cell.BaseId;
        std::ranges::copy(acRecord.Position, cPlayer.Position);
    }
    Player.Cell = {};
    Player.WorldSpace = {};
    Player.Position = {};
    const auto cQuests = acRecord.Extras.find(SystemQuestState::kExtra);
    ApplySystemQuests(cQuests != acRecord.Extras.end() ? cQuests->second : std::string{});
    Player.Extras.erase(SystemQuestState::kExtra);
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::SaveNow(const std::string& acName) noexcept
{
    if (MadeSave)
        return *std::exchange(MadeSave, std::nullopt);
    if (!PendingSave)
        PendingSave = acName;
    return std::nullopt;
}

bool FakeEngine::WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept
{
    PendingLoad = {acName, acFiles};
    return true;
}

// The save's game file is the player's record; its co-save the records SaveService writes.
void FakeEngine::RunMainLoop() noexcept
{
    if (const auto cName = std::exchange(PendingSave, std::nullopt))
    {
        if (RefusesSaves || !World::Get().ctx().at<SaveService>().OnSaveRequested())
            MadeSave = std::optional<SaveFiles>{};
        else
        {
            Saves.push_back(*cName);
            Wire::Buffer buffer;
            Wire::Buffer::Writer writer(&buffer);
            // The save holds where its player stands.
            PlayerRecord player = ReadPlayer();
            player.Knowledge = Knowledge;
            const Actor& cPlayer = Actors.at(kPlayer);
            player.Cell = GameId(0, cPlayer.Cell);
            player.Position = {cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]};
            player.Serialize(writer);
            Serialization::WriteVarInt(writer, References.size());
            for (const auto& [id, r] : References)
            {
                Serialization::WriteVarInt(writer, id);
                Serialization::WriteBool(writer, r.Locked);
            }
            // The words' flags (the world's), the player's own factions (its base's, kept with the save).
            for (const auto* pWords : {&WordsKnown, &WordsUnlocked})
            {
                Serialization::WriteVarInt(writer, pWords->size());
                for (const uint32_t cWord : *pWords)
                    Serialization::WriteVarInt(writer, cWord);
            }
            const auto& cFactions = Factions[kPlayer];
            Serialization::WriteVarInt(writer, cFactions.size());
            for (const auto& [cFaction, cRank] : cFactions)
            {
                Serialization::WriteVarInt(writer, cFaction);
                Serialization::WriteVarInt(writer, static_cast<uint32_t>(cRank));
            }
            MadeSave = SaveFiles{{reinterpret_cast<const char*>(buffer.GetData()), writer.Size()}, World::Get().ctx().at<SaveService>().WriteCoSave()};
            SavedFiles[*cName] = **MadeSave;
        }
    }
    const auto cLoad = std::exchange(PendingLoad, std::nullopt);
    if (!cLoad)
        return;
    const auto& [cName, cFiles] = *cLoad;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(cFiles.Game.data()), cFiles.Game.size()));
    PlayerRecord saved;
    saved.Deserialize(reader);
    const auto count = Serialization::ReadVarInt(reader);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t id = Serialization::ReadVarInt(reader);
        References[id].Locked = Serialization::ReadBool(reader);
    }
    for (auto* pWords : {&WordsKnown, &WordsUnlocked})
    {
        pWords->clear();
        const auto cWords = Serialization::ReadVarInt(reader);
        for (size_t i = 0; i < cWords; ++i)
            pWords->insert(static_cast<uint32_t>(Serialization::ReadVarInt(reader)));
    }
    auto& factions = Factions[kPlayer];
    factions.clear();
    const auto cFactions = Serialization::ReadVarInt(reader);
    for (size_t i = 0; i < cFactions; ++i)
    {
        const auto cFaction = static_cast<uint32_t>(Serialization::ReadVarInt(reader));
        factions[cFaction] = static_cast<int32_t>(Serialization::ReadVarInt(reader));
    }
    // The save's player's souls: its value in the save (the record sets the joining character's own).
    Souls[kPlayer] = 0.f;
    for (const PlayerRecord::Value& cValue : saved.Values)
        if (cValue.Index == 133)
            Souls[kPlayer] = cValue.Base;
    // The save's system quests: its list (the co-save's) and their instances, the saving game's player's (the VM's).
    SystemQuestList.Clear();
    QuestVariables.clear();
    QuestTimers.clear();
    if (const auto cQuests = saved.Extras.find(SystemQuestState::kExtra); cQuests != saved.Extras.end())
        if (const auto cState = SystemQuestState::Decode(cQuests->second))
        {
            for (const SystemQuestState::Quest& cQuest : cState->Quests)
                SystemQuests::Learn(cQuest.Id.BaseId, false);
            ApplySystemQuests(cQuests->second);
        }
    saved.Extras.erase(SystemQuestState::kExtra);
    Player = saved;
    Knowledge = saved.Knowledge;
    Player.Items = {};
    Player.Cell = {};
    Player.Position = {};
    Actor& player = Actors.at(kPlayer);
    player.Bleeding = player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    player.Values = FullValues();
    player.Items = saved.Items;
    player.Cell = saved.Cell.BaseId;
    std::copy(saved.Position.begin(), saved.Position.end(), player.Position);
    LoadCell(player.Cell);
    auto& saves = World::Get().ctx().at<SaveService>();
    saves.RevertCoSave();
    saves.ReadCoSave(cFiles.CoSave, CharacterRecords::kVersion);
    AtMainMenu = false;
    Loaded.push_back(cName);
    World::Get().GetRunner().Trigger(GameLoadedEvent{});
}

bool FakeEngine::Autosaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested(true))
        return false;
    Saves.push_back(acName);
    return true;
}

// As Save_Impl's hook: SaveService decides first.
bool FakeEngine::PlayerSaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested())
        return false;
    Saves.push_back(acName);
    return true;
}

// From the map ("Remove It"): the only removal our hook raises.
void FakeEngine::RemovesMarker()
{
    World::Get().GetRunner().Trigger(RemoveWaypointEvent());
    Waypoint.reset();
}

// HookSetGlobalTimeMultiplier: a change of this game's own, raised while connected.
void FakeEngine::SlowsTime(float aMultiplier)
{
    if (aMultiplier != WantedTimeScale && World::Get().GetTransport().IsConnected())
        World::Get().GetRunner().Trigger(TimeScaleEvent{aMultiplier});
    WantedTimeScale = aMultiplier;
}

void FakeEngine::CastsSpell(uint32_t aCaster, uint32_t aSpell, uint32_t aSource, uint32_t aTarget)
{
    const auto cActor = Actors.find(aCaster);
    if (cActor == Actors.end())
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(SpellCastEvent(aCaster, aSpell, aTarget, aSource, false, cActor->second.Loaded));
    Casts.push_back({aCaster, aSpell, aSource, aTarget});
}

void FakeEngine::SpellHits(uint32_t aTarget, uint32_t aCaster, uint32_t aSpell, uint32_t aEffect, float aMagnitude)
{
    const auto cTarget = Actors.find(aTarget);
    if (cTarget == Actors.end())
        return;
    const bool cOurCast = Actors.contains(aCaster) && !Actors.at(aCaster).Copy;
    if (cTarget->second.Copy)
    {
        if (!cOurCast)
            return;
        AddTargetEvent event;
        event.TargetID = aTarget;
        event.CasterID = aCaster;
        event.SpellID = aSpell;
        event.EffectID = aEffect;
        event.Magnitude = aMagnitude;
        World::Get().GetRunner().Trigger(event);
        return;
    }
    Effects.push_back({aTarget, aCaster, aSpell, aEffect});
}

void FakeEngine::Fights(uint32_t aActor, uint32_t aTarget)
{
    auto& ai = Actors.at(aActor).Ai;
    ai.CombatState = aTarget ? 1 : 0;
    ai.CombatTargetId = aTarget;
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::Burns(uint32_t aActor, uint32_t aEffect, bool aOn)
{
    auto& visuals = Actors.at(aActor).Visuals;
    aOn ? (void)visuals.insert(aEffect) : (void)visuals.erase(aEffect);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, 0});
}

std::vector<IActorBridge::ActiveEffectInfo> FakeEngine::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> effects;
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        for (const uint32_t cId : it->second.Visuals)
            effects.push_back({.EffectFormId = cId});
    return effects;
}

HitRules::Party FakeEngine::PartyOf(uint32_t aFormId) const noexcept
{
    using enum HitRules::Party;
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return None;
    if (aFormId == kPlayer)
        return LocalPlayer;
    if (it->second.Copy)
        return it->second.Player ? OtherPlayer : Copy;
    return Local;
}

void FakeEngine::TakesDamage(uint32_t aHittee, uint32_t aHitter, float aDamage, bool aApplyingEffect)
{
    switch (HitRules::Of(PartyOf(aHittee), PartyOf(aHitter), aApplyingEffect))
    {
    case HitRules::Route::Apply:
        World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage));
        DamageActorHealth(aHittee, aHitter, aDamage);
        break;
    case HitRules::Route::AskOwner: World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage, aHitter)); break;
    case HitRules::Route::Mirror:
    case HitRules::Route::Drop: break;
    }
}

void FakeEngine::Strikes(uint32_t aHitter, uint32_t aHittee, float aDamage)
{
    if (!Actors.contains(aHittee))
        return;
    World::Get().GetRunner().Trigger(HitEvent(aHitter, aHittee));
    TakesDamage(aHittee, aHitter, aDamage, false);
}

void FakeEngine::Shoots(uint32_t aShooter, uint32_t aSpell, const glm::vec3& acOrigin)
{
    ProjectileLaunchedEvent event{};
    event.Origin = acOrigin;
    event.ShooterID = aShooter;
    event.SpellID = aSpell;
    event.ParentCellID = Actors.at(aShooter).Cell;
    World::Get().GetRunner().Trigger(event);
    Shots.push_back({aShooter, aSpell, acOrigin});
}

void FakeEngine::Activates(uint32_t aActor, uint32_t aObject)
{
    ActivateEvent event;
    event.ObjectFormId = aObject;
    event.ActivatorFormId = aActor;
    event.ObjectParentCellFormId = Actors.contains(aObject) ? Actors.at(aObject).Cell : References.at(aObject).Cell;
    World::Get().GetRunner().Trigger(event);
    Activations.emplace_back(aObject, aActor);
    ToggleDoor(aObject);
}

// The alias clear hooks report their quest after the engine removes the fill, without a stage/flag event.
void FakeEngine::ClearReferenceAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Aliases)
        if (fill.AliasId == aAlias)
        {
            fill = QuestAliasFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

void FakeEngine::ClearLocationAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Locations)
        if (fill.AliasId == aAlias)
        {
            fill = QuestLocationFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

// StatsMenu::ModifyPerkPoints writes directly; its hook marks both default globals after either signed delta.
void FakeEngine::ModifyBeastPerks(bool aVampire, int32_t aDelta)
{
    Globals.at(BeastPerkGlobals[aVampire ? 1 : 0]) += static_cast<float>(aDelta);
    GlobalWrites.insert(BeastPerkGlobals.begin(), BeastPerkGlobals.end());
}

float FakeEngine::HeadingOf(const glm::quat& acRotation) noexcept
{
    const glm::vec3 cForward = acRotation * glm::vec3(0.f, 1.f, 0.f);
    const float cHeading = std::atan2(cForward.x, cForward.y);
    return cHeading < 0.f ? cHeading + 6.28318531f : cHeading;
}

void FakeEngine::Knock(const BodyKey& acKey, const glm::vec3& acVelocity, const glm::vec3& acSpin)
{
    Body& body = Bodies.at(acKey);
    if (body.RagdollOf && Actors.contains(body.RagdollOf) && !IsActorDead(body.RagdollOf))
        Actors.at(body.RagdollOf).KnockedDown = true;
    body.Flight.Resting = false;
    body.Flight.Body.LinearVelocity = acVelocity;
    body.Flight.Body.AngularVelocity = acSpin;
    body.Awake = true;
    body.Tick = 0;
}

std::optional<BodyPose> FakeEngine::ReadPose(const BodyKey& acKey) noexcept
{
    const auto it = acKey.Actor ? Bodies.end() : Bodies.find(acKey);
    if (it == Bodies.end())
        return std::nullopt;
    return BodyPose{.Key = acKey, .Position = it->second.Flight.Body.Position, .Rotation = it->second.Flight.Body.Rotation};
}

bool FakeEngine::Push(const BodyPush& acPush) noexcept
{
    const auto it = Bodies.find(acPush.Key);
    if (it == Bodies.end() || it->second.Held)
        return false;
    Knock(acPush.Key, it->second.Flight.Body.LinearVelocity + acPush.Impulse, it->second.Flight.Body.AngularVelocity);
    return true;
}

bool FakeEngine::Drive(const BodyFrame& acFrame, uint32_t aCopyFormId) noexcept
{
    const auto it = Bodies.find(acFrame.Key);
    // An actor's body needs its copy here (BodyBridge::Find): none yet, or gone, is not loaded here.
    if (it == Bodies.end() || (acFrame.Key.Actor && !Actors.contains(aCopyFormId)))
        return false;
    // A living copy's ragdoll body is its animation's: knocked down with its owner's, else left until its own death
    // (BodyBridge::Find).
    if (acFrame.Key.Actor && !IsActorDead(aCopyFormId))
    {
        auto& copy = Actors.at(aCopyFormId);
        if (!acFrame.Knocked || !copy.KnockedDown)
        {
            if (acFrame.Knocked && !copy.KnockIn)
                copy.KnockIn = 2;
            return true;
        }
    }
    if (it->second.NeedsKnock && !Actors.at(aCopyFormId).KnockedDown)
        return true;
    if (acFrame.Key.Actor)
    {
        m_pendingRagdolls.Put(aCopyFormId, acFrame, false);
        return true;
    }
    Body& body = it->second;
    body.Held = true;
    body.Awake = false;
    body.Flight.Body.Position = acFrame.Position;
    body.Flight.Body.Rotation = acFrame.Rotation;
    ++body.Drives;
    return true;
}

bool FakeEngine::Rest(const BodyPose& acPose, uint32_t aCopyFormId) noexcept
{
    const auto it = Bodies.find(acPose.Key);
    if (it == Bodies.end() || (acPose.Key.Actor && !Actors.contains(aCopyFormId)))
        return false;
    ++it->second.RestAttempts;
    if (it->second.RestBusy)
        return false;
    if (acPose.Key.Actor)
    {
        if (IsActorDead(aCopyFormId) || Actors.at(aCopyFormId).KnockedDown)
        {
            ++it->second.Rests;
            m_pendingRagdolls.Put(aCopyFormId, {.Key = acPose.Key, .Position = acPose.Position, .Rotation = acPose.Rotation,
                                               .Knocked = acPose.Knocked}, true);
        }
        return true;
    }
    Body& body = it->second;
    // A reference's body is simulated again at its rest; a ragdoll's stays held.
    body.Held = body.RagdollOf != 0;
    body.Flight.Body.Position = acPose.Position;
    body.Flight.Body.Rotation = acPose.Rotation;
    body.Flight.Body.LinearVelocity = {};
    body.Flight.Body.AngularVelocity = {};
    ++body.Rests;
    return true;
}

bool FakeEngine::Free(const BodyFrame& acFrame) noexcept
{
    const auto it = Bodies.find(acFrame.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    body.Held = false;
    body.Flight.Body = acFrame;
    body.Flight.Body.Key = {};
    Knock(acFrame.Key, acFrame.LinearVelocity, acFrame.AngularVelocity);
    return true;
}

HostBodies FakeEngine::TakeBodies(uint64_t aTick, const std::function<bool(const BodyKey&)>& acWhich)
{
    constexpr uint64_t cStepMs = 16; // Havok's substeps (fTimePerSubStep)
    HostBodies bodies;
    for (auto& [cKey, body] : Bodies)
    {
        if (!body.Awake || (acWhich && !acWhich(cKey)))
            continue;
        if (!body.Tick)
            body.Tick = aTick;
        for (; body.Tick + cStepMs <= aTick && !body.Flight.Resting; body.Tick += cStepMs)
            body.Flight.Step(cStepMs / 1000.f);
        BodyFrame frame = body.Flight.Body;
        frame.Key = cKey;
        // Skyrim keeps a ragdolled actor on its ragdoll's root body, turned with it (Actor's BSTransformDeltaEvent sink,
        // 37996).
        if (body.RagdollOf && cKey.Index == 0)
            if (const auto it = Actors.find(body.RagdollOf); it != Actors.end())
            {
                std::copy_n(&frame.Position.x, 3, it->second.Position);
                it->second.Rotation[2] = HeadingOf(frame.Rotation);
            }
        // A living actor's ragdoll simulated: knocked down (PhysicsBodies).
        frame.Knocked = body.RagdollOf && !IsActorDead(body.RagdollOf);
        if (body.Flight.Resting)
        {
            body.Awake = false;
            bodies.Rested.push_back({cKey, frame.Position, frame.Rotation, frame.Knocked});
        }
        else
            bodies.Awake.push_back(frame);
    }
    return bodies;
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::PreviousSave() noexcept
{
    for (auto i = Saves.rbegin(); i != Saves.rend(); ++i)
        if (!i->starts_with("SkyrimCoop"))
            if (const auto f = SavedFiles.find(*i); f != SavedFiles.end()) return f->second;
    return std::nullopt;
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::ReadSave(const std::string& acName) noexcept
{
    const auto it = SavedFiles.find(acName);
    return it != SavedFiles.end() ? std::optional(it->second) : std::nullopt;
}

bool FakeEngine::PlayerLoads(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnLoadRequested(acName))
        return false;
    const auto cFiles = ReadSave(acName);
    return cFiles && WriteAndLoad(acName, *cFiles);
}

bool FakeEngine::PlaceBesideHost() noexcept
{
    ++PlacementChecks;
    auto& player = Actors.at(kPlayer);
    const glm::vec3 cAnchor(player.Position[0], player.Position[1], player.Position[2]);
    const auto cSpot = CheckedPlacement::Beside(cAnchor, [&](const glm::vec3& acFrom, const glm::vec3& acTo) {
        if (acTo.z < acFrom.z - 200.f)
            return std::optional{CheckedPlacement::Hit{true, {acFrom.x, acFrom.y, cAnchor.z}, {0.f, 0.f, 1.f}}};
        return std::optional{CheckedPlacement::Hit{!PlacementClear, acTo}};
    });
    if (!cSpot) return false;
    std::copy_n(&cSpot->x, 3, player.Position);
    return true;
}

void FakeEngine::StartsHitVisuals(uint32_t aActor, uint32_t aEffect, float aDuration, uint32_t aCastingType)
{
    const bool cFlash = HitVisualsRules::Flash(EffectVisualFlags[aEffect], true, aDuration, aCastingType);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, cFlash ? aEffect : 0});
}

bool FakeEngine::ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    ++AppearanceAttempts;
    if (!AtMainLoop || BlockAppearance || !Actors.contains(aFormId) || !Actors.at(aFormId).Copy)
        return false;
    auto& base = Npcs.at(GetActorBaseFormId(aFormId));
    base.Looks = acAppearance;
    base.Race = RaceInLooks(acAppearance);
    base.Weight = aWeight;
    return true;
}

bool FakeEngine::OpenFollowerInventory(uint32_t aNpcFormId) noexcept
{
    if (FollowerTradeNpc || !Actors.contains(aNpcFormId))
        return false;
    FollowerTradeNpc = aNpcFormId;
    World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    return true;
}

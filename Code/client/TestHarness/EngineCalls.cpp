#include <TiltedOnlinePCH.h>
#include "EngineCalls.h"

#include <Actor.h>
#include <AI/AIProcess.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>
#include <Forms/TESForm.h>
#include <Games/Overrides.h>
#include <Games/TES.h>
#include <Interface/UI.h>
#include <Magic/ActorMagicCaster.h>
#include <Misc/BSFixedString.h>
#include <Utils.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESPackage.h>
#include <Games/Misc/SubtitleManager.h>
#include <World.h>
#include <Bridges/IFormBridge.h>
#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/GraphVariableWriters.h>

#include <algorithm>
#include <cmath>
#include <map>

// The test harness's own engine calls (scripts/coop_test.py, through the harness's commands): staging, the
// player's input, and what scenarios check. Nothing in the client uses them.
namespace TestHarness::EngineCalls
{
namespace
{
Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}

// SKSE's task interface AddUITask: runs a task where the UI processes its messages, which is where
// the console runs what is typed into it. Null without SKSE's interface.
void (*s_addUITask)(void*) = nullptr;

// SKSE's UIDelegate_v1: Run, then Dispose.
struct ConsoleTask
{
    explicit ConsoleTask(std::string aLine)
        : Line(std::move(aLine))
    {
    }
    virtual void Run()
    {
        spdlog::info("Console command (in the UI's message processing): {}", Line);
        ExecuteConsoleCommand(Line.c_str());
    }
    virtual void Dispose() { delete this; }
    virtual ~ConsoleTask() = default;
    std::string Line;
};
} // namespace

namespace
{
// Pauses the harness holds for console lines queued this frame (UI::numPausesGame, as the open console does).
uint32_t s_consolePauses = 0;
} // namespace

bool IsCellChange(std::string_view aLine) noexcept
{
    const auto cEnd = aLine.find(' ');
    std::string verb(aLine.substr(0, cEnd));
    std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return verb == "coc" || verb == "cow" || verb == "centeroncell" || verb == "centeronworld";
}

void EndConsolePauses() noexcept
{
    if (s_consolePauses == 0)
        return;
    if (UI* pUI = UI::Get())
        pUI->numPausesGame -= s_consolePauses;
    s_consolePauses = 0;
}

void SetUITaskQueue(void (*apAddUITask)(void*)) noexcept
{
    s_addUITask = apAddUITask;
}

void ExecuteConsoleCommand(const char* acpLine) noexcept
{
    using TExecuteCommand = void(const char*);
    POINTER_SKYRIMSE(TExecuteCommand, s_executeCommand, 442672); // Console::ExecuteCommand
    s_executeCommand.Get()(acpLine);
}

void RunConsoleCommand(const std::string& acLine) noexcept
{
    static const bool s_inUI = []
    {
        const char* pPhase = std::getenv("SKYRIMCOOP_CONSOLE_PHASE");
        return pPhase && std::string_view(pPhase) == "ui";
    }();
    if (s_inUI && s_addUITask)
    {
        s_addUITask(new ConsoleTask(acLine));
        return;
    }
    // A cell change runs paused, as a typed one: no combat jobs run while it unloads actors
    // (docs/re/crashes/2026-10-12-wolf-combat-coc.md). Other lines run live: an NPC's equip is lost in menu mode.
    if (IsCellChange(acLine))
    {
        if (UI* pUI = UI::Get())
        {
            ++pUI->numPausesGame;
            ++s_consolePauses;
        }
    }
    ExecuteConsoleCommand(acLine.c_str());
}

bool IsMenuOpen(const char* acpMenu) noexcept
{
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(acpMenu);
}

void SetMenuShown(const char* acpMenu, bool aShown) noexcept
{
    using TAddMessage = void(void*, const BSFixedString*, uint32_t, void*);
    POINTER_SKYRIMSE(TAddMessage, s_addMessage, 13631); // UIMessageQueue::AddMessage
    VersionDbPtr<void*> queue(400445);                   // UIMessageQueue singleton (RVA 0x20F8950)
    if (!*queue.Get())
        return;
    const BSFixedString cName(acpMenu);
    s_addMessage.Get()(*queue.Get(), &cName, aShown ? UIMessage::kShow : UIMessage::kHide, nullptr);
}

std::vector<std::string> GetOpenMenus() noexcept
{
    std::vector<std::string> names;
    if (UI* pUI = UI::Get())
        for (IMenu* pMenu : pUI->menuStack)
            if (const BSFixedString* pName = pUI->LookupMenuNameByInstance(pMenu))
                names.emplace_back(pName->AsAscii());
    return names;
}

std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pActor || !pProcessLists)
        return {};
    return pProcessLists->GetPlayingVisualFormIds(pActor->GetHandle().handle.iBits);
}

std::pair<int32_t, float> GetCombatSkillExperience() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const int32_t cSkill = PlayerCharacter::LastUsedCombatSkill;
    if (!pPlayer || cSkill == -1)
        return {-1, 0.f};
    return {cSkill, pPlayer->GetSkillExperience(Skills::GetSkillFromActorValue(cSkill))};
}

uint32_t GetEnemyHealthTarget() noexcept
{
    // HUDMenu (1.6.1170): BSTArray<HUDObject*> objects at +0x40 (data, then size at +0x50). The
    // EnemyHealth object keeps its actor's handle at +0x28 and its mode at +0x34 (0: none,
    // 1: combat target, 2: damaged; EnemyHealth 51672).
    UI* pUi = UI::Get();
    IMenu* pHud = pUi ? pUi->FindMenuByName(BSFixedString("HUD Menu")) : nullptr;
    if (!pHud)
        return 0;
    VersionDbPtr<void> enemyHealthVtable(215382);
    const auto* pBytes = reinterpret_cast<const uint8_t*>(pHud);
    auto* const* ppObjects = *reinterpret_cast<void* const* const*>(pBytes + 0x40);
    const uint32_t cCount = *reinterpret_cast<const uint32_t*>(pBytes + 0x50);
    for (uint32_t i = 0; ppObjects && i < cCount; ++i)
    {
        const auto* pObject = static_cast<const uint8_t*>(ppObjects[i]);
        if (!pObject || *reinterpret_cast<void* const*>(pObject) != enemyHealthVtable.Get())
            continue;
        if (*reinterpret_cast<const int32_t*>(pObject + 0x34) == 0)
            return 0;
        const TESObjectREFR* pTarget = TESObjectREFR::GetByHandle(*reinterpret_cast<const uint32_t*>(pObject + 0x28));
        return pTarget ? pTarget->formID : 0;
    }
    return 0;
}

void PlayerActivates(uint32_t aObjectFormId) noexcept
{
    TP_THIS_FUNCTION(TActivate, bool, TESObjectREFR, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing);
    POINTER_SKYRIMSE(TActivate, s_activate, 19796); // TESObjectREFR::ActivateRef
    if (auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId)))
        TiltedPhoques::ThisCall(s_activate.Get(), pObject, PlayerCharacter::Get(), 0, nullptr, 1, 0);
}

bool ChangeLockAsGame(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept
{
    if (!World::Get().GetFormBridge().SetObjectLock(aObjectFormId, aLocked, aLockLevel))
        return false;
    TP_THIS_FUNCTION(TLockChange, void, TESObjectREFR);
    POINTER_SKYRIMSE(TLockChange, s_lockChange, 19512); // TESObjectREFR::AddLockChange
    if (auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId)))
        TiltedPhoques::ThisCall(s_lockChange.Get(), pObject);
    return true;
}

std::vector<uint32_t> GetCellObjectIds() noexcept
{
    std::vector<uint32_t> ids;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCell() : nullptr)
        for (TESObjectREFR* pRef : pCell->GetRefsByFormTypes({FormType::Door, FormType::Container, FormType::Furniture}))
            ids.push_back(pRef->formID);
    return ids;
}

void UnequipAllUnsynced(uint32_t aFormId) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        ScopedUnequipOverride _;
        pActor->UnEquipAll();
    }
}

int32_t GetCasterState(uint32_t aFormId, uint32_t aCastingSource) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || aCastingSource >= 4 || !pActor->casters[aCastingSource])
        return -1;
    return static_cast<int32_t>(pActor->casters[aCastingSource]->eState);
}

int32_t GetVoiceLineState(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->soundCallBackSet : -2;
}

float GetVoiceTimer(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->fVoiceTimer : 0.f;
}

int32_t GetProcessLevel(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->currentProcess ? pActor->currentProcess->processLevel : -1;
}

VoiceSound GetVoiceSound(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess || !pActor->currentProcess->highProcess)
        return {};
    BSSoundHandle* pHandle = &pActor->currentProcess->highProcess->soundHandles[0];
    TP_THIS_FUNCTION(TGetDuration, uint64_t, BSSoundHandle);
    POINTER_SKYRIMSE(TGetDuration, s_getDuration, 67630); // BSSoundHandle::GetDuration (CommonLib)
    return {pHandle->soundID, pHandle->soundID == UINT32_MAX ? 0 : TiltedPhoques::ThisCall(s_getDuration.Get(), pHandle)};
}

bool IsProcessMe(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && (pActor->flags1 & Actor::kProcessMe);
}

int32_t GetPackageType(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess || !pActor->currentProcess->package)
        return -1;
    return pActor->currentProcess->package->packType;
}

Water GetWater(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    using TGetSubmergedLevel = float(const Actor*, float, TESObjectCELL*);
    POINTER_SKYRIMSE(TGetSubmergedLevel, s_getSubmergedLevel, 37448); // Actor::GetSubmergedLevel (CommonLib)
    return {pActor->parentCell ? s_getSubmergedLevel.Get()(pActor, pActor->position.z, pActor->parentCell) : 0.f,
            pActor->actorState.IsSwimming(), (pActor->flags2 & Actor::kUnderwater) != 0, pActor->fUnderWaterTimer};
}

std::string GetSubtitle(uint32_t aFormId) noexcept
{
    SubtitleManager* pManager = SubtitleManager::Get();
    if (!pManager)
        return {};
    BSScopedLock<BSRecursiveLock> _{pManager->lock};
    for (uint32_t i = 0; i < pManager->subtitles.length; ++i)
    {
        SubtitleInfo& info = pManager->subtitles.data[i];
        const TESObjectREFR* pSpeaker = TESObjectREFR::GetByHandle(info.speakerHandle);
        if (pSpeaker && pSpeaker->formID == aFormId && info.text.AsAscii())
            return info.text.AsAscii();
    }
    return {};
}

uint8_t GetSitSleepState(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->actorState.GetSitSleepState() : 0;
}

bool IsSneaking(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->actorState.IsSneaking();
}

bool GetGraphBool(uint32_t aFormId, const char* acpName) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    BSFixedString name(acpName);
    bool value = false;
    EngineVirtual(pActor->animationGraphHolder).GetVariableBool(&name, &value);
    return value;
}

std::string SaveGraphState(uint32_t aFormId, int64_t& aMicroseconds) noexcept
{
    aMicroseconds = 0;
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    TiltedPhoques::String state;
    const auto cStart = std::chrono::steady_clock::now();
    const bool cSaved = pActor->SaveAnimationGraphState(state);
    aMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cStart).count();
    return cSaved ? std::string(state.data(), state.size()) : std::string{};
}

bool LoadGraphState(uint32_t aFormId, const std::string& acState, int64_t& aMicroseconds) noexcept
{
    aMicroseconds = 0;
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    const TiltedPhoques::String cState(acState.data(), acState.size());
    const auto cStart = std::chrono::steady_clock::now();
    const bool cLoaded = pActor->LoadAnimationGraphState(cState);
    aMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cStart).count();
    return cLoaded;
}

std::vector<GraphWriter> GetGraphWriters(uint32_t aFormId) noexcept
{
    std::vector<GraphWriter> writers;
    Actor* pActor = GetActor(aFormId);
    BSAnimationGraphManager* pManager = nullptr;
    if (!pActor || !EngineVirtual(pActor->animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return writers;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        for (uint32_t i = 0; i < pManager->animationGraphs.size; ++i)
        {
            BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(i);
            if (!pGraph || !pGraph->hkxDB)
                continue;
            // The project's variables by index (BShkbHkxDB::animationVariables), as the descriptor key reads them.
            std::map<int32_t, std::string> projectNames;
            const auto& cTable = pGraph->hkxDB->animationVariables;
            for (uint32_t b = 0; cTable.buckets && b < cTable.bucketCount; ++b)
            {
                auto* pEntry = &cTable.buckets[b];
                if (!pEntry->next)
                    continue;
                for (; pEntry != cTable.end; pEntry = pEntry->next)
                    projectNames[pEntry->value] = pEntry->key.AsAscii();
            }
            for (const auto& cWriter : GraphVariableWriters::Collect(pGraph))
            {
                const auto cIt = projectNames.find(cWriter.Index);
                writers.push_back({pGraph->projectName ? pGraph->projectName : "", cWriter.Variable.c_str(),
                                   cIt != projectNames.end() ? cIt->second : std::string{}, cWriter.Behaviour.c_str(), cWriter.Node.c_str(), cWriter.Index});
            }
        }
    }
    pManager->Release();
    return writers;
}

int32_t GetCharacterState(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return -1;
    TP_THIS_FUNCTION(TGetCharacterController, uint8_t*, Actor);
    POINTER_SKYRIMSE(TGetCharacterController, s_getCharacterController, 37258); // Actor::GetCharacterController
    const uint8_t* pController = TiltedPhoques::ThisCall(s_getCharacterController.Get(), pActor);
    // bhkCharacterController::context (hkpCharacterContext) at 0x1E0, its currentState at +0x20 (CommonLibSSE-NG).
    return pController ? *reinterpret_cast<const int32_t*>(pController + 0x1E0 + 0x20) : -1;
}

bool SendPlayerButton(Button aButton, bool aDown, float aHeldSecs) noexcept
{
    // ButtonEvent (1.6.1170; CommonLibSSE-NG): InputEvent {vtable, device, eventType, next}, IDEvent
    // {userEvent, idCode}, then value and heldDownSecs; 0x30 bytes. The events live for the whole run,
    // one per button, so nothing that keeps a pointer to one from a dispatch sees freed memory.
    struct RawButtonEvent
    {
        void* pVtable;
        uint32_t Device;    // keyboard 0, mouse 1
        uint32_t EventType; // button 0
        RawButtonEvent* pNext;
        BSFixedString UserEvent;
        uint32_t IdCode;
        uint32_t Pad24;
        float Value;        // 1 while down, 0 on the release
        float HeldDownSecs; // 0 on the press
    };
    static_assert(offsetof(RawButtonEvent, UserEvent) == 0x18 && offsetof(RawButtonEvent, IdCode) == 0x20 &&
                  offsetof(RawButtonEvent, Value) == 0x28 && sizeof(RawButtonEvent) == 0x30);
    // The default mapping: the left mouse button is the right hand's, the right one the left hand's, R readies,
    // left Ctrl sneaks, W moves forward.
    struct Mapping
    {
        uint32_t UserEventOffset; // in UserEvents (singleton 402638): leftAttack 0x40, rightAttack 0x48, readyWeapon 0x68,
                                  // sneak 0x90, forward 0x08
        uint32_t Device;
        uint32_t IdCode;
    };
    constexpr Mapping cMappings[] = {{0x48, 1, 0}, {0x40, 1, 1}, {0x68, 0, 0x13}, {0x90, 0, 0x1D}, {0x08, 0, 0x11}};
    static RawButtonEvent s_events[std::size(cMappings)]{};

    VersionDbPtr<uint8_t*> userEvents(402638);
    VersionDbPtr<void*> inputDeviceManager(402776);
    const VersionDbPtr<void> buttonEventVtable(208708);
    if (!*userEvents.Get() || !*inputDeviceManager.Get())
        return false;

    const auto cIndex = static_cast<size_t>(aButton);
    const Mapping& cMapping = cMappings[cIndex];
    RawButtonEvent& event = s_events[cIndex];
    if (!event.pVtable)
    {
        event.pVtable = buttonEventVtable.GetPtr();
        event.Device = cMapping.Device;
        event.IdCode = cMapping.IdCode;
        // The interned user event string, with our own reference: handlers compare it by pointer.
        event.UserEvent.Set(*reinterpret_cast<const char* const*>(*userEvents.Get() + cMapping.UserEventOffset));
    }
    event.EventType = 0;
    event.pNext = nullptr;
    event.Value = aDown ? 1.f : 0.f;
    event.HeldDownSecs = aDown ? std::max(aHeldSecs, 0.f) : std::max(aHeldSecs, 0.01f);

    // BSTEventSource<InputEvent*>::SendEvent: PollInputDevices sends the frame's queue through it, to
    // MenuControls, PlayerControls and the rest. Our PollInputDevices hook drops device input while the
    // window is in the background; this does not go through it.
    TP_THIS_FUNCTION(TSendEvent, void, void, RawButtonEvent* const*);
    POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 68655);
    RawButtonEvent* pChain = &event;
    TiltedPhoques::ThisCall(s_sendEvent.Get(), *inputDeviceManager.Get(), &pChain);
    return true;
}

void ForcePlayerFirstPerson() noexcept
{
    VersionDbPtr<void*> camera(400802); // PlayerCamera singleton
    using TForceFirstPerson = void(void*);
    POINTER_SKYRIMSE(TForceFirstPerson, s_forceFirstPerson, 50790); // PlayerCamera::ForceFirstPerson
    if (void* pCamera = *camera.Get())
        s_forceFirstPerson(pCamera);
}

bool FacePlayerToward(uint32_t aTargetFormId) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    Actor* pTarget = GetActor(aTargetFormId);
    if (!pPlayer || !pTarget || pTarget == pPlayer)
        return false;
    // Skyrim angles: z is the heading clockwise from north (+y), x the pitch (positive looks down).
    // From the player's eyes to the target's chest.
    constexpr float cEyeHeight = 115.f;
    constexpr float cChestHeight = 85.f;
    const float cDx = pTarget->position.x - pPlayer->position.x;
    const float cDy = pTarget->position.y - pPlayer->position.y;
    const float cDz = (pPlayer->position.z + cEyeHeight) - (pTarget->position.z + cChestHeight);
    const float cHeading = std::atan2(cDx, cDy);
    const float cPitch = std::atan2(cDz, std::sqrt(cDx * cDx + cDy * cDy));
    // In first person the camera looks exactly where the player does; in third person it keeps its
    // own yaw, and aimed spells fly along the camera's crosshair.
    ForcePlayerFirstPerson();
    pPlayer->SetRotation(cPitch, 0.f, cHeading);
    return true;
}

bool PlacePlayerBefore(uint32_t aTargetFormId, float aDistance, std::optional<float> aBearingDegrees) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    Actor* pTarget = GetActor(aTargetFormId);
    if (!pPlayer || !pTarget || pTarget == pPlayer)
        return false;
    float dx = pPlayer->position.x - pTarget->position.x;
    float dy = pPlayer->position.y - pTarget->position.y;
    // A bearing (0 north, 90 east) picks the side; two players placed on one side push each other away.
    if (aBearingDegrees)
    {
        dx = std::sin(glm::radians(*aBearingDegrees));
        dy = std::cos(glm::radians(*aBearingDegrees));
    }
    const float cLength = std::sqrt(dx * dx + dy * dy);
    if (cLength < 1.f && !aBearingDegrees)
    {
        dx = 0.f;
        dy = -1.f;
    }
    else
    {
        dx /= cLength;
        dy /= cLength;
    }
    const NiPoint3 cSpot(glm::vec3(pTarget->position.x + dx * aDistance, pTarget->position.y + dy * aDistance, pTarget->position.z));
    pPlayer->SetPosition(cSpot, true);
    return FacePlayerToward(aTargetFormId);
}
} // namespace TestHarness::EngineCalls

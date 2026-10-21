#include <TiltedOnlinePCH.h>
#include "EngineCalls.h"

#include <AI/AIProcess.h>
#include <Actor.h>
#include <BSAnimationGraphManager.h>
#include <Bridges/IFormBridge.h>
#include <Camera/PlayerCamera.h>
#include <Camera/VATS.h>
#include <Components/TESContainer.h>
#include <ExtraData/ExtraOwnership.h>
#include <Forms/TESForm.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESIdleForm.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESPackage.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWorldSpace.h>
#include <Games/FormState.h>
#include <Games/Misc/SubtitleManager.h>
#include <Games/Overrides.h>
#include <Games/Skyrim/BSInput/InputEvent.h>
#include <Games/TES.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/bhkCharacterController.h>
#include <Havok/bhkCollision.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbVariableValueSet.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/GraphVariableWriters.h>
#include <Interface/CrosshairPickData.h>
#include <Interface/FxDelegateArgs.h>
#include <Interface/MenuCursor.h>
#include <Interface/Menus/HUDMenuUtils.h>
#include <Interface/Menus/MapMenu.h>
#include <Interface/UI.h>
#include <Magic/ActorMagicCaster.h>
#include <Misc/BSFixedString.h>
#include <Misc/BSScript.h>
#include <Misc/MiddleProcess.h>
#include <Misc/GameVM.h>
#include <NetImmerse/NiCamera.h>
#include <NetImmerse/NiNode.h>
#include <PlayerCharacter.h>
#include <SaveLoad.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>
#include <Utils.h>
#include <World.h>

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

bool IsKillCamPlaying() noexcept
{
    const VATS* pVATS = VATS::Get();
    return pVATS && pVATS->mode == VATS::kKillCam;
}

float GetGlobalTimeMultiplier() noexcept
{
    POINTER_SKYRIMSE(float, s_globalTimeMultiplier, 388442); // BSTimer's current multiplier
    return *s_globalTimeMultiplier.Get();
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
    // As the player's lockpick or key does: its own change (ScopedPlayerLockOverride).
    ScopedPlayerLockOverride playerLock;
    if (!World::Get().GetFormBridge().SetObjectLock(aObjectFormId, aLocked, aLockLevel))
        return false;
    TP_THIS_FUNCTION(TLockChange, void, TESObjectREFR);
    POINTER_SKYRIMSE(TLockChange, s_lockChange, 19512); // TESObjectREFR::AddLockChange
    if (auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId)))
        TiltedPhoques::ThisCall(s_lockChange.Get(), pObject);
    return true;
}

namespace
{
// Actor::StopInteractingQuick (38697): out of its furniture (bed, chair, workbench) at once, no exit animation.
void StopInteractingQuick(Actor* apActor) noexcept
{
    if (!apActor->currentProcess)
        return;
    TP_THIS_FUNCTION(TStopInteractingQuick, void, Actor, bool);
    POINTER_SKYRIMSE(TStopInteractingQuick, s_stopInteractingQuick, 38697);
    TiltedPhoques::ThisCall(s_stopInteractingQuick.Get(), apActor, true);
}
} // namespace

bool MoveActorToPlayer(uint32_t aFormId, const float acPosition[3]) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pActor || aFormId == 0x14 || !pCell)
        return false;
    StopInteractingQuick(pActor);
    NiPoint3 spot;
    spot.x = acPosition[0];
    spot.y = acPosition[1];
    spot.z = acPosition[2];
    pActor->MoveTo(pCell, spot);
    return true;
}

bool MoveActorHome(uint32_t aFormId) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor || aFormId == 0x14)
        return false;
    StopInteractingQuick(pActor);
    TP_THIS_FUNCTION(TMoveToMyEditorLocation, bool, TESObjectREFR, TESObjectCELL*, bool);
    POINTER_SKYRIMSE(TMoveToMyEditorLocation, s_moveToMyEditorLocation, 19516); // what Papyrus MoveToMyEditorLocation runs
    return TiltedPhoques::ThisCall(s_moveToMyEditorLocation.Get(), pActor, nullptr, false);
}

bool FreezeActor(uint32_t aFormId, bool aFrozen) noexcept
{
    // What freezing changed, to put back on thawing. Freezing again (once the actor has its 3D) adds the
    // controller it did not have yet.
    struct Before
    {
        bool Restrained;
        std::optional<uint32_t> ControllerFlags;
    };
    static std::unordered_map<uint32_t, Before> s_frozen;
    constexpr uint32_t cFrozen = bhkCharacterController::kNotPushable | bhkCharacterController::kNoCharacterCollisions;
    constexpr uint32_t cHits = bhkCharacterController::kRecordHits | bhkCharacterController::kHitDamage;
    TP_THIS_FUNCTION(TActorCall, void, Actor);
    TP_THIS_FUNCTION(TSetRestrained, void, Actor, bool);
    POINTER_SKYRIMSE(TSetRestrained, s_setRestrained, 37488); // Actor::SetRestrained: life state 6, movement stopped
    POINTER_SKYRIMSE(TActorCall, s_stopMovement, 37818);     // Actor::StopMovement: the mover's path

    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor || aFormId == 0x14)
        return false;
    AIProcess* pProcess = pActor->currentProcess;
    bhkCharacterController* pController = pProcess && pProcess->middleProcess ? pProcess->middleProcess->charController : nullptr;
    if (!aFrozen)
    {
        pActor->flags1 |= Actor::kProcessMe;
        const auto it = s_frozen.find(aFormId);
        if (it == s_frozen.end())
            return true;
        if (pController && it->second.ControllerFlags)
            pController->flags = (pController->flags & ~(cFrozen | cHits)) | (*it->second.ControllerFlags & (cFrozen | cHits));
        if (!it->second.Restrained)
            TiltedPhoques::ThisCall(s_setRestrained.Get(), pActor, false);
        s_frozen.erase(it);
        return true;
    }
    const bool cRestrained = ActorState::LifeStateOf(pActor->actorState.flags1) == ActorState::kLifeRestrained;
    auto [it, cFirst] = s_frozen.try_emplace(aFormId, Before{cRestrained, std::nullopt});
    // Out of any furniture first: its entry or exit animation moves the actor whatever its AI does.
    StopInteractingQuick(pActor);
    pActor->flags1 &= ~Actor::kProcessMe;
    if (pProcess)
        TiltedPhoques::ThisCall(s_stopMovement.Get(), pActor);
    if (!cRestrained)
        TiltedPhoques::ThisCall(s_setRestrained.Get(), pActor, true);
    if (pController)
    {
        if (!it->second.ControllerFlags)
            it->second.ControllerFlags = pController->flags;
        pController->flags = (pController->flags | cFrozen) & ~cHits;
        alignas(16) const float cStill[4]{};
        pController->SetLinearVelocityImpl(cStill);
    }
    return true;
}

bool SetDoorOpen(uint32_t aObjectFormId, bool aOpen, bool aSnap) noexcept
{
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    if (!pObject || !pObject->baseForm || pObject->baseForm->formType != FormType::Door)
        return false;
    using TSetOpenState = bool(TESObjectREFR*, bool, bool);
    POINTER_SKYRIMSE(TSetOpenState, s_setOpenState, 14287); // BGSOpenCloseForm::SetOpenState
    s_setOpenState.Get()(pObject, aOpen, aSnap);
    return true;
}

std::vector<CellObject> GetCellObjects() noexcept
{
    std::vector<CellObject> objects;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr)
        for (TESObjectREFR* pRef : pCell->GetRefsByFormTypes({FormType::Door, FormType::Container, FormType::Furniture}))
            objects.push_back({pRef->formID, {pRef->position.x, pRef->position.y, pRef->position.z}});
    return objects;
}

namespace
{
constexpr size_t cMaxPoseNodes = 64;

// The named nodes under the reference's 3D, breadth first, and the physics bodies on any of them.
void ReadPose(const NiAVObject& acRoot, ReferenceState& aState) noexcept
{
    std::vector<const NiAVObject*> queue{&acRoot};
    for (size_t i = 0; i < queue.size() && aState.Pose.size() < cMaxPoseNodes; ++i)
    {
        const NiAVObject* pObject = queue[i];
        // A billboard turns to this player's camera: not the world's state.
        if (Cast<NiBillboardNode>(pObject))
            continue;
        const std::string cName = pObject->name ? pObject->name : "";
        if (!cName.empty())
        {
            auto& node = aState.Pose.emplace_back(ReferenceState::Node{cName});
            for (int axis = 0; axis < 3; ++axis)
                node.Position[axis] = pObject->world.translate[axis];
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                    node.Rotate[row * 3 + column] = pObject->world.rotate[row][column];
        }
        if (const bhkNiCollisionObject* pCollision = pObject->collisionObject; pCollision && pCollision->body)
            if (const auto* pBody = Cast<bhkRigidBody>(pCollision->body) ? pCollision->body->referencedObject : nullptr)
                aState.Collision.push_back({cName, pBody->world != nullptr,
                                            {pBody->translation[0] * cHavokToGame, pBody->translation[1] * cHavokToGame,
                                             pBody->translation[2] * cHavokToGame}});
        if (const auto* pNode = Cast<NiNode>(pObject))
            for (uint16_t child = 0; child < pNode->children.length; ++child)
                if (const NiAVObject* pChild = pNode->children.data[child])
                    queue.push_back(pChild);
    }
}

// The active behavior graph's variables by name, each as its raw 4 bytes (a bool, int or float).
void ReadGraph(TESObjectREFR& aRef, ReferenceState& aState) noexcept
{
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(aRef.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        BShkbAnimationGraph* pGraph = pManager->animationGraphIndex < pManager->animationGraphs.size
                                          ? pManager->animationGraphs.Get(pManager->animationGraphIndex)
                                          : nullptr;
        const auto* pValues = pGraph && pGraph->behaviorGraph ? pGraph->behaviorGraph->animationVariables : nullptr;
        if (pValues && pValues->data)
            for (const auto& [cIndex, cName] : pManager->DumpAnimationVariables(false))
                if (cIndex < pValues->size)
                    aState.Graph.emplace_back(cName.c_str(), pValues->data[cIndex]);
    }
    pManager->Release();
}
} // namespace

std::vector<ReferenceState> GetLoadedReferences(bool aDetails) noexcept
{
    std::vector<TESObjectCELL*> cells;
    if (TES* pTes = TES::Get())
    {
        if (pTes->interiorCell)
            cells.push_back(pTes->interiorCell);
        else if (const GridCellArray* pGrid = pTes->cells; pGrid && pGrid->arr)
            for (uint32_t i = 0; i < pGrid->dimension * pGrid->dimension; ++i)
                if (TESObjectCELL* pCell = pGrid->arr[i]; pCell && pCell->IsValid())
                    cells.push_back(pCell);
    }
    std::vector<ReferenceState> references;
    for (TESObjectCELL* pCell : cells)
    {
        if (!pCell->refData.refArray)
            continue;
        for (uint32_t i = 0; i < pCell->refData.capacity; ++i)
        {
            TESObjectREFR* pRef = pCell->refData.refArray[i].Get();
            if (!pRef || !pRef->baseForm)
                continue;
            ReferenceState state{};
            state.Id = pRef->formID;
            state.Base = pRef->baseForm->formID;
            state.BaseType = static_cast<uint8_t>(pRef->baseForm->formType);
            state.Actor = Cast<Actor>(pRef) != nullptr;
            state.Cell = pCell->formID;
            state.Flags = pRef->flags;
            state.Position[0] = pRef->position.x, state.Position[1] = pRef->position.y, state.Position[2] = pRef->position.z;
            state.Rotation[0] = pRef->rotation.x, state.Rotation[1] = pRef->rotation.y, state.Rotation[2] = pRef->rotation.z;
            state.Scale = pRef->scale / 100.f;
            if (const NiAVObject* p3D = pRef->GetNiNode())
            {
                state.Has3D = true;
                state.Hidden = (p3D->flags & 1) != 0;
                for (int axis = 0; axis < 3; ++axis)
                    state.Position3D[axis] = p3D->world.translate[axis];
            }
            if (Lock* pLock = pRef->GetLock())
            {
                state.HasLock = true;
                state.Locked = pLock->IsLocked();
                state.LockLevel = pLock->lockLevel;
            }
            if (pRef->baseForm->formType == FormType::Door)
                state.OpenState = static_cast<uint8_t>(pRef->GetOpenState());
            if (const auto* pOwnership = static_cast<ExtraOwnership*>(pRef->extraData.GetByType(ExtraDataType::Ownership)); pOwnership && pOwnership->owner)
                state.Owner = pOwnership->owner->formID;
            if (!state.Actor)
            {
                state.Items = GetContainerItems(pRef->formID);
                state.Leveled = pRef->CountLeveledEntries();
            }
            if (const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get())
                for (uint32_t bit = 0; bit < 32; ++bit)
                    if (pSaveLoad->HasChange(*pRef, 1u << bit))
                        state.Changes |= 1u << bit;
            if (aDetails)
            {
                if (const NiAVObject* p3D = pRef->GetNiNode())
                    ReadPose(*p3D, state);
                ReadGraph(*pRef, state);
                if (ProcessLists* pProcessLists = ProcessLists::Get())
                    state.Effects = pProcessLists->GetPlayingVisualFormIds(pRef->GetHandle().handle.iBits);
            }
            references.push_back(std::move(state));
        }
    }
    return references;
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

namespace
{
// The script object the VM binds to that form, with a reference to release (DecRef).
BSScript::Object* FindScript(uint32_t aFormId, const char* acpScript) noexcept
{
    auto* pForm = TESForm::GetById(aFormId);
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    auto* pPolicy = pVm ? pVm->GetObjectHandlePolicy() : nullptr;
    if (!pForm || !pPolicy)
        return nullptr;
    const uint64_t cHandle = pPolicy->GetHandleForObject(static_cast<uint32_t>(pForm->formType), pForm);
    BSScript::Object* pObject = nullptr;
    return pVm->FindBoundObject(cHandle, acpScript, &pObject) ? pObject : nullptr;
}
} // namespace

std::optional<std::string> GetScriptState(uint32_t aFormId, const char* acpScript) noexcept
{
    BSScript::Object* pObject = FindScript(aFormId, acpScript);
    if (!pObject)
        return std::nullopt;
    std::string state = pObject->currentState.AsAscii() ? pObject->currentState.AsAscii() : "";
    pObject->DecRef();
    return state;
}

std::optional<ScriptValue> GetScriptVariable(uint32_t aFormId, const char* acpScript, const char* acpVariable) noexcept
{
    BSScript::Object* pObject = FindScript(aFormId, acpScript);
    if (!pObject)
        return std::nullopt;
    std::optional<ScriptValue> value;
    if (const BSScript::Variable* pVariable = pObject->GetVariable(acpVariable))
    {
        value.emplace();
        value->Type = pVariable->type;
        switch (pVariable->type)
        {
        case BSScript::Variable::kString: value->String = pVariable->data.s ? pVariable->data.s : ""; break;
        case BSScript::Variable::kInteger: value->Int = pVariable->data.i; break;
        case BSScript::Variable::kFloat: value->Float = pVariable->data.f; break;
        case BSScript::Variable::kBoolean: value->Bool = pVariable->data.b; break;
        default:
            // An object variable's type is its script's type info (not 1-15, low bit clear for a single object).
            if (pVariable->type >= BSScript::Variable::kMax && !(pVariable->type & 1) && pVariable->data.pObj)
                value->Form = BSScript::FormIdOfHandle(pVariable->data.pObj->GetHandle());
        }
    }
    pObject->DecRef();
    return value;
}

bool SetScriptVariable(uint32_t aFormId, const char* acpScript, const char* acpVariable, double aValue) noexcept
{
    BSScript::Object* pObject = FindScript(aFormId, acpScript);
    if (!pObject)
        return false;
    BSScript::Variable* pVariable = pObject->GetVariable(acpVariable);
    const auto cType = pVariable ? pVariable->type : BSScript::Variable::kEmpty;
    if (cType == BSScript::Variable::kInteger)
        pVariable->data.i = static_cast<int32_t>(aValue);
    else if (cType == BSScript::Variable::kFloat)
        pVariable->data.f = static_cast<float>(aValue);
    else if (cType == BSScript::Variable::kBoolean)
        pVariable->data.b = aValue != 0;
    pObject->DecRef();
    return cType == BSScript::Variable::kInteger || cType == BSScript::Variable::kFloat || cType == BSScript::Variable::kBoolean;
}

std::vector<ScriptStack> GetRunningScripts() noexcept
{
    std::vector<ScriptStack> stacks;
    auto* pVm = GameVM::Get() ? reinterpret_cast<BSScript::VirtualMachine*>(GameVM::Get()->virtualMachine) : nullptr;
    if (!pVm)
        return stacks;
    const auto cName = [](BSFixedString& aName) { return std::string(aName.AsAscii() ? aName.AsAscii() : ""); };
    pVm->runningStacksLock.Lock();
    for (auto& entry : pVm->allRunningStacks)
    {
        ScriptStack& stack = stacks.emplace_back();
        stack.Id = entry.key;
        for (BSScript::StackFrame* pFrame = entry.value ? entry.value->pTop : nullptr; pFrame && stack.Frames.size() < 32;
             pFrame = pFrame->pPrevious)
            if (pFrame->pFunction)
                stack.Frames.push_back(cName(pFrame->pFunction->GetObjectTypeName()) + "." + cName(pFrame->pFunction->GetName()));
    }
    pVm->runningStacksLock.Unlock();
    return stacks;
}

std::optional<bool> IsReferenceEnabled(uint32_t aFormId) noexcept
{
    const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    return pObject ? std::optional<bool>(!pObject->IsDisabled()) : std::nullopt;
}

std::optional<int64_t> GetItemCount(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(aItemFormId));
    if (!pObject)
        return std::nullopt;
    return pObject->GetContainerCount(pItem);
}

std::optional<std::vector<std::pair<uint32_t, int32_t>>> GetContainerItems(uint32_t aObjectFormId) noexcept
{
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    if (!pObject || !pObject->GetContainer())
        return std::nullopt;
    std::vector<std::pair<uint32_t, int32_t>> items;
    for (const auto& [pItem, cCount] : pObject->GetContainerItems())
        items.emplace_back(pItem->formID, cCount);
    return items;
}

std::optional<float> GetGlobalValue(uint32_t aGlobalId) noexcept
{
    const auto* pGlobal = Cast<TESGlobal>(TESForm::GetById(aGlobalId));
    if (!pGlobal)
        return std::nullopt;
    return pGlobal->f;
}

float GetRawDaysPassed() noexcept
{
    const TimeData* pTime = TimeData::Get();
    return pTime ? pTime->rawDaysPassed : 0.f;
}

std::optional<QuestState> GetQuestState(uint32_t aQuestId) noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(aQuestId));
    if (!pQuest)
        return std::nullopt;
    QuestState state{pQuest->flags, pQuest->currentStage, pQuest->alreadyRun};
    for (TESQuest::Stage* pStage : pQuest->stages)
        if (pStage)
            state.Stages.emplace_back(pStage->stageIndex, pStage->IsDone());
    for (TESQuest::Objective* pObjective : pQuest->objectives)
        if (pObjective)
            state.Objectives.emplace_back(pObjective->index, pObjective->state);
    for (BGSBaseAlias* pAlias : pQuest->aliases)
    {
        if (!pAlias)
            continue;
        auto* pRef = pQuest->GetAliasedRef(pAlias->aliasId);
        state.Aliases.push_back({pAlias->aliasId, pAlias->name.AsAscii() ? pAlias->name.AsAscii() : "",
                                 pRef ? pRef->formID : 0, pRef && pRef->baseForm ? pRef->baseForm->formID : 0});
    }
    return state;
}

uint32_t GetSelectedPower(uint32_t aFormId) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor && pActor->equippedShout ? pActor->equippedShout->formID : 0;
}

std::optional<SavedForm> SaveFormState(uint32_t aFormId, bool aBaseForm, std::optional<uint32_t> aChangeFlags) noexcept
{
    TESForm* pForm = TESForm::GetById(aFormId);
    if (aBaseForm)
    {
        auto* pReference = Cast<TESObjectREFR>(pForm);
        pForm = pReference ? pReference->baseForm : nullptr;
    }
    if (!pForm)
        return std::nullopt;
    const uint32_t cFlags = aChangeFlags.value_or(pForm->GetChangeFlags());
    String data;
    if (!FormState::Save(*pForm, cFlags, data))
        return std::nullopt;
    return SavedForm{pForm->formID, cFlags, std::string(data.data(), data.size())};
}

bool LoadFormState(uint32_t aFormId, uint32_t aChangeFlags, const std::string& acData) noexcept
{
    TESForm* pForm = TESForm::GetById(aFormId);
    return pForm && FormState::Load(*pForm, aChangeFlags, String(acData.data(), acData.size()));
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

bool IsInKillMove(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->IsInKillMove();
}

KillMoveCheck CheckKillMove(uint32_t aAttackerFormId, uint32_t aVictimFormId) noexcept
{
    Actor* pAttacker = GetActor(aAttackerFormId);
    Actor* pVictim = GetActor(aVictimFormId);
    if (!pAttacker || !pVictim)
        return {};
    using TActorCheck = bool(Actor*, Actor*);
    using TOwnCheck = bool(Actor*);
    POINTER_SKYRIMSE(TActorCheck, s_shouldAttackKill, 38559);
    POINTER_SKYRIMSE(TActorCheck, s_killable, 37229);
    POINTER_SKYRIMSE(TOwnCheck, s_engagingInCombat, 38563);
    using TGetAttackReach = float(Actor*);
    using TPickAttackTarget = Actor*(Actor*, float, void*);
    POINTER_SKYRIMSE(TGetAttackReach, s_getAttackReach, 38538);
    POINTER_SKYRIMSE(TPickAttackTarget, s_pickAttackTarget, 47272);
    uint32_t crosshairActor = 0;
    if (const CrosshairPickData* pPick = CrosshairPickData::Get())
        if (TESObjectREFR* pRef = TESObjectREFR::GetByHandle(pPick->targetActorHandle))
            crosshairActor = pRef->formID;
    const Actor* pTarget = s_pickAttackTarget.Get()(pAttacker, s_getAttackReach.Get()(pAttacker), nullptr);
    return {s_shouldAttackKill.Get()(pAttacker, pVictim),
            s_killable.Get()(pVictim, pAttacker),
            s_engagingInCombat.Get()(pAttacker),
            IsInKillMove(aAttackerFormId),
            crosshairActor,
            pTarget ? pTarget->formID : 0};
}

std::vector<IdleCondition> CheckIdleConditions(uint32_t aIdleFormId, uint32_t aSubjectFormId,
                                               uint32_t aTargetFormId) noexcept
{
    std::vector<IdleCondition> results;
    ConditionCheckParams params;
    params.actionRef = Cast<TESObjectREFR>(TESForm::GetById(aSubjectFormId));
    params.targetRef = Cast<TESObjectREFR>(TESForm::GetById(aTargetFormId));
    if (!params.actionRef)
        return results;
    for (const TESIdleForm* pIdle = Cast<TESIdleForm>(TESForm::GetById(aIdleFormId)); pIdle; pIdle = pIdle->parentIdle)
        for (const TESConditionItem* pItem = pIdle->conditions.pHead; pItem; pItem = pItem->next)
        {
            const float cComparison = !pItem->flags.global       ? pItem->comparison.value
                                      : pItem->comparison.global ? pItem->comparison.global->f
                                                                 : 0.f;
            results.push_back({pIdle->formID, pItem->function, pItem->flags.opCode, pItem->flags.isOr != 0,
                               static_cast<uint8_t>(pItem->runOn), cComparison, pItem->IsTrue(params)});
        }
    return results;
}

std::vector<IdleNode> CheckIdleTree(uint32_t aIdleFormId, uint32_t aSubjectFormId, uint32_t aTargetFormId) noexcept
{
    std::vector<IdleNode> idles;
    auto* pSubject = Cast<TESObjectREFR>(TESForm::GetById(aSubjectFormId));
    auto* pTarget = Cast<TESObjectREFR>(TESForm::GetById(aTargetFormId));
    if (!pSubject)
        return idles;
    for (const TESIdleForm* pIdle = Cast<TESIdleForm>(TESForm::GetById(aIdleFormId)); pIdle; pIdle = pIdle->parentIdle)
        idles.push_back({pIdle->formID, pIdle->animEventName.data ? pIdle->animEventName.data : "",
                         pIdle->conditions.IsTrue(pSubject, pTarget)});
    return idles;
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

std::optional<CameraView> GetCameraView(uint32_t aTargetFormId) noexcept
{
    PlayerCamera* pCamera = PlayerCamera::Get();
    NiCamera* pNiCamera = NiCamera::WorldRoot();
    if (!pCamera || !pNiCamera)
        return std::nullopt;

    CameraView view;
    view.State = pCamera->state ? pCamera->state->id : 0;
    const auto& cWorld = pNiCamera->world;
    view.Position[0] = cWorld.translate.x;
    view.Position[1] = cWorld.translate.y;
    view.Position[2] = cWorld.translate.z;
    // A NiCamera looks along its local x axis: the first column of its world rotation.
    const glm::vec3 cForward(cWorld.rotate[0][0], cWorld.rotate[1][0], cWorld.rotate[2][0]);
    view.Yaw = glm::degrees(std::atan2(cForward.x, cForward.y));
    view.Pitch = glm::degrees(std::asin(std::clamp(-cForward.z, -1.f, 1.f)));
    if (const CrosshairPickData* pPick = CrosshairPickData::Get())
        if (TESObjectREFR* pRef = TESObjectREFR::GetByHandle(pPick->targetHandle))
            view.Crosshair = pRef->formID;

    if (Actor* pTarget = aTargetFormId ? GetActor(aTargetFormId) : nullptr)
    {
        // The head, as FacePlayerToward aims (eye height).
        constexpr float cHeadHeight = 115.f;
        const NiPoint3 cHead(glm::vec3(pTarget->position.x, pTarget->position.y, pTarget->position.z + cHeadHeight));
        NiPoint3 screen;
        view.TargetOnScreen = HUDMenuUtils::WorldPtToScreenPt3(cHead, screen) && screen.z > 0.f;
        view.TargetScreen[0] = screen.x;
        view.TargetScreen[1] = screen.y;
        const glm::vec3 cToHead = glm::normalize(glm::vec3(cHead) - glm::vec3(cWorld.translate));
        view.TargetOffCenter = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(cForward), cToHead), -1.f, 1.f)));
    }
    return view;
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
    // left Ctrl sneaks, W moves forward, Z shouts.
    struct Mapping
    {
        uint32_t UserEventOffset; // in UserEvents (singleton 402638): leftAttack 0x40, rightAttack 0x48, readyWeapon 0x68,
                                  // sneak 0x90, forward 0x08, shout 0x98
        uint32_t Device;
        uint32_t IdCode;
    };
    constexpr Mapping cMappings[] = {{0x48, 1, 0}, {0x40, 1, 1}, {0x68, 0, 0x13}, {0x90, 0, 0x1D}, {0x08, 0, 0x11}, {0x98, 0, 0x2C}};
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

std::optional<MapMarker> GetPlayerMapMarker() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectREFR* pMarker = pPlayer && pPlayer->playerMapMarker ? TESObjectREFR::GetByHandle(pPlayer->playerMapMarker) : nullptr;
    if (!pMarker)
        return std::nullopt;
    // 40535 puts it in the worldspace's persistent cell, or in the interior cell.
    const TESWorldSpace* pWorld = pMarker->GetWorldSpace();
    const uint32_t cWorldOrCell = pWorld ? pWorld->formID : (pMarker->parentCell ? pMarker->parentCell->formID : 0);
    return MapMarker{pMarker->position.x, pMarker->position.y, pMarker->position.z, cWorldOrCell};
}

bool IsMapReady() noexcept
{
    static const BSFixedString s_mapMenu("MapMenu");
    UI* pUI = UI::Get();
    const auto* pMap = pUI && pUI->GetMenuOpen(s_mapMenu) ? static_cast<const MapMenu*>(pUI->FindMenuByName(s_mapMenu)) : nullptr;
    return pMap && pMap->controlsReady;
}

std::optional<double> GetWaitMenuHours() noexcept
{
    static const BSFixedString s_sleepWaitMenu("Sleep/Wait Menu");
    UI* pUI = UI::Get();
    IMenu* pMenu = pUI && pUI->GetMenuOpen(s_sleepWaitMenu) ? pUI->FindMenuByName(s_sleepWaitMenu) : nullptr;
    GFxValue value{};
    if (!pMenu || !pMenu->uiMovie || !pMenu->uiMovie->GetVariable(&value, "_root.SleepWaitMenu_mc.HoursSlider.value") ||
        (value.type & GFxValue::kTypeMask) != GFxValue::kNumber)
        return std::nullopt;
    return value.value.number;
}

std::optional<double> GetMenuVariable(const char* acpMenu, const char* acpPath) noexcept
{
    const BSFixedString cName(acpMenu);
    UI* pUI = UI::Get();
    IMenu* pMenu = pUI && pUI->GetMenuOpen(cName) ? pUI->FindMenuByName(cName) : nullptr;
    GFxValue value{};
    if (!pMenu || !pMenu->uiMovie || !pMenu->uiMovie->GetVariable(&value, acpPath))
        return std::nullopt;
    switch (value.type & GFxValue::kTypeMask)
    {
    case GFxValue::kBoolean: return value.value.boolean ? 1.0 : 0.0;
    case GFxValue::kNumber: return value.value.number;
    default: return std::nullopt;
    }
}

void ClearPlayerMapMarker() noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RemoveWaypoint();
}

namespace
{
// Gives the event the user event it maps to in the open menu or gameplay, as the poll does
// (ControlMap::MapUserEvents 68542; the map's P is PlacePlayerMarker), then sends it.
bool MapAndSend(InputEvent& aEvent, BSFixedString& aUserEvent) noexcept
{
    VersionDbPtr<void*> inputDeviceManager(402776);
    VersionDbPtr<void*> controlMap(400863); // ControlMap
    if (!*inputDeviceManager.Get() || !*controlMap.Get())
        return false;

    aUserEvent.Release();
    TP_THIS_FUNCTION(TMapUserEvents, void, void, InputEvent*);
    POINTER_SKYRIMSE(TMapUserEvents, s_mapUserEvents, 68542); // ControlMap::MapUserEvents
    TiltedPhoques::ThisCall(s_mapUserEvents.Get(), *controlMap.Get(), &aEvent);

    TP_THIS_FUNCTION(TSendEvent, void, void, InputEvent* const*);
    POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 68655); // BSTEventSource<InputEvent*>::SendEvent
    InputEvent* pChain = &aEvent;
    TiltedPhoques::ThisCall(s_sendEvent.Get(), *inputDeviceManager.Get(), &pChain);
    return true;
}

bool SendButton(InputEvent::Device aDevice, uint32_t aIdCode, bool aDown) noexcept
{
    // One event per button for the whole run (sinks may keep a pointer to one), as SendPlayerButton.
    static Map<uint64_t, ButtonEvent> s_events;
    const VersionDbPtr<void> buttonEventVtable(208708);
    ButtonEvent& event = s_events[(static_cast<uint64_t>(aDevice) << 32) | aIdCode];
    event.header.pVtable = buttonEventVtable.GetPtr();
    event.header.device = aDevice;
    event.header.eventType = InputEvent::kButton;
    event.header.pNext = nullptr;
    event.idCode = aIdCode;
    event.value = aDown ? 1.f : 0.f;
    event.heldDownSecs = aDown ? 0.f : 0.05f;
    return MapAndSend(event.header, event.userEvent);
}
} // namespace

bool SendKey(uint32_t aScanCode, bool aDown) noexcept
{
    return SendButton(InputEvent::kKeyboard, aScanCode, aDown);
}

bool SendMouseButton(uint32_t aButton, bool aDown) noexcept
{
    return SendButton(InputEvent::kMouse, aButton, aDown);
}

bool SendMouseMove(int32_t aDeltaX, int32_t aDeltaY) noexcept
{
    static MouseMoveEvent s_event{};
    const VersionDbPtr<void> mouseMoveEventVtable(236703);
    s_event.header.pVtable = mouseMoveEventVtable.GetPtr();
    s_event.header.device = InputEvent::kMouse;
    s_event.header.eventType = InputEvent::kMouseMove;
    s_event.header.pNext = nullptr;
    s_event.idCode = 0xA;
    s_event.mouseInputX = aDeltaX;
    s_event.mouseInputY = aDeltaY;
    return MapAndSend(s_event.header, s_event.userEvent);
}

std::optional<std::pair<float, float>> GetMenuCursor() noexcept
{
    static const BSFixedString s_cursorMenu("Cursor Menu");
    UI* pUI = UI::Get();
    const MenuCursor* pCursor = MenuCursor::Get();
    if (!pUI || !pCursor || !pUI->GetMenuOpen(s_cursorMenu))
        return std::nullopt;
    return std::pair{pCursor->cursorPosX, pCursor->cursorPosY};
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
    auto* pTarget = Cast<TESObjectREFR>(TESForm::GetById(aTargetFormId));
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
    auto* pTarget = Cast<TESObjectREFR>(TESForm::GetById(aTargetFormId));
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

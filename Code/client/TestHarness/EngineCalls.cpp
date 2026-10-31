#include "EngineCalls.h"
#include <Havok/Raycast.h>

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Forms/TESNPC.h>
#include <Games/ActorPools.h>
#include <BSAnimationGraphManager.h>
#include <Bridges/ContainerItems.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/LooseItems.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Camera/PlayerCamera.h>
#include <Camera/VATS.h>
#include <Effects/ReferenceEffect.h>
#include <Components/TESContainer.h>
#include <ExtraData/ExtraOwnership.h>
#include <ExtraData/ExtraLeveledCreature.h>
#include <Forms/TESForm.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESIdleForm.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESFaction.h>
#include <Forms/TESPackage.h>
#include <Forms/BGSLocation.h>
#include <Forms/TESQuest.h>
#include <Forms/TESQuest.h>
#include <Forms/TESTopic.h>
#include <Forms/TESTopicInfo.h>
#include <Misc/JudgedFor.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Misc/MenuTopicManager.h>
#include <Games/FormState.h>
#include <Games/Misc/SubtitleManager.h>
#include <Games/Skyrim/Events/EventDispatcher.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <Games/Skyrim/BSInput/InputEvent.h>
#include <Games/TES.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/bhkCharacterController.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <Interface/FxDelegateArgs.h>
#include <Interface/GFxMovie.h>
#include <Interface/IMenu.h>
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
#include <Papyrus/GlobalCall.h>
#include <Papyrus/NativeReach.h>
#include <AI/Movement/PlayerControls.h>
#include <Games/TES.h>
#include <NetImmerse/NiCamera.h>
#include <NetImmerse/NiNode.h>
#include <FormIds.h>
#include <PlayerCharacter.h>
#include <SaveLoad.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>
#include <Utils.h>
#include <World.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <RTTI.h>
#include <VersionDb.h>

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

std::optional<uint32_t> GetMenuFlags(const char* acpMenu) noexcept
{
    UI* pUI = UI::Get();
    const auto* cpMenu = pUI && pUI->GetMenuOpen(acpMenu) ? pUI->FindMenuByName(acpMenu) : nullptr;
    return cpMenu ? std::optional(cpMenu->uiMenuFlags) : std::nullopt;
}

std::optional<bool> IsFirstPersonModelHidden() noexcept
{
    const auto* cpPlayer = PlayerCharacter::Get();
    return cpPlayer && cpPlayer->firstPerson3D ? std::optional((cpPlayer->firstPerson3D->flags & 1) != 0) : std::nullopt;
}

std::optional<bool> IsLocalMapShown() noexcept
{
    UI* pUI = UI::Get();
    const auto* cpMap = pUI && pUI->GetMenuOpen("MapMenu") ? static_cast<const MapMenu*>(pUI->FindMenuByName("MapMenu")) : nullptr;
    return cpMap ? std::optional(cpMap->localMapShown) : std::nullopt;
}

std::optional<bool> IsLocalMapReady() noexcept
{
    UI* pUI = UI::Get();
    const auto* cpMap = pUI && pUI->GetMenuOpen("MapMenu") ? static_cast<const MapMenu*>(pUI->FindMenuByName("MapMenu")) : nullptr;
    return cpMap ? std::optional(cpMap->localMapControlsReady) : std::nullopt;
}

std::vector<ReferenceEffectReadout> GetReferenceEffects(uint32_t aFormId) noexcept
{
    auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pRef || !pProcessLists)
        return {};
    const uint32_t cHandle = pRef->GetHandle().handle.iBits;
    const auto cFormOf = [](uint32_t aHandle) -> uint32_t {
        const TESObjectREFR* pOther = aHandle ? TESObjectREFR::GetByHandle(aHandle) : nullptr;
        return pOther ? pOther->formID : 0;
    };
    std::vector<ReferenceEffectReadout> result;
    BSScopedLock<BSRecursiveLock> _(pProcessLists->magicEffectsLock);
    for (uint32_t i = 0; pProcessLists->magicEffects.data && i < pProcessLists->magicEffects.length; ++i)
    {
        const auto* pEffect = Cast<ReferenceEffect>(pProcessLists->magicEffects[i]);
        if (!pEffect || (pEffect->hTarget != cHandle && pEffect->hAimTarget != cHandle))
            continue;
        ReferenceEffectReadout readout{.Target = cFormOf(pEffect->hTarget), .AimTarget = cFormOf(pEffect->hAimTarget),
                                       .Finished = pEffect->bFinished,
                                       .AtCamera = pEffect->pController && pEffect->pController->EffectAttachesToCamera()};
        if (const auto* pShader = Cast<ShaderReferenceEffect>(pProcessLists->magicEffects[i]))
        {
            readout.Shader = true;
            readout.Form = pShader->pEffectShader ? pShader->pEffectShader->formID : 0;
        }
        else if (const auto* pModel = Cast<ModelReferenceEffect>(pProcessLists->magicEffects[i]))
        {
            readout.Form = pModel->pArtObject ? pModel->pArtObject->formID : 0;
            readout.Attached = pModel->uiFlags & 1;
            if (const NiAVObject* p3D = pModel->pArtObject3D)
            {
                readout.Loaded = true;
                readout.InScene = p3D->parent != nullptr;
                readout.Hidden = p3D->flags & 1;
                readout.Position = p3D->world.translate;
            }
        }
        result.push_back(readout);
    }
    return result;
}

void ChooseDifficulty(uint32_t aDifficulty) noexcept
{
    using TSetDifficulty = void(PlayerCharacter*, int32_t);
    POINTER_SKYRIMSE(TSetDifficulty, s_setDifficulty, 40563);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || aDifficulty > 5)
        return;
    *Settings::GetDifficulty() = static_cast<int32_t>(aDifficulty);
    s_setDifficulty(pPlayer, static_cast<int32_t>(aDifficulty));
}

void PlayerActivates(uint32_t aObjectFormId) noexcept
{
    using TActivate = bool(TESObjectREFR*, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing);
    POINTER_SKYRIMSE(TActivate, s_activate, 19796); // TESObjectREFR::ActivateRef
    if (auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId)))
        (s_activate.Get())(pObject, PlayerCharacter::Get(), 0, nullptr, 1, 0);
}

bool ChangeLockAsGame(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept
{
    // As the player's lockpick or key does: its own change (ScopedPlayerLockOverride).
    ScopedPlayerLockOverride playerLock;
    if (!World::Get().GetFormBridge().SetObjectLock(aObjectFormId, aLocked, aLockLevel))
        return false;
    using TLockChange = void(TESObjectREFR*);
    POINTER_SKYRIMSE(TLockChange, s_lockChange, 19512); // TESObjectREFR::AddLockChange
    if (auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId)))
        (s_lockChange.Get())(pObject);
    return true;
}

namespace
{
// Actor::StopInteractingQuick (38697): out of its furniture (bed, chair, workbench) at once, no exit animation.
void StopInteractingQuick(Actor* apActor) noexcept
{
    if (!apActor->currentProcess)
        return;
    using TStopInteractingQuick = void(Actor*, bool);
    POINTER_SKYRIMSE(TStopInteractingQuick, s_stopInteractingQuick, 38697);
    (s_stopInteractingQuick.Get())(apActor, true);
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
    using TMoveToMyEditorLocation = bool(TESObjectREFR*, TESObjectCELL*, bool);
    POINTER_SKYRIMSE(TMoveToMyEditorLocation, s_moveToMyEditorLocation, 19516); // what Papyrus MoveToMyEditorLocation runs
    return (s_moveToMyEditorLocation.Get())(pActor, nullptr, false);
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
    using TActorCall = void(Actor*);
    using TSetRestrained = void(Actor*, bool);
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
            (s_setRestrained.Get())(pActor, false);
        s_frozen.erase(it);
        return true;
    }
    const bool cRestrained = ActorState::LifeStateOf(pActor->actorState.flags1) == ActorState::kLifeRestrained;
    auto [it, cFirst] = s_frozen.try_emplace(aFormId, Before{cRestrained, std::nullopt});
    // Out of any furniture first: its entry or exit animation moves the actor whatever its AI does.
    StopInteractingQuick(pActor);
    pActor->flags1 &= ~Actor::kProcessMe;
    if (pProcess)
        (s_stopMovement.Get())(pActor);
    if (!cRestrained)
        (s_setRestrained.Get())(pActor, true);
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
            {
                node.Position[axis] = pObject->world.translate[axis];
                node.LocalPosition[axis] = pObject->local.translate[axis];
            }
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                {
                    node.Rotate[row * 3 + column] = pObject->world.rotate[row][column];
                    node.LocalRotate[row * 3 + column] = pObject->local.rotate[row][column];
                }
        }
        if (const bhkNiCollisionObject* pCollision = pObject->collisionObject; pCollision && pCollision->body)
            if (const auto* pBody = Cast<bhkRigidBody>(pCollision->body) ? pCollision->body->referencedObject : nullptr)
            {
                auto& body = aState.Collision.emplace_back(ReferenceState::Body{cName, pBody->world != nullptr,
                                                           {pBody->translation[0] * cHavokToGame, pBody->translation[1] * cHavokToGame,
                                                            pBody->translation[2] * cHavokToGame}});
                body.Motion = pBody->motionType;
                body.Flags = pCollision->flags;
                body.Offset = Cast<bhkRigidBodyT>(pCollision->body) != nullptr;
                if (BodyRead read; ReadBody(*pCollision->body, read))
                {
                    body.Awake = read.Active;
                    body.Rotation[0] = read.Rotation.x;
                    body.Rotation[1] = read.Rotation.y;
                    body.Rotation[2] = read.Rotation.z;
                    body.Rotation[3] = read.Rotation.w;
                    for (int axis = 0; axis < 3; ++axis)
                        body.Velocity[axis] = read.LinearVelocity[axis];
                }
            }
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
                state.Flags3D = p3D->flags;
                for (int axis = 0; axis < 3; ++axis)
                    state.Position3D[axis] = p3D->world.translate[axis];
                if (const NiNode* pParent = p3D->parent)
                {
                    state.Branch = pParent->name ? pParent->name : "";
                    state.BranchIndex = pParent->parent ? static_cast<int32_t>(pParent->parentIndex) : -1;
                }
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
                state.Stored = pRef->HasStoredChanges();
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

bool SetScriptState(uint32_t aFormId, const char* acpScript, const char* acpState) noexcept
{
    BSScript::Object* pObject = FindScript(aFormId, acpScript);
    if (!pObject)
        return false;
    pObject->currentState.Set(acpState);
    pObject->DecRef();
    return true;
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

std::optional<std::vector<GlobalNative>> GetGlobalNatives(const char* acpClass) noexcept
{
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    BSScript::ObjectTypeInfo* pType = nullptr;
    if (!pVm || !pVm->GetScriptObjectType1(BSFixedString(acpClass), &pType) || !pType)
        return std::nullopt;
    std::vector<GlobalNative> natives;
    for (BSScript::IFunction* pFunction : pType->GetGlobalFunctions())
        if (pFunction && pFunction->GetIsNative())
            natives.push_back({pFunction->GetName().AsAscii(), NativeReach::ReachesThisPlayer(pFunction)});
    _InterlockedDecrement(reinterpret_cast<volatile long*>(pType)); // the lookup's reference (refCount at 00)
    return natives;
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

bool SetScriptVariableForm(uint32_t aFormId, const char* acpScript, const char* acpVariable, uint32_t aValueFormId) noexcept
{
    TESForm* pValue = TESForm::GetById(aValueFormId);
    BSScript::Object* pObject = pValue ? FindScript(aFormId, acpScript) : nullptr;
    if (!pObject)
        return false;
    BSScript::Variable* pVariable = pObject->GetVariable(acpVariable);
    if (pVariable)
        pVariable->SetForm(pValue, GameVM::Get()->virtualMachine);
    pObject->DecRef();
    return pVariable && pVariable->GetObject();
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

std::optional<std::vector<Inventory::Entry>> GetItemStacks(uint32_t aObjectFormId) noexcept
{
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    if (!pObject)
        return std::nullopt;
    if (auto* pActor = Cast<Actor>(pObject))
        return pActor->GetActorInventory().Entries;
    std::vector<Inventory::Entry> stacks;
    if (!pObject->GetContainer())
    {
        if (auto item = ReadLooseItem(*pObject))
            stacks.push_back(std::move(*item));
        return stacks;
    }
    for (const auto& [pItem, cCount] : pObject->GetContainerItems())
        std::ranges::move(ReadContainerStacks(*pObject, *pItem), std::back_inserter(stacks));
    return stacks;
}

bool GiveItem(const Inventory::Entry& acItem) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || !Cast<TESBoundObject>(TESForm::GetById(ToGame(acItem.BaseId))))
        return false;
    pPlayer->AddOrRemoveItem(acItem);
    return true;
}

namespace
{
// The player's stack of the item the inventory menus name: its first with item extra data, or (aPlain) none, the
// plain ones; false when it holds no such stack.
bool FindPlayerStack(TESBoundObject& aItem, bool aPlain, ExtraDataList*& apStack) noexcept
{
    apStack = nullptr;
    if (aPlain)
        return true;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto* pChanges = pPlayer ? pPlayer->FindContainerChanges() : nullptr;
    if (!pChanges || !pChanges->entries)
        return false;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form == &aItem && pEntry->dataList)
            for (ExtraDataList* pList : *pEntry->dataList)
            {
                Inventory::Entry stack;
                if (!pList)
                    continue;
                TESObjectREFR::GetItemFromExtraData(stack, pList);
                stack.ExtraWorn = stack.ExtraWornLeft = stack.IsQuestItem = false;
                if (stack.ContainsExtraData())
                {
                    apStack = pList;
                    return true;
                }
            }
    return false;
}
} // namespace

std::optional<uint32_t> PlayerDrops(uint32_t aItemFormId, int32_t aCount, bool aPlain) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(aItemFormId));
    ExtraDataList* pStack = nullptr;
    if (!pPlayer || !pItem || !FindPlayerStack(*pItem, aPlain, pStack))
        return std::nullopt;
    // Its address, which our hook patched: the drop as the menu makes it.
    using TDropObject = void*(PlayerCharacter*, uint32_t* apResult, TESBoundObject*, ExtraDataList*, int32_t, const NiPoint3*, const NiPoint3*);
    POINTER_SKYRIMSE(TDropObject, s_dropObject, 40454);
    uint32_t handle = 0;
    (s_dropObject.Get())(pPlayer, &handle, pItem, pStack, aCount, nullptr, nullptr);
    const TESObjectREFR* pDropped = TESObjectREFR::GetByHandle(handle);
    if (!pDropped)
        return std::nullopt;
    return pDropped->formID;
}

bool PlayerStores(uint32_t aItemFormId, int32_t aCount, uint32_t aContainerFormId, bool aPlain) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(aItemFormId));
    auto* pContainer = aContainerFormId ? Cast<TESObjectREFR>(TESForm::GetById(aContainerFormId)) : nullptr;
    ExtraDataList* pStack = nullptr;
    if (!pPlayer || !pItem || (aContainerFormId && (!pContainer || !pContainer->GetContainer())) || !FindPlayerStack(*pItem, aPlain, pStack))
        return false;
    pPlayer->RemoveItem(pItem, aCount, pContainer ? ITEM_REMOVE_REASON::kStoreInContainer : ITEM_REMOVE_REASON::kRemove, pStack, pContainer);
    return true;
}

std::optional<std::vector<std::pair<int32_t, std::vector<uint32_t>>>> GetStackExtraTypes(uint32_t aHolder, uint32_t aItem) noexcept
{
    auto* pHolder = Cast<TESObjectREFR>(TESForm::GetById(aHolder));
    auto* pItem = TESForm::GetById(aItem);
    const auto* pChanges = pHolder ? pHolder->GetContainerChanges() : nullptr;
    if (!pChanges || !pItem || !pChanges->entries)
        return std::nullopt;
    std::vector<std::pair<int32_t, std::vector<uint32_t>>> stacks;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form == pItem && pEntry->dataList)
            for (ExtraDataList* pList : *pEntry->dataList)
            {
                if (!pList)
                    continue;
                std::vector<uint32_t> types;
                for (const BSExtraData* pData = pList->data; pData; pData = pData->next)
                    types.push_back(static_cast<uint32_t>(pData->GetType()));
                stacks.emplace_back(pList->GetCount(), std::move(types));
            }
    return stacks;
}

bool PlayerImproves(uint32_t aItemFormId, float aHealth, uint32_t aEnchantment, uint16_t aCharge) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(aItemFormId));
    auto* pEnchantment = aEnchantment ? Cast<EnchantmentItem>(TESForm::GetById(aEnchantment)) : nullptr;
    ExtraDataList* pStack = nullptr;
    auto* pChanges = pPlayer ? pPlayer->GetContainerChanges() : nullptr;
    if (!pChanges || !pItem || (aEnchantment && !pEnchantment) || !FindPlayerStack(*pItem, false, pStack) || !pStack)
        return false;
    // Their addresses, which our hooks patched: the change as the smithing and enchanting tables make it.
    using TTemper = void*(ExtraContainerChanges::Data*, TESBoundObject*, ExtraDataList*, float);
    using TEnchantObject = ExtraDataList*(ExtraContainerChanges::Data*, TESBoundObject*, ExtraDataList*, EnchantmentItem*, uint16_t);
    POINTER_SKYRIMSE(TTemper, s_temper, 16145);
    POINTER_SKYRIMSE(TEnchantObject, s_enchantObject, 16146);
    if (aHealth > 0.f)
        s_temper.Get()(pChanges, pItem, pStack, aHealth);
    if (pEnchantment)
    {
        FindPlayerStack(*pItem, false, pStack);
        s_enchantObject.Get()(pChanges, pItem, pStack, pEnchantment, aCharge);
    }
    return true;
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
        const BGSLocation* pLocation = pQuest->GetRefAlias(pAlias->aliasId) ? nullptr : pQuest->GetAliasedLocation(pAlias->aliasId);
        state.Aliases.push_back({pAlias->aliasId, pAlias->name.AsAscii() ? pAlias->name.AsAscii() : "",
                                 pRef ? pRef->formID : 0, pRef && pRef->baseForm ? pRef->baseForm->formID : 0,
                                 pLocation ? pLocation->formID : 0});
    }
    return state;
}

bool AllowQuestRerun(uint32_t aQuestId) noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(aQuestId));
    if (!pQuest)
        return false;
    pQuest->alreadyRun = false;
    return true;
}

bool ForceQuestAlias(uint32_t aQuestId, uint32_t aAliasId, uint32_t aRefId, uint32_t aParentId, int16_t aGridX, int16_t aGridY) noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(aQuestId));
    if (!pQuest || !pQuest->GetRefAlias(aAliasId))
        return false;
    TESObjectREFR* pRef = TESObjectREFR::LoadFromPlugin(aRefId, aParentId, aGridX, aGridY, pQuest);
    if (!pRef)
        return false;
    pQuest->ForceRefIntoAlias(aAliasId, pRef);
    return true;
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
    std::string data;
    if (!FormState::Save(*pForm, cFlags, data))
        return std::nullopt;
    return SavedForm{pForm->formID, cFlags, std::string(data.data(), data.size())};
}

bool LoadFormState(uint32_t aFormId, uint32_t aChangeFlags, const std::string& acData) noexcept
{
    TESForm* pForm = TESForm::GetById(aFormId);
    return pForm && FormState::Load(*pForm, aChangeFlags, std::string(acData.data(), acData.size()));
}

bool PutPackage(uint32_t aFormId, uint32_t aPackageId) noexcept
{
    using TInitFromPackage = void(ActorPackage*, TESPackage*, TESObjectREFR* apTarget, Actor*);
    POINTER_SKYRIMSE(TInitFromPackage, s_initFromPackage, 38959);
    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    auto* pPackage = Cast<TESPackage>(TESForm::GetById(aPackageId));
    if (!pActor || !pActor->currentProcess || !pPackage)
        return false;
    s_initFromPackage(&pActor->currentProcess->currentPackage, pPackage, nullptr, pActor);
    // BSTEventSource<TESPackageEvent>::SendEvent (39769), as the package senders (39434-39436) call it.
    using TSendPackageEvent = void(EventDispatcher<TESPackageEvent>*, const TESPackageEvent*);
    POINTER_SKYRIMSE(TSendPackageEvent, s_sendPackageEvent, 39769);
    if (auto* pEvents = EventDispatcherManager::Get())
    {
        const TESPackageEvent cEvent{pActor, pPackage->formID, TESPackageEvent::kStart};
        s_sendPackageEvent(&pEvents->packageEvent, &cEvent);
    }
    return true;
}

std::optional<bool> IsCurrentPackage(uint32_t aFormId, uint32_t aPackageId) noexcept
{
    // GameFunc::native::GetIsCurrentPackage (subject, the package parameter, unused, the result).
    using TCondition = bool(TESObjectREFR*, TESForm*, void*, double*);
    POINTER_SKYRIMSE(TCondition, s_getIsCurrentPackage, 21529);
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    auto* pPackage = Cast<TESPackage>(TESForm::GetById(aPackageId));
    if (!pActor || !pPackage)
        return std::nullopt;
    double result = 0.0;
    s_getIsCurrentPackage(pActor, pPackage, nullptr, &result);
    return result == 1.0;
}

std::optional<int32_t> GetFactionRank(uint32_t aFormId, uint32_t aFactionId) noexcept
{
    // GameFunc::native::GetFactionRank (subject, the faction parameter, unused, the result).
    using TCondition = bool(TESObjectREFR*, TESForm*, void*, double*);
    POINTER_SKYRIMSE(TCondition, s_getFactionRank, 21493);
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    auto* pFaction = Cast<TESFaction>(TESForm::GetById(aFactionId));
    if (!pActor || !pFaction)
        return std::nullopt;
    double result = -1.0;
    s_getFactionRank(pActor, pFaction, nullptr, &result);
    return static_cast<int32_t>(result);
}

std::optional<int32_t> GetRelationshipRank(uint32_t aActor, uint32_t aOther) noexcept
{
    // GameFunc::native::GetRelationshipRank (subject, the other actor, unused, the result).
    using TCondition = bool(TESObjectREFR*, TESForm*, void*, double*);
    POINTER_SKYRIMSE(TCondition, s_getRelationshipRank, 21679);
    auto* pActor = Cast<Actor>(TESForm::GetById(aActor));
    auto* pOther = Cast<Actor>(TESForm::GetById(aOther));
    if (!pActor || !pOther)
        return std::nullopt;
    double result = 0.0;
    s_getRelationshipRank(pActor, pOther, nullptr, &result);
    return static_cast<int32_t>(result);
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
    auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    if (!pRef)
        return {};
    auto* pActor = Cast<Actor>(pRef);
    std::string state;
    const auto cStart = std::chrono::steady_clock::now();
    const bool cSaved = pActor ? pActor->SaveAnimationGraphState(state) : pRef->SaveGraphs(state);
    aMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cStart).count();
    return cSaved ? std::string(state.data(), state.size()) : std::string{};
}

bool LoadGraphState(uint32_t aFormId, const std::string& acState, int64_t& aMicroseconds) noexcept
{
    aMicroseconds = 0;
    auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    if (!pRef)
        return false;
    auto* pActor = Cast<Actor>(pRef);
    const std::string cState(acState.data(), acState.size());
    const auto cStart = std::chrono::steady_clock::now();
    const bool cLoaded = pActor ? pActor->LoadAnimationGraphState(cState) : pRef->LoadGraphs(cState);
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

DialogueState GetDialogue() noexcept
{
    DialogueState state;
    MenuTopicManager* pManager = MenuTopicManager::Get();
    if (!pManager)
        return state;
    state.MenuOpen = pManager->menuOpen;
    state.SkipHead = pManager->skipHeadResponse;
    state.CurrentInfo = pManager->currentTopicInfo ? pManager->currentTopicInfo->formID : 0;
    state.LastInfo = pManager->lastTopicInfo ? pManager->lastTopicInfo->formID : 0;
    if (TESObjectREFR* pSpeaker = TESObjectREFR::GetByHandle(pManager->speaker))
        state.Speaker = pSpeaker->formID;
    for (const MenuTopicManager::Dialogue* pDialogue : pManager->Options())
    {
        DialogueOption& option = state.Options.emplace_back();
        option.Text = pDialogue->topicText.AsAscii() ? pDialogue->topicText.AsAscii() : "";
        option.Topic = pDialogue->parentTopic ? pDialogue->parentTopic->formID : 0;
        option.Info = pDialogue->parentTopicInfo ? pDialogue->parentTopicInfo->formID : 0;
        option.Quest = pDialogue->parentQuest ? pDialogue->parentQuest->formID : 0;
    }
    UI* pUI = UI::Get();
    IMenu* pMenu = pUI && pUI->GetMenuOpen("Dialogue Menu") ? pUI->FindMenuByName("Dialogue Menu") : nullptr;
    if (pMenu && pMenu->uiMovie)
    {
        const auto cRead = [pMenu](const char* acpPath) -> int32_t {
            GFxValue value{};
            if (!pMenu->uiMovie->GetVariable(&value, acpPath))
                return -1;
            const uint32_t cType = value.type & GFxValue::kTypeMask;
            return cType == GFxValue::kBoolean ? int32_t(value.value.boolean)
                   : cType == GFxValue::kNumber ? int32_t(value.value.number) : -1;
        };
        state.AllowProgress = cRead("_root.DialogueMenu_mc.bAllowProgress");
        state.SwfState = cRead("_root.DialogueMenu_mc.eMenuState");
        state.Selected = cRead("_root.DialogueMenu_mc.TopicList.selectedIndex");
        const int32_t cCursor = cRead("_root.DialogueMenu_mc.TopicList.selectedEntry.topicIndex");
        if (cCursor >= 0 && static_cast<size_t>(cCursor) < state.Options.size())
        {
            state.CursorTopic = state.Options[cCursor].Topic;
            state.CursorIndex = cCursor;
        }
    }
    return state;
}

bool PickDialogue(uint32_t aIndex) noexcept
{
    MenuTopicManager* pManager = MenuTopicManager::Get();
    if (!pManager || !TESObjectREFR::GetByHandle(pManager->speaker))
        return false;
    pManager->Pick(aIndex);
    return true;
}

bool ForceGreet(uint32_t aActorFormId) noexcept
{
    using TSetDialogueWithPlayer = bool(Actor*, bool aStart, bool aForceGreet, TESTopicInfo* apTopic);
    POINTER_SKYRIMSE(TSetDialogueWithPlayer, s_setDialogueWithPlayer, 37200);
    Actor* pActor = Cast<Actor>(TESForm::GetById(aActorFormId));
    return pActor && s_setDialogueWithPlayer.Get()(pActor, true, true, nullptr);
}

std::optional<uint32_t> GetJudgedLine(uint32_t aTopic, uint32_t aSpeaker, uint32_t aPlayer) noexcept
{
    auto* pTopic = Cast<TESTopic>(TESForm::GetById(aTopic));
    auto* pSpeaker = Cast<TESObjectREFR>(TESForm::GetById(aSpeaker));
    auto* pPlayer = Cast<TESObjectREFR>(TESForm::GetById(aPlayer));
    if (!pTopic || !pSpeaker || !pPlayer)
        return std::nullopt;
    const JudgedFor::Scope cJudged(pPlayer == PlayerCharacter::Get() ? nullptr : pPlayer);
    const TESTopicInfo* pLine = pTopic->LineFor(pSpeaker, pPlayer);
    return pLine ? pLine->formID : 0;
}

std::vector<IdleCondition> CheckInfoConditions(uint32_t aInfo, uint32_t aSpeaker, uint32_t aPlayer) noexcept
{
    std::vector<IdleCondition> results;
    const auto* pInfo = Cast<TESTopicInfo>(TESForm::GetById(aInfo));
    ConditionCheckParams params;
    params.actionRef = Cast<TESObjectREFR>(TESForm::GetById(aSpeaker));
    params.targetRef = Cast<TESObjectREFR>(TESForm::GetById(aPlayer));
    if (!pInfo || !params.actionRef || !params.targetRef || !pInfo->objConditions)
        return results;
    params.quest = pInfo->parentTopic ? pInfo->parentTopic->ownerQuest : nullptr;
    const JudgedFor::Scope cJudged(params.targetRef == PlayerCharacter::Get() ? nullptr : params.targetRef);
    for (const TESConditionItem* pItem = static_cast<const TESCondition*>(pInfo->objConditions)->pHead; pItem; pItem = pItem->next)
    {
        const float cComparison = !pItem->flags.global       ? pItem->comparison.value
                                  : pItem->comparison.global ? pItem->comparison.global->f
                                                             : 0.f;
        results.push_back({aInfo, pItem->function, pItem->flags.opCode, pItem->flags.isOr != 0, static_cast<uint8_t>(pItem->runOn),
                           cComparison, pItem->IsTrue(params)});
    }
    return results;
}

std::optional<InfoResponses> GetInfoResponses(uint32_t aInfo) noexcept
{
    // A response (TESTopicInfo::ResponseData): its text at 28 (a BSFixedString), the next one at 40.
    struct Response
    {
        uint8_t unk00[0x28];
        const char* text;
        uint8_t unk30[0x40 - 0x30];
        Response* next;
    };
    static_assert(offsetof(Response, text) == 0x28 && offsetof(Response, next) == 0x40);
    using TGetResponses = Response**(TESTopicInfo*, Response**);
    using TFreeResponses = void(Response**);
    POINTER_SKYRIMSE(TGetResponses, s_getResponses, 25626);
    POINTER_SKYRIMSE(TFreeResponses, s_freeResponses, 25587);
    auto* pInfo = Cast<TESTopicInfo>(TESForm::GetById(aInfo));
    if (!pInfo)
        return std::nullopt;
    const TESTopicInfo* pData = pInfo;
    while (pData->dataInfo)
        pData = pData->dataInfo;
    InfoResponses responses{pData->formID, pData->fileOffset, {}};
    Response* pList = nullptr;
    s_getResponses.Get()(pInfo, &pList);
    for (const Response* pResponse = pList; pResponse; pResponse = pResponse->next)
        responses.Texts.emplace_back(pResponse->text ? pResponse->text : "");
    s_freeResponses.Get()(&pList);
    return responses;
}

std::optional<bool> SetInfoSaid(uint32_t aInfo, std::optional<bool> aSaid) noexcept
{
    auto* pInfo = Cast<TESTopicInfo>(TESForm::GetById(aInfo));
    if (!pInfo)
        return std::nullopt;
    if (aSaid)
        pInfo->saidOnce = *aSaid;
    return pInfo->saidOnce;
}

float GetActorValue(uint32_t aActor, uint32_t aIndex) noexcept
{
    Actor* pActor = GetActor(aActor);
    return pActor ? pActor->GetActorValue(aIndex) : 0.f;
}

float GetPlayerActorValue(uint32_t aIndex) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer ? pPlayer->GetActorValue(aIndex) : 0.f;
}

float GetPlayerSkillExperience(uint32_t aIndex) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer ? pPlayer->GetSkillExperience(Skills::GetSkillFromActorValue(static_cast<int32_t>(aIndex))) : 0.f;
}

uint32_t GetOwner(uint32_t aRefFormId) noexcept
{
    TESForm* pForm = TESForm::GetById(aRefFormId);
    ExtraDataList* pExtra = nullptr;
    if (auto* pRef = Cast<TESObjectREFR>(pForm))
        pExtra = &pRef->extraData;
    else if (auto* pCell = Cast<TESObjectCELL>(pForm))
        pExtra = &pCell->extraData;
    const auto* pOwnership = pExtra ? static_cast<ExtraOwnership*>(pExtra->GetByType(ExtraDataType::Ownership)) : nullptr;
    return pOwnership && pOwnership->owner ? pOwnership->owner->formID : 0;
}

std::optional<bool> GetPlayerKnows(uint32_t aFormId) noexcept
{
    const TESForm* pForm = TESForm::GetById(aFormId);
    return pForm ? std::optional((pForm->flags & 0x40) != 0) : std::nullopt;
}

bool SetPlayerKnows(uint32_t aFormId, bool aKnows) noexcept
{
    using TSetPlayerKnows = void(TESForm*, bool);
    POINTER_SKYRIMSE(TSetPlayerKnows, s_setPlayerKnows, 14639);
    TESForm* pForm = TESForm::GetById(aFormId);
    if (pForm)
        s_setPlayerKnows(pForm, aKnows);
    return pForm != nullptr;
}

std::optional<bool> GetUnlocked(uint32_t aFormId) noexcept
{
    const TESForm* pForm = TESForm::GetById(aFormId);
    return pForm ? std::optional((pForm->flags & TESForm::UNLOCKED) != 0) : std::nullopt;
}

bool SetUnlocked(uint32_t aFormId, bool aUnlocked) noexcept
{
    using TSetUnlocked = bool(TESForm*, bool);
    POINTER_SKYRIMSE(TSetUnlocked, s_setUnlocked, 14640);
    TESForm* pForm = TESForm::GetById(aFormId);
    if (pForm)
        s_setUnlocked(pForm, aUnlocked);
    return pForm != nullptr;
}

uint32_t GetPlayerTrainings() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer ? pPlayer->skillTrainingsThisLevel : 0;
}

void SetPlayerTrainings(uint32_t aCount) noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->skillTrainingsThisLevel = aCount;
}

ScreenState GetScreenState() noexcept
{
    POINTER_SKYRIMSE(float, s_shakeStrength, 402622);
    POINTER_SKYRIMSE(float, s_shakeSeconds, 402623);
    ScreenState state;
    if (const auto* pControls = BSInputEnableManager::Get())
        state.Controls = pControls->enabledControls;
    if (auto* pTes = TES::Get())
        for (ImageSpaceModifierInstance* pInstance : pTes->activeImageSpaceModifiers)
            if (const auto* pForm = pInstance ? pInstance->AsForm() : nullptr; pForm && pForm->pImod)
                state.Imods.push_back(pForm->pImod->formID);
    state.ShakeStrength = *s_shakeStrength.Get();
    state.ShakeSeconds = *s_shakeSeconds.Get();
    return state;
}

bool QueuePapyrusCall(const std::string& acClass, const std::string& acFunction, std::optional<uint32_t> aSelf, const std::vector<LocalValue>& acArguments,
                      std::function<void(LocalValue)> aOnReturn) noexcept
{
    const LocalCall cCall{.Class = acClass, .Function = acFunction, .Self = aSelf ? LocalValue{LocalForm{*aSelf}} : LocalValue{}, .Arguments = acArguments};
    return GlobalCall::QueueCall(cCall, std::move(aOnReturn));
}

bool IsInRagdollState(uint32_t aFormId) noexcept
{
    using TIsInRagdollState = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInRagdollState, s_isInRagdollState, 37491);
    const Actor* pActor = GetActor(aFormId);
    return pActor && s_isInRagdollState.Get()(pActor);
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
    const glm::vec3 cForward = glm::normalize(glm::vec3(cWorld.rotate[0][0], cWorld.rotate[1][0], cWorld.rotate[2][0]));
    for (int axis = 0; axis < 3; ++axis)
        view.Forward[axis] = cForward[axis];
    view.Yaw = glm::degrees(std::atan2(cForward.x, cForward.y));
    view.Pitch = glm::degrees(std::asin(std::clamp(-cForward.z, -1.f, 1.f)));
    if (const CrosshairPickData* pPick = CrosshairPickData::Get())
        if (TESObjectREFR* pRef = TESObjectREFR::GetByHandle(pPick->targetHandle))
            view.Crosshair = pRef->formID;

    const glm::vec3 cCamera(cWorld.translate.x, cWorld.translate.y, cWorld.translate.z);
    const glm::vec3 cAhead = cCamera + cForward * 4096.f;
    const float cFrom[3]{cCamera.x, cCamera.y, cCamera.z};
    const float cTo[3]{cAhead.x, cAhead.y, cAhead.z};
    view.Looking = CastRay(cFrom, cTo);
    if (TESObjectREFR* pFollowed = TESObjectREFR::GetByHandle(pCamera->cameraTarget))
    {
        view.Following = pFollowed->formID;
        static const BSFixedString s_head("NPC Head [Head]");
        const glm::vec3 head = NodePosition(pFollowed, s_head)
                                   .value_or(glm::vec3(pFollowed->position.x, pFollowed->position.y, pFollowed->position.z + 115.f));
        for (int axis = 0; axis < 3; ++axis)
            view.FollowedHead[axis] = head[axis];
        // First person sits in the head: nothing to see between them.
        if (glm::distance(head, cCamera) > 20.f)
        {
            const float cHead[3]{head.x, head.y, head.z};
            view.Sight = CastRay(cHead, cFrom, 30, pFollowed->formID);
        }
    }

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
    // left Ctrl sneaks, W moves forward, Z shouts, E activates; S, A and D step back, left and right.
    struct Mapping
    {
        uint32_t UserEventOffset; // in UserEvents (singleton 402638): leftAttack 0x40, rightAttack 0x48, readyWeapon 0x68,
                                  // sneak 0x90, forward 0x08, shout 0x98, activate 0x38, back 0x10, strafeLeft 0x18,
                                  // strafeRight 0x20
        uint32_t Device;
        uint32_t IdCode;
    };
    constexpr Mapping cMappings[] = {{0x48, 1, 0}, {0x40, 1, 1}, {0x68, 0, 0x13}, {0x90, 0, 0x1D}, {0x08, 0, 0x11}, {0x98, 0, 0x2C},
                                   {0x38, 0, 0x12}, {0x10, 0, 0x1F}, {0x18, 0, 0x1E}, {0x20, 0, 0x20}};
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
    using TSendEvent = void(void*, RawButtonEvent* const*);
    POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 68655);
    RawButtonEvent* pChain = &event;
    (s_sendEvent.Get())(*inputDeviceManager.Get(), &pChain);
    return true;
}

namespace
{
std::optional<GFxValue> ReadMenuValue(const char* acpMenu, const char* acpPath) noexcept
{
    const BSFixedString cName(acpMenu);
    UI* pUI = UI::Get();
    IMenu* pMenu = pUI && pUI->GetMenuOpen(cName) ? pUI->FindMenuByName(cName) : nullptr;
    GFxValue value{};
    if (!pMenu || !pMenu->uiMovie || !pMenu->uiMovie->GetVariable(&value, acpPath))
        return std::nullopt;
    return value;
}
} // namespace

std::optional<std::string> GetMenuText(const char* acpMenu, const char* acpPath) noexcept
{
    const auto cValue = ReadMenuValue(acpMenu, acpPath);
    if (!cValue || (cValue->type & GFxValue::kTypeMask) != GFxValue::kString)
        return std::nullopt;
    const char* pText = (cValue->type & GFxValue::kManaged) ? *reinterpret_cast<const char* const*>(cValue->value.string) : cValue->value.string;
    return pText ? std::optional<std::string>(pText) : std::nullopt;
}

std::optional<double> GetMenuVariable(const char* acpMenu, const char* acpPath) noexcept
{
    const auto cRead = ReadMenuValue(acpMenu, acpPath);
    if (!cRead)
        return std::nullopt;
    const GFxValue& value = *cRead;
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
    using TMapUserEvents = void(void*, InputEvent*);
    POINTER_SKYRIMSE(TMapUserEvents, s_mapUserEvents, 68542); // ControlMap::MapUserEvents
    (s_mapUserEvents.Get())(*controlMap.Get(), &aEvent);

    using TSendEvent = void(void*, InputEvent* const*);
    POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 68655); // BSTEventSource<InputEvent*>::SendEvent
    InputEvent* pChain = &aEvent;
    (s_sendEvent.Get())(*inputDeviceManager.Get(), &pChain);
    return true;
}

bool SendButton(InputEvent::Device aDevice, uint32_t aIdCode, bool aDown) noexcept
{
    // One event per button for the whole run (sinks may keep a pointer to one), as SendPlayerButton.
    static std::unordered_map<uint64_t, ButtonEvent> s_events;
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

bool SendChar(uint32_t aCharacter) noexcept
{
    VersionDbPtr<void*> inputDeviceManager(402776);
    if (!*inputDeviceManager.Get())
        return false;
    static CharEvent s_event{};
    const VersionDbPtr<void> charEventVtable(236705);
    s_event.header.pVtable = charEventVtable.GetPtr();
    s_event.header.device = static_cast<InputEvent::Device>(5); // as CharEvent's constructor (68634)
    s_event.header.eventType = InputEvent::kChar;
    s_event.header.pNext = nullptr;
    s_event.keyCode = aCharacter;

    using TSendEvent = void(void*, InputEvent* const*);
    POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 68655); // BSTEventSource<InputEvent*>::SendEvent
    InputEvent* pChain = &s_event.header;
    (s_sendEvent.Get())(*inputDeviceManager.Get(), &pChain);
    return true;
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

std::optional<InputState> GetInputState() noexcept
{
    const auto* pMap = BSInputEnableManager::Get();
    if (!pMap)
        return std::nullopt;
    InputState state{.Controls = pMap->enabledControls, .TextEntry = pMap->textEntryCount, .IgnoreKeyboardMouse = pMap->ignoreKeyboardMouse};
    for (uint32_t i = 0; i < pMap->contextStack.length; ++i)
        state.Contexts.push_back(pMap->contextStack.data[i]);
    return state;
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
    // From the player's eyes to an actor's chest, or to an object itself.
    constexpr float cEyeHeight = 115.f;
    const float cTargetHeight = Cast<Actor>(pTarget) ? 85.f : 0.f;
    const float cDx = pTarget->position.x - pPlayer->position.x;
    const float cDy = pTarget->position.y - pPlayer->position.y;
    const float cDz = (pPlayer->position.z + cEyeHeight) - (pTarget->position.z + cTargetHeight);
    const float cHeading = std::atan2(cDx, cDy);
    const float cPitch = std::atan2(cDz, std::sqrt(cDx * cDx + cDy * cDy));
    // In first person the camera looks exactly where the player does; in third person it keeps its
    // own yaw, and aimed spells fly along the camera's crosshair.
    ForcePlayerFirstPerson();
    pPlayer->SetRotation(cPitch, 0.f, cHeading);
    return true;
}

bool PlacePlayerAt(const float acPosition[3], uint32_t aTargetFormId) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;
    pPlayer->SetPosition(NiPoint3(glm::vec3(acPosition[0], acPosition[1], acPosition[2])), true);
    return !aTargetFormId || FacePlayerToward(aTargetFormId);
}

std::optional<RayHit> CastRay(const float acFrom[3], const float acTo[3], uint32_t aLayer, uint32_t aThrough) noexcept
{
    return GameRaycast::CastRay(acFrom, acTo, aLayer, aThrough);
}

Placement PlacePlayerBefore(uint32_t aTargetFormId, float aDistance, std::optional<float> aBearingDegrees) noexcept
{
    Placement placement;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    auto* pTarget = Cast<TESObjectREFR>(TESForm::GetById(aTargetFormId));
    if (!pPlayer || !pTarget || pTarget == pPlayer)
    {
        placement.Blocked = "no such target";
        return placement;
    }
    const glm::vec3 cTarget(pTarget->position.x, pTarget->position.y, pTarget->position.z);
    // What the player looks at: an actor's chest, an object's middle.
    const glm::vec3 cAim = cTarget + glm::vec3(0.f, 0.f, Cast<Actor>(pTarget) ? 100.f : 15.f);
    float first = 180.f;
    if (aBearingDegrees)
        first = *aBearingDegrees;
    else if (const glm::vec2 cAway(pPlayer->position.x - cTarget.x, pPlayer->position.y - cTarget.y); glm::length(cAway) >= 1.f)
        first = glm::degrees(std::atan2(cAway.x, cAway.y));

    const auto cCast = [](const glm::vec3& acFrom, const glm::vec3& acTo) {
        const float cFrom[3]{acFrom.x, acFrom.y, acFrom.z};
        const float cTo[3]{acTo.x, acTo.y, acTo.z};
        return CastRay(cFrom, cTo);
    };
    const auto cWhat = [](const RayHit& acHit) {
        return acHit.Reference ? std::format("{:X} (layer {})", acHit.Reference, acHit.Layer) : std::format("layer {}", acHit.Layer);
    };
    for (int step = 0; step <= 18; ++step)
    {
        // The bearing asked first, then 20 degrees either way, then 40...
        const float cBearing = std::fmod(first + static_cast<float>((step + 1) / 2 * 20 * (step % 2 ? 1 : -1)) + 720.f, 360.f);
        const float cRadians = glm::radians(cBearing);
        const glm::vec2 cSpot(cTarget.x + std::sin(cRadians) * aDistance, cTarget.y + std::cos(cRadians) * aDistance);
        const auto cFloor = cCast({cSpot, cTarget.z + 150.f}, {cSpot, cTarget.z - 600.f});
        const auto cBlocked = [&](std::string aWhy) { placement.Blocked += std::format("{}{:.0f}: {}", placement.Blocked.empty() ? "" : "; ", cBearing, aWhy); };
        if (!cFloor)
        {
            placement.Blocked = "no physics world, or this frame's picks are spent";
            return placement;
        }
        if (!cFloor->Hit)
        {
            cBlocked("no floor");
            continue;
        }
        const glm::vec3 cFeet(cSpot, cFloor->Point[2]);
        if (const auto cHead = cCast(cFeet + glm::vec3(0.f, 0.f, 20.f), cFeet + glm::vec3(0.f, 0.f, 140.f)); cHead && cHead->Hit)
        {
            cBlocked("no headroom under " + cWhat(*cHead));
            continue;
        }
        // Room for the player's capsule: nothing within 35 units at knee height.
        std::optional<RayHit> touching;
        for (int around = 0; around < 360 && !touching; around += 45)
        {
            const glm::vec3 cKnee = cFeet + glm::vec3(0.f, 0.f, 30.f);
            const glm::vec3 cOut(std::sin(glm::radians(static_cast<float>(around))) * 35.f, std::cos(glm::radians(static_cast<float>(around))) * 35.f, 0.f);
            if (const auto cHit = cCast(cKnee, cKnee + cOut); cHit && cHit->Hit)
                touching = cHit;
        }
        if (touching)
        {
            cBlocked("no room beside " + cWhat(*touching));
            continue;
        }
        const glm::vec3 cEyes = cFeet + glm::vec3(0.f, 0.f, 100.f);
        if (const auto cSight = cCast(cEyes, cAim); cSight && cSight->Hit && cSight->Reference != aTargetFormId &&
                                                     glm::distance(glm::vec3(cSight->Point[0], cSight->Point[1], cSight->Point[2]), cAim) > 40.f)
        {
            cBlocked(std::format("{} in the way, {:.0f} from the spot", cWhat(*cSight), cSight->Distance));
            continue;
        }
        pPlayer->SetPosition(NiPoint3(cFeet + glm::vec3(0.f, 0.f, 5.f)), true);
        FacePlayerToward(aTargetFormId);
        placement.Placed = true;
        placement.Bearing = cBearing;
        for (int axis = 0; axis < 3; ++axis)
            placement.Position[axis] = cFeet[axis];
        return placement;
    }
    return placement;
}
std::optional<ActorRuntime> ReadActorRuntime(uint32_t aFormId) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor) return std::nullopt;
    ActorRuntime result;
    result.State1 = pActor->actorState.flags1;
    result.State2 = pActor->actorState.flags2;
    result.LifeState = ActorState::LifeStateOf(result.State1);
    if (const auto* pScene = pActor->GetOwnCurrentScene())
        result.NativeScene = pScene->formID;
    result.OwnerScene = pActor->GetExtension()->OwnerSceneId;
    using TGetCurrentScene = TESForm*(Actor*);
    POINTER_SKYRIMSE(TGetCurrentScene, s_getCurrentScene, 37247);
    if (const auto* pScene = s_getCurrentScene(pActor))
        result.EffectiveScene = pScene->formID;
    return result;
}

uint8_t GetReplayPhase(uint32_t aFormId) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor ? pActor->GetExtension()->Replay.Phase.load(std::memory_order_acquire) : 0;
}

std::optional<PoseVisibility> GetPoseVisibility(uint32_t aFormId) noexcept
{
    const auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
        return std::nullopt;
    // CommonLib BOOL_BITS::kWasInFrustum; HighActorCuller's low nibble is also used by Precision.
    return PoseVisibility{(pActor->flags1 & (1u << 21)) != 0, pActor->cullingState & 0xFu, pActor->iLastSeenTime};
}

NpcTemplates ReadNpcTemplates(uint32_t aFormId) noexcept
{
    NpcTemplates result;
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
        return result;
    std::vector<TESForm*> pending{pActor->baseForm};
    if (auto* pExtra = static_cast<ExtraLeveledCreature*>(pActor->extraData.GetByType(ExtraDataType::LeveledCreature)))
    {
        result.Original = pExtra->originalBase ? pExtra->originalBase->formID : 0;
        result.Selected = pExtra->templateBase ? pExtra->templateBase->formID : 0;
        pending.push_back(pExtra->originalBase);
        pending.push_back(pExtra->templateBase);
    }
    while (!pending.empty())
    {
        auto* pBase = pending.back();
        pending.pop_back();
        if (!pBase || std::ranges::any_of(result.Forms, [&](const auto& acForm) { return acForm.Id == pBase->formID; }))
            continue;
        auto* pNpc = Cast<TESNPC>(pBase);
        auto* pFace = pNpc ? pNpc->npcTemplate : nullptr; // TESNPC +1F0: faceNPC in CommonLibSSE-NG
        auto* pTemplate = pNpc ? pNpc->actorData.owner : nullptr; // TESActorBaseData +30: baseTemplateForm
        const char* cpName = pBase->GetName();
        result.Forms.push_back({pBase->formID, uint32_t(pBase->formType), cpName ? cpName : "",
                                pFace ? pFace->formID : 0, pTemplate ? pTemplate->formID : 0});
        pending.push_back(pFace);
        pending.push_back(pTemplate);
    }
    return result;
}

std::vector<ActorPoolReadout> ReadActorPools(uint32_t aFormId) noexcept
{
    std::vector<ActorPoolReadout> pools;
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
        return pools;
    using Mode = ActorValueOwner::ForceMode;
    for (const uint32_t cIndex : ActorPools::kAll)
        pools.push_back({cIndex, EngineVirtual(pActor->actorValueOwner).GetBaseValue(cIndex),
                          pActor->GetActorPermanentValue(cIndex), pActor->GetActorValueModifier(Mode::TEMPORARY, cIndex),
                          pActor->GetActorValueModifier(Mode::DAMAGE, cIndex), pActor->GetActorValue(cIndex)});
    return pools;
}

} // namespace TestHarness::EngineCalls

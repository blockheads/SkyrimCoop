#include "EngineReads.h"
#include <Havok/Raycast.h>

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Forms/TESNPC.h>
#include <BSAnimationGraphManager.h>
#include <Bridges/IFormBridge.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Camera/PlayerCamera.h>
#include <Camera/VATS.h>
#include <Effects/ReferenceEffect.h>
#include <Components/TESContainer.h>
#include <ExtraData/ExtraOwnership.h>
#include <Forms/TESForm.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESIdleForm.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESFaction.h>
#include <Forms/TESPackage.h>
#include <Forms/BGSLocation.h>
#include <Forms/TESQuest.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWorldSpace.h>
#include <Games/FormState.h>
#include <Games/Misc/SubtitleManager.h>
#include <Games/Skyrim/Events/EventDispatcher.h>
#include <Games/Overrides.h>
#include <Games/References.h>
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

namespace Diagnostics::EngineReads
{
namespace
{
Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}

enum class Visuals
{
    OnBody,   // playing on the actor's body
    AtCamera, // playing at this game's camera, which follows the actor
    Shown     // every one in the list, the finished ones fading out too
};

std::vector<uint32_t> VisualFormIds(uint32_t aFormId, Visuals aWhich) noexcept
{
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pActor || !pProcessLists)
        return {};
    const uint32_t cHandle = pActor->GetHandle().handle.iBits;
    std::vector<uint32_t> result;
    {
        BSScopedLock<BSRecursiveLock> _(pProcessLists->magicEffectsLock);
        for (uint32_t i = 0; pProcessLists->magicEffects.data && i < pProcessLists->magicEffects.length; ++i)
        {
            const auto* pEffect = Cast<ReferenceEffect>(pProcessLists->magicEffects[i]);
            if (!pEffect || pEffect->hTarget != cHandle)
                continue;
            if (aWhich != Visuals::Shown)
            {
                const bool cAtCamera = pEffect->pController && pEffect->pController->EffectAttachesToCamera();
                if (pEffect->bFinished || cAtCamera != (aWhich == Visuals::AtCamera))
                    continue;
            }
            const TESForm* pForm = nullptr;
            if (const auto* pShader = Cast<ShaderReferenceEffect>(pProcessLists->magicEffects[i]))
                pForm = pShader->pEffectShader;
            else if (const auto* pModel = Cast<ModelReferenceEffect>(pProcessLists->magicEffects[i]))
                pForm = pModel->pArtObject;
            if (pForm)
                result.push_back(pForm->formID);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace

// A node of the reference's 3D by name (NiAVObject::GetObjectByName), in world space: the 3D on screen (the
// player's first-person one in first person), or the third-person body others see.
std::optional<glm::vec3> NodePosition(TESObjectREFR* apRef, const BSFixedString& acName, bool aBody) noexcept
{
    NiAVObject* pRoot = aBody ? apRef->Get3D1(false) : apRef->GetNiNode();
    const NiAVObject* pNode = pRoot ? pRoot->GetByName(acName) : nullptr;
    if (!pNode)
        return std::nullopt;
    return glm::vec3(pNode->world.translate.x, pNode->world.translate.y, pNode->world.translate.z);
}

AiReadout GetAiReadout(uint32_t aFormId) noexcept
{
    AiReadout readout;
    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor || !pActor->currentProcess)
        return readout;
    const TESPackage* pPackage = pActor->GetCurrentPackage();
    readout.Package = pPackage ? pPackage->formID : 0;
    const ActorPackage& cRunning = pActor->currentProcess->currentPackage;
    if (pPackage && cRunning.package == pPackage)
        if (const TESObjectREFR* pTarget = TESObjectREFR::GetByHandle(cRunning.target))
            readout.PackageTarget = pTarget->formID;
    // Through the hooked function: a copy answers its owner's.
    using TGetCurrentScene = TESForm*(Actor*);
    POINTER_SKYRIMSE(TGetCurrentScene, s_getCurrentScene, 37247);
    if (const TESForm* pScene = s_getCurrentScene(pActor))
        readout.Scene = pScene->formID;
    if (const MiddleProcess* pMiddle = pActor->currentProcess->middleProcess)
        if (const TESObjectREFR* pFurniture = TESObjectREFR::GetByHandle(pMiddle->occupiedFurniture))
            readout.Furniture = pFurniture->formID;
    if (const TESObjectREFR* pFollowed = TESObjectREFR::GetByHandle(pActor->currentProcess->followTarget))
        readout.FollowTarget = pFollowed->formID;
    // GameFunc::native::GetCombatState (subject, two parameters, the result), as a dialogue line reads it.
    using TCondition = bool(TESObjectREFR*, void*, void*, double*);
    POINTER_SKYRIMSE(TCondition, s_getCombatState, 21740);
    double combat = 0.0;
    s_getCombatState(pActor, nullptr, nullptr, &combat);
    readout.Combat = static_cast<uint32_t>(combat);
    if (const TESObjectREFR* pTarget = TESObjectREFR::GetByHandle(pActor->combatHandle))
        readout.CombatTarget = pTarget->formID;
    if (const HighProcessData* pHigh = pActor->currentProcess->highProcess)
        std::ranges::copy(pHigh->GetHeadTrackTargets(), readout.HeadTrack);
    return readout;
}

std::optional<BaseLooks> GetBaseLooks(uint32_t aFormId) noexcept
{
    const auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    const auto* pNpc = pActor && pActor->baseForm ? Cast<TESNPC>(pActor->baseForm) : nullptr;
    if (!pNpc)
        return std::nullopt;
    return BaseLooks{.Female = (pNpc->actorData.flags & TESActorBaseData::IS_FEMALE) != 0, .Weight = pNpc->weight, .Level = pNpc->actorData.level};
}

std::optional<BodyPose> GetBodyPose(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return std::nullopt;
    static const BSFixedString s_pelvis("NPC Pelvis [Pelv]");
    static const BSFixedString s_head("NPC Head [Head]");
    const auto cPelvis = NodePosition(pActor, s_pelvis, true);
    const auto cHead = NodePosition(pActor, s_head, true);
    if (!cPelvis || !cHead)
        return std::nullopt;
    // Down from above its feet (a body lying on its back starts in its own collision: passed through).
    const glm::vec3 cFeet(pActor->position.x, pActor->position.y, pActor->position.z);
    const float cFrom[3]{cFeet.x, cFeet.y, cFeet.z + 50.f};
    const float cTo[3]{cFeet.x, cFeet.y, cFeet.z - 2000.f};
    const auto cFloor = GameRaycast::CastRay(cFrom, cTo, 30, aFormId);
    if (!cFloor || !cFloor->Hit)
        return std::nullopt;
    const float cFloorZ = cFloor->Point[2];
    static const BSFixedString s_skeleton("NPC Root [Root]");
    const NiAVObject* pRoot = pActor->Get3D1(false);
    const float cRootZ = pRoot ? pRoot->world.translate.z : cFeet.z;
    return BodyPose{.FloorGap = cFeet.z - cFloorZ, .Root = cRootZ - cFloorZ,
                    .Skeleton = NodePosition(pActor, s_skeleton, true).value_or(glm::vec3(cFeet.x, cFeet.y, cRootZ)).z - cFloorZ,
                    .Pelvis = cPelvis->z - cFloorZ, .Head = cHead->z - cFloorZ, .Floor = cFloor->Reference};
}

std::vector<uint32_t> GetCameraVisualFormIds(uint32_t aFormId) noexcept
{
    return VisualFormIds(aFormId, Visuals::AtCamera);
}

int32_t GetCasterState(uint32_t aFormId, uint32_t aCastingSource) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || aCastingSource >= 4 || !pActor->casters[aCastingSource])
        return -1;
    return static_cast<int32_t>(pActor->casters[aCastingSource]->eState);
}

int32_t GetCharacterState(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return -1;
    using TGetCharacterController = uint8_t*(Actor*);
    POINTER_SKYRIMSE(TGetCharacterController, s_getCharacterController, 37258); // Actor::GetCharacterController
    const uint8_t* pController = (s_getCharacterController.Get())(pActor);
    // bhkCharacterController::context (hkpCharacterContext) at 0x1E0, its currentState at +0x20 (CommonLibSSE-NG).
    return pController ? *reinterpret_cast<const int32_t*>(pController + 0x1E0 + 0x20) : -1;
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

uint32_t GetFadeState(uint32_t aFormId) noexcept
{
    using TGetFadeState = uint32_t(AIProcess*);
    POINTER_SKYRIMSE(TGetFadeState, s_getFadeState, 39451); // ActorProcess::GetFadeState
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->currentProcess ? s_getFadeState(pActor->currentProcess) : 0;
}

float GetGlobalTimeMultiplier() noexcept
{
    POINTER_SKYRIMSE(float, s_globalTimeMultiplier, 388442); // BSTimer's current multiplier
    return *s_globalTimeMultiplier.Get();
}

std::optional<float> GetGlobalValue(uint32_t aGlobalId) noexcept
{
    const auto* pGlobal = Cast<TESGlobal>(TESForm::GetById(aGlobalId));
    if (!pGlobal)
        return std::nullopt;
    return pGlobal->f;
}

uint32_t GetGrabbedObject() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const TESObjectREFR* pObject = pPlayer && pPlayer->grabbedObject ? TESObjectREFR::GetByHandle(pPlayer->grabbedObject) : nullptr;
    return pObject ? pObject->formID : 0;
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

KillState GetKillState(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess)
        return {};
    using TIsInDeferredKill = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInDeferredKill, s_isInDeferredKill, 38619);
    using TIsKillQueued = bool(const AIProcess*);
    POINTER_SKYRIMSE(TIsKillQueued, s_isKillQueued, 40011);
    return {s_isInDeferredKill(pActor), s_isKillQueued(pActor->currentProcess)};
}

uint8_t GetKnockState(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->actorState.GetKnockState() : 0;
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

uint32_t GetMenuPauseCount() noexcept
{
    const UI* cpUI = UI::Get();
    return cpUI ? cpUI->numPausesGame : 0;
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

int32_t GetPackageType(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess || !pActor->currentProcess->currentPackage.package)
        return -1;
    return pActor->currentProcess->currentPackage.package->packType;
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

std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) noexcept
{
    return VisualFormIds(aFormId, Visuals::OnBody);
}

int32_t GetProcessLevel(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->currentProcess ? pActor->currentProcess->processLevel : -1;
}

std::optional<std::vector<RagdollBody>> GetRagdoll(uint32_t aActorFormId) noexcept
{
    const auto* pActor = Cast<Actor>(TESForm::GetById(aActorFormId));
    if (!pActor)
        return std::nullopt;
    std::vector<RagdollBody> bodies;
    for (const bhkWorldObject* pBody : RagdollBodiesOf(*pActor))
    {
        auto& body = bodies.emplace_back();
        BodyRead read;
        if (!pBody || !pBody->referencedObject || !ReadBody(*pBody, read))
            continue;
        body.InWorld = pBody->referencedObject->world != nullptr;
        body.Motion = pBody->referencedObject->motionType;
        body.Awake = read.Active;
        for (int axis = 0; axis < 3; ++axis)
        {
            body.Position[axis] = read.Position[axis];
            body.Velocity[axis] = read.LinearVelocity[axis];
        }
        body.Rotation[0] = read.Rotation.x;
        body.Rotation[1] = read.Rotation.y;
        body.Rotation[2] = read.Rotation.z;
        body.Rotation[3] = read.Rotation.w;
        body.HasBone = BoneOf(*pBody, body.Bone);
    }
    return bodies;
}

float GetRawDaysPassed() noexcept
{
    const TimeData* pTime = TimeData::Get();
    return pTime ? pTime->rawDaysPassed : 0.f;
}

uint32_t GetRunningPackage(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess || !pActor->currentProcess->currentPackage.package)
        return 0;
    return pActor->currentProcess->currentPackage.package->formID;
}

uint32_t GetSelectedPower(uint32_t aFormId) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor && pActor->equippedShout ? pActor->equippedShout->formID : 0;
}

std::vector<uint32_t> GetShownVisualFormIds(uint32_t aFormId) noexcept
{
    return VisualFormIds(aFormId, Visuals::Shown);
}

uint8_t GetSitSleepState(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->actorState.GetSitSleepState() : 0;
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

uint32_t GetSubtitleOnScreen() noexcept
{
    SubtitleManager* pManager = SubtitleManager::Get();
    if (!pManager)
        return 0;
    const TESObjectREFR* pSpeaker = TESObjectREFR::GetByHandle(pManager->currentSpeakerHandle);
    return pSpeaker ? pSpeaker->formID : 0;
}

int32_t GetVoiceLineState(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->soundCallBackSet : -2;
}

VoiceSound GetVoiceSound(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess || !pActor->currentProcess->highProcess)
        return {};
    BSSoundHandle* pHandle = &pActor->currentProcess->highProcess->soundHandles[0];
    using TGetDuration = uint64_t(BSSoundHandle*);
    POINTER_SKYRIMSE(TGetDuration, s_getDuration, 67630); // BSSoundHandle::GetDuration (CommonLib)
    return {pHandle->soundID, pHandle->soundID == UINT32_MAX ? 0 : (s_getDuration.Get())(pHandle)};
}

float GetVoiceTimer(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->fVoiceTimer : 0.f;
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

bool HasMountInteraction(uint32_t aFormId) noexcept
{
    const auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor && pActor->extraData.Contains(ExtraDataType::Interaction);
}

bool IsGettingOnOrOffMount(uint32_t aFormId) noexcept
{
    const auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor && (pActor->flags2 & Actor::GETTING_ON_OFF_MOUNT);
}

bool IsInCombat(uint32_t aFormId) noexcept
{
    // The condition IsInCombat (21576: subject, two parameters, the result), which reads the actor's vfunc 0xE3.
    using TCondition = bool(TESObjectREFR*, void*, void*, double*);
    POINTER_SKYRIMSE(TCondition, s_isInCombat, 21576);
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
        return false;
    double result = 0.0;
    s_isInCombat(pActor, nullptr, nullptr, &result);
    return result == 1.0;
}

bool IsKillCamPlaying() noexcept
{
    const VATS* pVATS = VATS::Get();
    return pVATS && pVATS->mode == VATS::kKillCam;
}

bool IsMapReady() noexcept
{
    static const BSFixedString s_mapMenu("MapMenu");
    UI* pUI = UI::Get();
    const auto* pMap = pUI && pUI->GetMenuOpen(s_mapMenu) ? static_cast<const MapMenu*>(pUI->FindMenuByName(s_mapMenu)) : nullptr;
    return pMap && pMap->controlsReady;
}

bool IsMenuOpen(const char* acpMenu) noexcept
{
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(acpMenu);
}

bool IsProcessMe(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && (pActor->flags1 & Actor::kProcessMe);
}

bool IsSneaking(uint32_t aFormId) noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->actorState.IsSneaking();
}

bool IsTalking(uint32_t aFormId) noexcept
{
    using TIsTalking = bool(Actor*);
    POINTER_SKYRIMSE(TIsTalking, s_isTalking, 37266);
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor && s_isTalking(pActor);
}

bool PlayerCanRide(uint32_t aMountFormId) noexcept
{
    using TCanRide = bool(Actor*, Actor*);
    POINTER_SKYRIMSE(TCanRide, s_canRide, 38706);
    Actor* pMount = GetActor(aMountFormId);
    return pMount && PlayerCharacter::Get() && s_canRide(PlayerCharacter::Get(), pMount);
}

bool BoneOf(const bhkWorldObject& acBody, float aPosition[3]) noexcept
{
    using TFindCollidableObject = NiAVObject*(const void*);
    POINTER_SKYRIMSE(TFindCollidableObject, s_findCollidableObject, 15870);
    const NiAVObject* pNode = acBody.referencedObject ? s_findCollidableObject(acBody.referencedObject->collidable) : nullptr;
    if (!pNode)
        return false;
    aPosition[0] = pNode->world.translate.x;
    aPosition[1] = pNode->world.translate.y;
    aPosition[2] = pNode->world.translate.z;
    return true;
}
} // namespace Diagnostics::EngineReads

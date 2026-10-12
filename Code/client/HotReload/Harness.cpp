#include <TiltedOnlinePCH.h>
#include "Harness.h"

#include <Actor.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>
#include <Forms/TESForm.h>
#include <Games/Overrides.h>
#include <Games/TES.h>
#include <Interface/UI.h>
#include <Magic/ActorMagicCaster.h>
#include <Misc/BSFixedString.h>
#include <Utils.h>

#include <algorithm>
#include <cmath>

// The test harness's own engine calls (scripts/coop_test.py, through Runtime's commands): staging, the
// player's input, and what scenarios check. Nothing in the client uses them.
namespace CoopReload::Harness
{
namespace
{
Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}
} // namespace

std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pActor || !pProcessLists)
        return {};
    return pProcessLists->GetPlayingVisualFormIds(pActor->GetHandle().handle.iBits);
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
    // The default mapping: the left mouse button is the right hand's, the right one the left hand's, R readies.
    struct Mapping
    {
        uint32_t UserEventOffset; // in UserEvents (singleton 402638): leftAttack 0x40, rightAttack 0x48, readyWeapon 0x68
        uint32_t Device;
        uint32_t IdCode;
    };
    constexpr Mapping cMappings[] = {{0x48, 1, 0}, {0x40, 1, 1}, {0x68, 0, 0x13}};
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

bool PlacePlayerBefore(uint32_t aTargetFormId, float aDistance) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    Actor* pTarget = GetActor(aTargetFormId);
    if (!pPlayer || !pTarget || pTarget == pPlayer)
        return false;
    float dx = pPlayer->position.x - pTarget->position.x;
    float dy = pPlayer->position.y - pTarget->position.y;
    const float cLength = std::sqrt(dx * dx + dy * dy);
    if (cLength < 1.f)
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
} // namespace CoopReload::Harness

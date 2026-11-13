#include <Games/Skyrim/PlayerHands.h>

#include <AI/AIProcess.h>
#include <Havok/bhkCollision.h>
#include <HookSwitch.h>
#include <Misc/MiddleProcess.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <atomic>
#include <mutex>
#include <Hooks/FunctionHook.h>
#include <Hooks/Memory.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace
{
// hkpCharacterObjectInteractionEvent and Result (CommonLibSSE-NG; checked in IDA, 79133's callee reads the body at
// 0x30 and scales the result's impulse).
struct InteractionEvent
{
    float position[4];        // 00
    float normal[4];          // 10
    float objectImpulse;      // 20
    float timestep;           // 24
    float projectedVelocity;  // 28
    float objectMassInv;      // 2C
    hkpRigidBody* body;       // 30
};
static_assert(offsetof(InteractionEvent, body) == 0x30);

struct InteractionResult
{
    float objectImpulse[4];   // 00: what Havok applies to the body, Havok's units
    float impulsePosition[4]; // 10
};
static_assert(offsetof(InteractionResult, impulsePosition) == 0x10);

std::atomic<bool> s_watching;
// The player's character controller, set each update on the main thread; the callbacks run on Havok's.
std::atomic<const void*> s_playerController;
std::mutex s_pushesLock; // a leaf lock
std::vector<PlayerHands::Push> s_pushes;
std::vector<PlayerHands::Push> s_hits;
thread_local const void* s_swinger{}; // the actor whose swing 38628 runs now (38603 sends it to the main thread)

const void* PlayerController() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const AIProcess* pProcess = pPlayer ? pPlayer->currentProcess : nullptr;
    const MiddleProcess* pMiddle = pProcess ? pProcess->middleProcess : nullptr;
    return pMiddle ? pMiddle->charController : nullptr;
}

// The listener (bhkCharProxyController's hkpCharacterProxyListener) is 0x10 before its bhkCharacterController.
bool IsPlayers(const void* apListener) noexcept
{
    return s_watching.load(std::memory_order_relaxed) &&
           static_cast<const uint8_t*>(apListener) + 0x10 == s_playerController.load(std::memory_order_relaxed);
}

void Record(std::vector<PlayerHands::Push>& aInto, const hkpRigidBody& acBody, const glm::vec3& acImpulse, const glm::vec3& acPoint) noexcept
{
    const TESObjectREFR* pObject = ReferenceOf(acBody);
    if (!pObject)
        return;
    aInto.push_back({pObject->formID, &acBody, acImpulse * cHavokToGame, acPoint * cHavokToGame});
}

// bhkCharProxyController::objectInteractionCallback (79133, vfunc 5): the engine's scaling (by layer and mass) fills
// the result.
using TObjectInteraction = void(void*, void*, const InteractionEvent*, InteractionResult*);
TObjectInteraction* RealObjectInteraction = nullptr;

void HookObjectInteraction(void* apThis, void* apProxy, const InteractionEvent* apEvent, InteractionResult* apResult)
{
    RealObjectInteraction(apThis, apProxy, apEvent, apResult);
    if (!IsPlayers(apThis) || !apEvent->body)
        return;
    const glm::vec3 cImpulse(apResult->objectImpulse[0], apResult->objectImpulse[1], apResult->objectImpulse[2]);
    if (cImpulse != glm::vec3(0.f))
    {
        std::scoped_lock _(s_pushesLock);
        Record(s_pushes, *apEvent->body, cImpulse, glm::vec3(apResult->impulsePosition[0], apResult->impulsePosition[1], apResult->impulsePosition[2]));
    }
}

// Actor::CalculateCurrentHitTargetForWeaponSwing (38628): the swing's shape cast; a dynamic body it meets gets a point
// impulse at +0x6BE (docs/re/systems/physics.md, "A swing's hit").
using TWeaponSwing = void*(void*);
TWeaponSwing* RealWeaponSwing = nullptr;

void* HookWeaponSwing(void* apActor)
{
    const void* pOuter = std::exchange(s_swinger, apActor);
    void* pTarget = RealWeaponSwing(apActor);
    s_swinger = pOuter;
    return pTarget;
}

// 38628's call of hkpMotion::applyPointImpulse (vfunc 0x14), under the world's write lock.
using TApplyPointImpulse = void(hkpRigidBody::Motion*, const float*, const float*);
constexpr ptrdiff_t cSwingImpulseCall = 0x6BE;

void SwingImpulse(hkpRigidBody::Motion* apMotion, const float* apImpulse, const float* apPoint)
{
    reinterpret_cast<TApplyPointImpulse*>(apMotion->vtable[0x14])(apMotion, apImpulse, apPoint);
    if (!s_watching.load(std::memory_order_relaxed) || !s_swinger || s_swinger != PlayerCharacter::Get())
        return;
    const auto* pBody = reinterpret_cast<const hkpRigidBody*>(reinterpret_cast<const uint8_t*>(apMotion) - offsetof(hkpRigidBody, motion));
    const glm::vec3 cImpulse(apImpulse[0], apImpulse[1], apImpulse[2]);
    if (cImpulse != glm::vec3(0.f))
    {
        std::scoped_lock _(s_pushesLock);
        Record(s_hits, *pBody, cImpulse, glm::vec3(apPoint[0], apPoint[1], apPoint[2]));
    }
}

Hooks::Initializer s_playerHandsHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(79133, &RealObjectInteraction, HookObjectInteraction);
    // call qword ptr [rax+0A0h], 6 bytes: a call to ours and a nop.
    VersionDbPtr<uint8_t> swing(38628);
    uint8_t* pCall = swing.Get() + cSwingImpulseCall;
    if (pCall[0] == 0xFF && pCall[1] == 0x90 && *reinterpret_cast<const int32_t*>(pCall + 2) == 0xA0)
    {
        Hooks::PutCall(pCall, &SwingImpulse);
        Hooks::Nop(pCall + 5, 1);
        HOOK_ID(38628, &RealWeaponSwing, HookWeaponSwing);
    }
    else
        spdlog::error("PlayerHands: the weapon swing's impulse call is missing");
});
} // namespace

namespace PlayerHands
{
std::vector<Push> Take() noexcept
{
    s_playerController.store(PlayerController(), std::memory_order_relaxed);
    std::scoped_lock _(s_pushesLock);
    return std::exchange(s_pushes, {});
}

std::vector<Push> TakeHits() noexcept
{
    std::scoped_lock _(s_pushesLock);
    return std::exchange(s_hits, {});
}

void Watch(bool aOn) noexcept
{
    s_watching.store(aOn, std::memory_order_relaxed);
    if (!aOn)
    {
        std::scoped_lock _(s_pushesLock);
        s_pushes.clear();
        s_hits.clear();
    }
}
} // namespace PlayerHands

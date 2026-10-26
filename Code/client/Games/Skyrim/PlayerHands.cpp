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

void Record(const hkpRigidBody& acBody, const glm::vec3& acImpulse, const glm::vec3& acPoint) noexcept
{
    const TESObjectREFR* pObject = ReferenceOf(acBody);
    if (!pObject)
        return;
    std::scoped_lock _(s_pushesLock);
    s_pushes.push_back({pObject->formID, &acBody, acImpulse * cHavokToGame, acPoint * cHavokToGame});
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
        Record(*apEvent->body, cImpulse, glm::vec3(apResult->impulsePosition[0], apResult->impulsePosition[1], apResult->impulsePosition[2]));
}

Hooks::Initializer s_playerHandsHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(79133, &RealObjectInteraction, HookObjectInteraction);
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

void Watch(bool aOn) noexcept
{
    s_watching.store(aOn, std::memory_order_relaxed);
    if (!aOn)
    {
        std::scoped_lock _(s_pushesLock);
        s_pushes.clear();
    }
}
} // namespace PlayerHands

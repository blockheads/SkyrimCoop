#include <Papyrus/WorldEvents.h>

#include <HookSwitch.h>
#include <Misc/BSScript.h>
#include <Papyrus/GuestScripts.h>
#include <Papyrus/OwnEvents.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/PlayerNatives.h>
#include <Papyrus/SystemQuests.h>
#include <RunningGame.h>

#include <atomic>
#include <intrin.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace WorldEvents
{
namespace
{
std::atomic<bool> s_delivered{true};

constexpr uint32_t cPlayerFormId = OwnEvents::kThisPlayer;

// Whether this game's own player or UI caused the event (OwnEvents::IsOwnEvent): its scripts then run here.
bool IsOwnEvent(uint64_t aHandle, const BSFixedString& acEvent, const void* apCaller) noexcept
{
    const char* cpEvent = acEvent.AsAscii();
    const OwnEvents::SentEvent cEvent{aHandle, cpEvent ? cpEvent : "", RunningGame::IsScriptExtenderCode(apCaller)};
    // A system quest is every game's own (docs/design/system-quests.md).
    const auto cOwnQuest = [](uint32_t aQuest) { return PlayerNatives::IsOwnQuest(aQuest) || SystemQuests::Is(aQuest); };
    return OwnEvents::IsOwnEvent(cEvent, cOwnQuest, GuestScripts::RegisteredBy);
}

// Internal::VirtualMachine::SendEvent (104800, vtable slot 0x24, SKSE's QueueEvent): every event to a handle's
// scripts; RelayEvent (54033) sends a reference's event to its aliases and magic effects through it too.
using TSendEvent = void(void*, uint64_t, const BSFixedString&, void*);
TSendEvent* RealSendEvent = nullptr;

void HookSendEvent(void* apThis, uint64_t aHandle, const BSFixedString& acEvent, void* apArguments)
{
    const bool cFromNative = GuestScripts::InNative() || GuestScripts::NativeGuest();
    const void* cpCaller = _ReturnAddress();
    switch (OwnEvents::Deliver(!s_delivered.load(std::memory_order_relaxed), PartyNatives::InSession(), cFromNative,
                               [&] { return IsOwnEvent(aHandle, acEvent, cpCaller); }))
    {
    case OwnEvents::Delivery::kRun: return RealSendEvent(apThis, aHandle, acEvent, apArguments);
    case OwnEvents::Delivery::kAsThisPlayer:
    {
        // What its scripts start or register runs for this game's player too.
        GuestScripts::ScopedCause _(cPlayerFormId);
        return RealSendEvent(apThis, aHandle, acEvent, apArguments);
    }
    case OwnEvents::Delivery::kDrop: return; // a guest: the host's game runs the world's scripts
    }
}

Hooks::Initializer s_hooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(104800, &RealSendEvent, HookSendEvent);
    });
} // namespace

void SetDelivered(bool aOn) noexcept
{
    s_delivered.store(aOn, std::memory_order_relaxed);
}
} // namespace WorldEvents

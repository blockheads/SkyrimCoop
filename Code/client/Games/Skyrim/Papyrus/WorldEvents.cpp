#include <Papyrus/WorldEvents.h>

#include <HookSwitch.h>
#include <Misc/BSScript.h>

#include <algorithm>
#include <atomic>
#include <string_view>

namespace WorldEvents
{
namespace
{
std::atomic<bool> s_delivered{true};

// The events a player's action raises on the world: activating it, entering or leaving its trigger.
constexpr std::string_view cWorldEvents[] = {"OnActivate", "OnTriggerEnter", "OnTriggerLeave", "OnTrigger"};

constexpr uint32_t cPlayerFormId = 0x14;

// Internal::VirtualMachine::SendEvent (104800, vtable slot 0x24): every event to a handle's scripts; RelayEvent
// (54033) sends a reference's event to its aliases and magic effects through it too.
TP_THIS_FUNCTION(TSendEvent, void, void, uint64_t, const BSFixedString&, void*);
TSendEvent* RealSendEvent = nullptr;

void TP_MAKE_THISCALL(HookSendEvent, void, uint64_t aHandle, const BSFixedString& acEvent, void* apArguments)
{
    if (!s_delivered.load(std::memory_order_relaxed) && BSScript::FormIdOfHandle(aHandle) != cPlayerFormId &&
        std::ranges::any_of(cWorldEvents, [&acEvent](std::string_view aName) { return acEvent.Is(aName); }))
        return;
    TiltedPhoques::ThisCall(RealSendEvent, apThis, aHandle, acEvent, apArguments);
}

TiltedPhoques::Initializer s_hooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 104800);
        RealSendEvent = s_sendEvent.Get();
        TP_HOOK(&RealSendEvent, HookSendEvent);
    });
} // namespace

void SetDelivered(bool aOn) noexcept
{
    s_delivered.store(aOn, std::memory_order_relaxed);
}
} // namespace WorldEvents

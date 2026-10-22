#include <Papyrus/WorldEvents.h>

#include <HookSwitch.h>
#include <Misc/BSScript.h>
#include <Papyrus/GuestScripts.h>
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

constexpr uint32_t cPlayerFormId = 0x14;

// What a VM handle names (SkyrimScript::HandlePolicy::GetHandleForObject, 53508): a form is
// 0x0000'FFFF'<form id>, an active effect 0x0002'<unique id>'<the actor it is on>.
bool IsAboutPlayer(uint64_t aHandle) noexcept
{
    const auto cKind = static_cast<uint16_t>(aHandle >> 48);
    const auto cTag = static_cast<uint16_t>(aHandle >> 32);
    const auto cFormId = static_cast<uint32_t>(aHandle);
    const bool cForm = cKind == 0 && cTag == 0xFFFF;
    const bool cEffect = cKind == 2;
    return (cForm || cEffect) && cFormId == cPlayerFormId;
}

// Whether this game's own player or UI caused the event: its scripts then run here.
bool IsOwnEvent(uint64_t aHandle, const BSFixedString& acEvent, const void* apCaller) noexcept
{
    // SKSE's events (input, menus, mod events) and the game's load are this game's own.
    if (RunningGame::IsScriptExtenderCode(apCaller) || acEvent.Is("OnPlayerLoadGame") || IsAboutPlayer(aHandle))
        return true;
    // A timer or registration its own scripts set.
    const char* cpEvent = acEvent.AsAscii();
    return cpEvent && GuestScripts::RegisteredBy(aHandle, cpEvent) == cPlayerFormId;
}

// Internal::VirtualMachine::SendEvent (104800, vtable slot 0x24, SKSE's QueueEvent): every event to a handle's
// scripts; RelayEvent (54033) sends a reference's event to its aliases and magic effects through it too.
using TSendEvent = void(void*, uint64_t, const BSFixedString&, void*);
TSendEvent* RealSendEvent = nullptr;

void HookSendEvent(void* apThis, uint64_t aHandle, const BSFixedString& acEvent, void* apArguments)
{
    if (s_delivered.load(std::memory_order_relaxed))
        return RealSendEvent(apThis, aHandle, acEvent, apArguments);
    // A guest: the host's game runs the world's scripts.
    if (!IsOwnEvent(aHandle, acEvent, _ReturnAddress()))
        return;
    // What its scripts start or register runs here too.
    GuestScripts::ScopedCause _(cPlayerFormId);
    RealSendEvent(apThis, aHandle, acEvent, apArguments);
}

Hooks::Initializer s_hooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSendEvent, s_sendEvent, 104800);
        RealSendEvent = s_sendEvent.Get();
        HOOK(&RealSendEvent, HookSendEvent);
    });
} // namespace

void SetDelivered(bool aOn) noexcept
{
    s_delivered.store(aOn, std::memory_order_relaxed);
}
} // namespace WorldEvents

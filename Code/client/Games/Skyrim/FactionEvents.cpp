#include <FactionEvents.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <Events/FactionsChangeEvent.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <World.h>

#include <atomic>

namespace FactionEvents
{
namespace
{
std::atomic<uint32_t> s_users{0};

void Raise(const Actor* apActor) noexcept
{
    if (apActor && s_users.load(std::memory_order_relaxed))
        World::Get().GetRunner().Trigger(FactionsChangeEvent{apActor->formID});
}

// Each writes the base's faction list (a unique actor) or the reference's ExtraFactionChanges, which GetFactions reads.
using TAddToFaction = uint64_t(Actor*, TESFaction*, int8_t);
using TRemoveFromFaction = uint64_t(Actor*, TESFaction*);
using TSetFactionRank = void(Actor*, TESFaction*, int8_t);
using TRemoveFromAllFactions = uint64_t(void* apVm, uint32_t aStackId, Actor* apActor);
TAddToFaction* RealAddToFaction = nullptr;
TRemoveFromFaction* RealRemoveFromFaction = nullptr;
TSetFactionRank* RealSetFactionRank = nullptr;
TRemoveFromFaction* RealSetCrimeFaction = nullptr;
TRemoveFromAllFactions* RealRemoveFromAllFactions = nullptr;

// Actor::AddToFaction (37686): the console's AddToFaction.
uint64_t HookAddToFaction(Actor* apThis, TESFaction* apFaction, int8_t aRank)
{
    const uint64_t cResult = RealAddToFaction(apThis, apFaction, aRank);
    Raise(apThis);
    return cResult;
}

// Actor::RemoveFromFaction (37688): the console's and Papyrus's.
uint64_t HookRemoveFromFaction(Actor* apThis, TESFaction* apFaction)
{
    const uint64_t cResult = RealRemoveFromFaction(apThis, apFaction);
    Raise(apThis);
    return cResult;
}

// 37677 (ours: Actor::SetFactionRank): Papyrus SetFactionRank and AddToFaction, ModFactionRank, alias factions.
void HookSetFactionRank(Actor* apThis, TESFaction* apFaction, int8_t aRank)
{
    RealSetFactionRank(apThis, apFaction, aRank);
    Raise(apThis);
}

// Character::SetCrimeFaction (37381).
uint64_t HookSetCrimeFaction(Actor* apThis, TESFaction* apFaction)
{
    const uint64_t cResult = RealSetCrimeFaction(apThis, apFaction);
    Raise(apThis);
    return cResult;
}

// Papyrus Actor.RemoveFromAllFactions (54735) clears the lists itself.
uint64_t HookRemoveFromAllFactions(void* apVm, uint32_t aStackId, Actor* apActor)
{
    const uint64_t cResult = RealRemoveFromAllFactions(apVm, aStackId, apActor);
    Raise(apActor);
    return cResult;
}

Hooks::Initializer s_factionHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TAddToFaction, s_addToFaction, 37686);
        POINTER_SKYRIMSE(TRemoveFromFaction, s_removeFromFaction, 37688);
        POINTER_SKYRIMSE(TSetFactionRank, s_setFactionRank, 37677);
        POINTER_SKYRIMSE(TRemoveFromFaction, s_setCrimeFaction, 37381);
        POINTER_SKYRIMSE(TRemoveFromAllFactions, s_removeFromAllFactions, 54735);
        RealAddToFaction = s_addToFaction.Get();
        RealRemoveFromFaction = s_removeFromFaction.Get();
        RealSetFactionRank = s_setFactionRank.Get();
        RealSetCrimeFaction = s_setCrimeFaction.Get();
        RealRemoveFromAllFactions = s_removeFromAllFactions.Get();
        HOOK(&RealAddToFaction, HookAddToFaction);
        HOOK(&RealRemoveFromFaction, HookRemoveFromFaction);
        HOOK(&RealSetFactionRank, HookSetFactionRank);
        HOOK(&RealSetCrimeFaction, HookSetCrimeFaction);
        HOOK(&RealRemoveFromAllFactions, HookRemoveFromAllFactions);
    });
} // namespace

void Start() noexcept
{
    ++s_users;
}

void Stop() noexcept
{
    if (s_users)
        --s_users;
}
} // namespace FactionEvents

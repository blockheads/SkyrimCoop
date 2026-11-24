#include <Combat/Traps.h>

#include <Actor.h>
#include <Combat/HitOwnership.h>
#include <Events/EventDispatcher.h>
#include <Events/TrapEvents.h>
#include <Games/ActorExtension.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <Services/HostService.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <TrapRules.h>
#include <VersionDb.h>
#include <World.h>

#include <algorithm>

namespace
{
// The VM's sink on TESTrapHitEvent (53999): OnTrapHitStart, OnTrapHitStop, OnTrapHit on the trap's scripts. Raised in
// the physics step.
using TTrapHitSink = uint32_t(void*, const TESTrapHitEvent*, void*);
TTrapHitSink* RealTrapHitSink = nullptr;

thread_local bool t_fromRequest = false;

uint32_t HookTrapHitSink(void* apThis, const TESTrapHitEvent* apEvent, void* apSource)
{
    Actor* pTarget = apEvent ? Cast<Actor>(apEvent->target) : nullptr;
    if (!pTarget || !apEvent->trap)
        return RealTrapHitSink(apThis, apEvent, apSource);
    World& world = World::Get();
    switch (TrapRules::OfContact(world.GetTransport().IsOnline(), world.GetHostService().IsHosting(), HitRules::PartyOf(pTarget), t_fromRequest))
    {
    case TrapRules::Contact::Raise: return RealTrapHitSink(apThis, apEvent, apSource);
    case TrapRules::Contact::Drop: return 0;
    case TrapRules::Contact::AskHost: break;
    }
    if (pTarget->IsDead()) // the sink's own check
        return 0;
    TrapContactEvent event{apEvent->trap->formID, pTarget->formID};
    event.Contact.Type = static_cast<Payloads::TrapContact::Kind>(apEvent->type);
    if (TrapRules::CarriesVectors(apEvent->type))
    {
        std::copy_n(apEvent->velocity, 3, event.Contact.Velocity.begin());
        std::copy_n(apEvent->position, 3, event.Contact.Position.begin());
    }
    event.Contact.Material = apEvent->material;
    event.Contact.Motion = apEvent->motion;
    event.Contact.Initial = apEvent->initial;
    world.GetRunner().Trigger(event);
    return 0;
}

// Actor::DoTrap1 (37509, TESObjectREFR vfunc 0x58, void): a script's ProcessTrapHit on the actor.
using TDoTrap = void(Actor*, TrapData&);
TDoTrap* RealDoTrap = nullptr;

void HookDoTrap(Actor* apThis, TrapData& arData)
{
    World& world = World::Get();
    switch (TrapRules::OfHit(world.GetTransport().IsOnline(), world.GetHostService().IsHosting(), HitRules::PartyOf(apThis)))
    {
    case TrapRules::Hit::Apply: return RealDoTrap(apThis, arData);
    case TrapRules::Hit::Drop: return;
    case TrapRules::Hit::AskOwner: break;
    }
    const TESObjectREFR* pTrap = TESObjectREFR::GetByHandle(arData.trapHandle);
    TrapHitEvent event{pTrap ? pTrap->formID : 0, apThis->formID};
    event.Hit.Damage = arData.damage;
    event.Hit.Push = arData.push;
    event.Hit.Stagger = arData.stagger;
    std::copy_n(arData.velocity, 3, event.Hit.Velocity.begin());
    std::copy_n(arData.position, 3, event.Hit.Position.begin());
    event.Hit.Material = arData.material;
    world.GetRunner().Trigger(event);
}

Hooks::Initializer s_trapHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(53999, &RealTrapHitSink, HookTrapHitSink);
    HOOK_ID(37509, &RealDoTrap, HookDoTrap);
});
} // namespace

void Traps::TakeHit(Actor& aActor, const TESObjectREFR* apTrap, const Payloads::TrapHit& acHit) noexcept
{
    TrapData data{};
    data.trapHandle = apTrap ? apTrap->GetHandle().handle.iBits : 0;
    data.damage = acHit.Damage;
    data.push = acHit.Push;
    data.stagger = acHit.Stagger;
    std::ranges::copy(acHit.Velocity, data.velocity);
    std::ranges::copy(acHit.Position, data.position);
    data.material = acHit.Material;
    RealDoTrap(&aActor, data);
}

void Traps::RaiseContact(TESObjectREFR& aTrap, Actor& aCopy, const Payloads::TrapContact& acContact) noexcept
{
    EventDispatcherManager* pEvents = EventDispatcherManager::Get();
    if (!pEvents)
        return;
    TESTrapHitEvent event{};
    event.trap = &aTrap;
    event.target = &aCopy;
    event.type = static_cast<TESTrapHitEvent::Type>(acContact.Type);
    std::ranges::copy(acContact.Velocity, event.velocity);
    std::ranges::copy(acContact.Position, event.position);
    event.material = acContact.Material;
    event.motion = acContact.Motion;
    event.initial = acContact.Initial;
    t_fromRequest = true;
    pEvents->trapHitEvent.PushEvent(&event);
    t_fromRequest = false;
}

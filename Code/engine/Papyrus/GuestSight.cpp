#include <Papyrus/GuestSight.h>

#include <scripts/HostSight.h>
#include <scripts/RoutedCallEvent.h>

#include <Actor.h>
#include <ActorExtension.h>
#include <HookSwitch.h>
#include <Misc/BSScript.h>
#include <Misc/GameVM.h>
#include <core/World.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace GuestSight
{
namespace
{
HostSight s_sight;
thread_local SkyrimVM* t_losPoll = nullptr; // set while 54037 polls the LOS registrations

// On the host, a guest's character: its sight is its own game's camera.
bool IsGuestViewer(Actor* apViewer) noexcept
{
    return apViewer && World::Hosts() && apViewer->GetExtension() && apViewer->GetExtension()->IsOtherPlayer();
}

// The registration being polled for this pair: no answer yet keeps its state, so no event is sent.
bool UnknownInPoll(SkyrimVM& aVm, uint32_t aViewer, uint32_t aTarget) noexcept
{
    for (uint32_t i = 0; i < aVm.losCount; ++i)
        if (LosRegistration* pEntry = aVm.losRegistrations[i]; pEntry && pEntry->viewer == aViewer && pEntry->target == aTarget)
        {
            const HostSight::Poll cPoll = HostSight::Unknown(pEntry->lastState);
            pEntry->lastState = cPoll.LastState;
            return cPoll.Sees;
        }
    return false;
}

// Actor::HasLineOfSight (53829): every script question about sight, Actor.HasLOS and the LOS registrations' poll (54037).
using THasLineOfSight = bool(Actor*, TESObjectREFR*, bool&);
THasLineOfSight* RealHasLineOfSight = nullptr;

bool HookHasLineOfSight(Actor* apThis, TESObjectREFR* apTarget, bool& aUsedActorLos)
{
    if (!apTarget || !IsGuestViewer(apThis))
        return RealHasLineOfSight(apThis, apTarget, aUsedActorLos);
    aUsedActorLos = false;
    const auto cReading = s_sight.Sees(apThis->formID, apTarget->formID, HostSight::Clock::now());
    if (cReading.Ask)
        World::Runner().Trigger(RoutedCallEvent{
            .To = CallTo::kOwner,
            .GuestFormId = apThis->formID,
            .ReplyId = cReading.Ask->ReplyId,
            .Call = {.Class = "Actor", .Function = "HasLOS", .Self = LocalForm{apThis->formID}, .Arguments = {LocalForm{apTarget->formID}}}});
    if (cReading.Sees)
        return *cReading.Sees;
    return t_losPoll && UnknownInPoll(*t_losPoll, apThis->formID, apTarget->formID);
}

// SkyrimVM::UpdateLOSRegistrations (54037), each VM update.
using TUpdateLosRegistrations = uint64_t(SkyrimVM*);
TUpdateLosRegistrations* RealUpdateLosRegistrations = nullptr;

uint64_t HookUpdateLosRegistrations(SkyrimVM* apThis)
{
    t_losPoll = apThis;
    const uint64_t cResult = RealUpdateLosRegistrations(apThis);
    t_losPoll = nullptr;
    return cResult;
}

// papyrus::Actor::HasLOS (54698, (vm, stackId, self, akOther)): refuses a non-player's sight of a non-actor before 53829.
using TPapyrusHasLos = bool(BSScript::IVirtualMachine*, uint32_t, Actor*, TESObjectREFR*);
TPapyrusHasLos* RealPapyrusHasLos = nullptr;

bool HookPapyrusHasLos(BSScript::IVirtualMachine* apVm, uint32_t aStackId, Actor* apSelf, TESObjectREFR* apOther)
{
    if (!apOther || !IsGuestViewer(apSelf))
        return RealPapyrusHasLos(apVm, aStackId, apSelf, apOther);
    bool usedActorLos = false;
    return apSelf->HasLineOfSight(apOther, usedActorLos);
}

// The RegisterForLOS natives' gate (53830, (viewer, target, vm, stackId)): the same refusal.
using TCanRegisterForLos = bool(Actor*, TESObjectREFR*, BSScript::IVirtualMachine*, uint32_t);
TCanRegisterForLos* RealCanRegisterForLos = nullptr;

bool HookCanRegisterForLos(Actor* apViewer, TESObjectREFR* apTarget, BSScript::IVirtualMachine* apVm, uint32_t aStackId)
{
    return (apTarget && IsGuestViewer(apViewer)) || RealCanRegisterForLos(apViewer, apTarget, apVm, aStackId);
}

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(53829, &RealHasLineOfSight, HookHasLineOfSight);
    HOOK_ID(54037, &RealUpdateLosRegistrations, HookUpdateLosRegistrations);
    HOOK_ID(54698, &RealPapyrusHasLos, HookPapyrusHasLos);
    HOOK_ID(53830, &RealCanRegisterForLos, HookCanRegisterForLos);
});
} // namespace

bool Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept
{
    const auto* cpSees = std::get_if<bool>(&acResult);
    return s_sight.Answer(aReplyId, cpSees && *cpSees, HostSight::Clock::now());
}
} // namespace GuestSight

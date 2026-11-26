#include <Effects/ActiveEffect.h>
#include <GuestNpcs.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// SummonCreatureEffect::CreateSummon (34993), where a summon is made: none on a guest, the host makes it (summons.md).
using TCreateSummon = void(ActiveEffect*);
static TCreateSummon* RealCreateSummon = nullptr;

static void HookCreateSummon(ActiveEffect* apThis)
{
    if (GuestNpcs::Session())
        return;
    RealCreateSummon(apThis);
}

static Hooks::Initializer s_summonHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(34993, &RealCreateSummon, HookCreateSummon);
});

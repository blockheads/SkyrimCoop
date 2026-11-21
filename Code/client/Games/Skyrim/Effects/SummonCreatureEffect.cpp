#include <Effects/ActiveEffect.h>
#include <Games/GuestNpcs.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// SummonCreatureEffect::CreateSummon (34993), the one place a summon is made: a guest's engine makes none, its
// player's summon is the host's to make (MagicTarget's HookAddTarget sends it; docs/re/systems/summons.md). Its effect
// still runs here: the portal, the timer.
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

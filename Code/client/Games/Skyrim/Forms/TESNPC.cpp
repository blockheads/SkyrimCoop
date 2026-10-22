#include <HookSwitch.h>

#include <Forms/TESNPC.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>
#include <Hooks/FunctionHook.h>

using TSetLeveledNpc = TESNPC*(TESNPC*, TESNPC*);
static TSetLeveledNpc* RealSetLeveledNpc = nullptr;

TESNPC* HookSetLeveledNpc(TESNPC* apThis, TESNPC* apSelectedNpc)
{
    spdlog::info("For TESNPC: {}, spawning: {}", apThis->fullName.value.AsAscii(), apSelectedNpc->fullName.value.AsAscii());

    return RealSetLeveledNpc(apThis, Cast<TESNPC>(TESForm::GetById(0x3B547)));
}

static Hooks::Initializer s_npcInitHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSetLeveledNpc, s_SetLeveledNpc, 14375);

        RealSetLeveledNpc = s_SetLeveledNpc.Get();

        // HOOK(&RealSetLeveledNpc, HookSetLeveledNpc);
    });

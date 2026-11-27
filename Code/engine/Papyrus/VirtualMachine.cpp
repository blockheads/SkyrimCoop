#include <Misc/BSScript.h>
#include <HookSwitch.h>
#include <Misc/NativeFunction.h>

#include <core/World.h>
#include <scripts/PapyrusFunctionRegisterEvent.h>
#include <Memory.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <Bridges/PapyrusService.h>

using TRegisterPapyrusFunction = void(BSScript::IVirtualMachine*, NativeFunction*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;

void HookRegisterPapyrusFunction(BSScript::IVirtualMachine* apThis, NativeFunction* apFunction)
{
    // Synchronous: natives register on the main thread during VM setup, and hooks such as
    // HookActivate look them up (PAPYRUS_FUNCTION) before the first World::Update would
    // drain a queued event, caching a null function pointer for good.
    World::Get().GetDispatcher().trigger(
        PapyrusFunctionRegisterEvent(apFunction->functionName.AsAscii(), apFunction->typeName.AsAscii(), apFunction->functionAddress));

    RealRegisterPapyrusFunction(apThis, apFunction);
}

static Hooks::Initializer s_vmHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(104788, &RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
    });

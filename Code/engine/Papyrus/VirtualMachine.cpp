#include <Misc/BSScript.h>
#include <HookSwitch.h>
#include <Misc/NativeFunction.h>

#include <Memory.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <Bridges/PapyrusService.h>

using TRegisterPapyrusFunction = void(BSScript::IVirtualMachine*, NativeFunction*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;

void HookRegisterPapyrusFunction(BSScript::IVirtualMachine* apThis, NativeFunction* apFunction)
{
    // At once: hooks such as HookActivate look natives up (PAPYRUS_FUNCTION) before our first update, caching for good.
    PapyrusNatives::Add(apFunction->typeName.AsAscii(), apFunction->functionName.AsAscii(), apFunction->functionAddress);

    RealRegisterPapyrusFunction(apThis, apFunction);
}

static Hooks::Initializer s_vmHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(104788, &RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
    });

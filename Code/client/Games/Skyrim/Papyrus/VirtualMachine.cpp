#include <Misc/BSScript.h>
#include <HookSwitch.h>
#include <Misc/NativeFunction.h>

#include <World.h>
#include <Events/PapyrusFunctionRegisterEvent.h>
#include <Games/Memory.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <Services/PapyrusService.h>

using TRegisterPapyrusFunction = void(BSScript::IVirtualMachine*, NativeFunction*);
using TBindEverythingToScript = void(BSScript::IVirtualMachine**);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;
TBindEverythingToScript* RealBindEverythingToScript = nullptr;

// SkyrimTogetherVerifyLaunchScript.DidLaunchSkyrimTogether: the plugin is loaded.
static bool DidLaunchSkyrimTogether()
{
    return true;
}

void HookRegisterPapyrusFunction(BSScript::IVirtualMachine* apThis, NativeFunction* apFunction)
{
    // Synchronous: natives register on the main thread during VM setup, and hooks such as
    // HookActivate look them up (PAPYRUS_FUNCTION) before the first World::Update would
    // drain a queued event, caching a null function pointer for good.
    World::Get().GetDispatcher().trigger(
        PapyrusFunctionRegisterEvent(apFunction->functionName.AsAscii(), apFunction->typeName.AsAscii(), apFunction->functionAddress));

    RealRegisterPapyrusFunction(apThis, apFunction);
}

void HookBindEverythingToScript(BSScript::IVirtualMachine** apThis)
{
    // The VM owns bound functions and frees them with the game heap (e.g. when the
    // script class is not installed), so they must come from the game heap too.
    // Plain `new` used the DLL's allocator and crashed BSSmallBlockAllocator at startup.
    (*apThis)->BindNativeMethod(Memory::New<BSScript::DidLaunchSkyrimTogetherFunc>("DidLaunchSkyrimTogether", "SkyrimTogetherVerifyLaunchScript", DidLaunchSkyrimTogether, BSScript::Variable::kBoolean));

    RealBindEverythingToScript(apThis);
}

static Hooks::Initializer s_vmHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(104788, &RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
        HOOK_ID(55739, &RealBindEverythingToScript, HookBindEverythingToScript);
    });
